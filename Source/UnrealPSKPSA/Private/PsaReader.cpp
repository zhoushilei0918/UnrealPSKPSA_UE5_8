#include "PsaReader.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"

namespace
{
    FString ReadName(FArchive& Ar, int32 Length)
    {
        ANSICHAR Buffer[65]{};
        check(Length <= 64);
        Ar.Serialize(Buffer, Length);
        return UTF8_TO_TCHAR(Buffer);
    }

    bool RepairPoseKeys(FPsaReader& Reader, const TBitArray<>& Invalid)
    {
        // Always find anchors in the original data, never in already repaired keys.
        const auto OriginalKeys = Reader.Keys;
        const auto OriginalScales = Reader.ScaleKeys;
        TBitArray<> Repaired(false, Reader.Keys.Num());
        for (const auto& Sequence : Reader.Sequences)
        {
            for (int32 Bone = 0; Bone < Sequence.TotalBones; ++Bone)
            {
                auto Index = [&](int32 Frame) { return int32((int64(Sequence.FirstRawFrame) + Frame) * Sequence.TotalBones + Bone); };
                int32 Frame = 0, Previous = INDEX_NONE;
                while (Frame < Sequence.NumRawFrames)
                {
                    if (!Invalid[Index(Frame)]) { Previous = Frame++; continue; }
                    const int32 Start = Frame;
                    while (Frame < Sequence.NumRawFrames && Invalid[Index(Frame)]) ++Frame;
                    const int32 Next = Frame < Sequence.NumRawFrames ? Frame : INDEX_NONE;
                    if (Previous == INDEX_NONE && Next == INDEX_NONE)
                    {
                        Reader.Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "NoRepairAnchor", "Animation {0}, bone {1} has no valid frames to use for repair."), FText::FromString(Sequence.Name), FText::FromString(UTF8_TO_TCHAR(Reader.Bones[Bone].Name))).ToString();
                        return false;
                    }
                    const bool bInterpolate = Previous != INDEX_NONE && Next != INDEX_NONE;
                    const int32 Left = Index(Previous == INDEX_NONE ? Next : Previous);
                    const int32 Right = Index(Next == INDEX_NONE ? Previous : Next);
                    for (int32 Missing = Start; Missing < Frame; ++Missing)
                    {
                        const int32 KeyIndex = Index(Missing);
                        const float Alpha = bInterpolate ? float(Missing - Previous) / float(Next - Previous) : 0.0f;
                        FPsaKey Key = OriginalKeys[KeyIndex];
                        Key.Position = FMath::Lerp(OriginalKeys[Left].Position, OriginalKeys[Right].Position, Alpha);
                        Key.Rotation = FQuat4f::Slerp(OriginalKeys[Left].Rotation, OriginalKeys[Right].Rotation, Alpha).GetNormalized();
                        const FVector3f Scale = OriginalScales.IsEmpty() ? FVector3f::OneVector :
                            FMath::Lerp(OriginalScales[Left].Scale, OriginalScales[Right].Scale, Alpha);
                        if (Key.Position.ContainsNaN() || Key.Rotation.ContainsNaN() || Scale.ContainsNaN())
                        {
                            Reader.Error = NSLOCTEXT("UnrealPSKPSA", "RepairOverflow", "Valid key values exceed the repair calculation range. Export the animation again.").ToString();
                            return false;
                        }
                        // Overlapping sequence ranges must not silently overwrite one another
                        // with different repairs determined by different animation boundaries.
                        if (Repaired[KeyIndex] && (!Reader.Keys[KeyIndex].Position.Equals(Key.Position, 0.00001f) ||
                            !Reader.Keys[KeyIndex].Rotation.Equals(Key.Rotation, 0.00001f) ||
                            (!Reader.ScaleKeys.IsEmpty() && !Reader.ScaleKeys[KeyIndex].Scale.Equals(Scale, 0.00001f))))
                        {
                            Reader.Error = NSLOCTEXT("UnrealPSKPSA", "RepairOverlap", "Overlapping animation ranges produce conflicting repairs. Export and import each animation separately.").ToString();
                            return false;
                        }
                        Reader.Keys[KeyIndex] = Key;
                        if (!Reader.ScaleKeys.IsEmpty()) Reader.ScaleKeys[KeyIndex].Scale = Scale;
                        if (!Repaired[KeyIndex])
                        {
                            if (bInterpolate) ++Reader.InterpolatedKeyCount;
                            else ++Reader.CopiedKeyCount;
                            Repaired[KeyIndex] = true;
                        }
                    }
                }
            }
        }
        for (int32 Key = 0; Key < Invalid.Num(); ++Key)
            if (Invalid[Key] && !Repaired[Key])
            {
                Reader.Error = NSLOCTEXT("UnrealPSKPSA", "RepairOutsideRange", "Invalid keys lie outside the animation ranges; no repair anchors can be determined.").ToString();
                return false;
            }
        return true;
    }
}

FPsaReader::FPsaReader(const FString& Filename, bool bRepairInvalidKeys)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Filename)) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaReadFailed", "Cannot read the PSA file.").ToString(); return; }
    FMemoryReader Ar(Bytes);
    TSet<FString> Seen;
    TArray<int32> InvalidPoseKeys, InvalidScaleKeys;
    while (Ar.Tell() < Ar.TotalSize())
    {
        if (Ar.TotalSize() - Ar.Tell() < 32) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaTruncatedHeader", "The PSA chunk header is truncated.").ToString(); return; }
        const FString Name = ReadName(Ar, 20);
        int32 Type, Size, Count;
        Ar << Type << Size << Count;
        if (Seen.IsEmpty() && Name != TEXT("ANIMHEAD")) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaMissingSignature", "Invalid PSA file: missing ANIMHEAD.").ToString(); return; }
        const int64 Payload = int64(Size) * Count;
        if (Size < 0 || Count < 0 || (Count > 0 && Size == 0) || Payload > Ar.TotalSize() - Ar.Tell())
        { Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "PsaInvalidChunk", "PSA chunk {0} has an invalid size or is truncated."), FText::FromString(Name)).ToString(); return; }
        const int64 End = Ar.Tell() + Payload;
        const bool Known = Name == TEXT("BONENAMES") || Name == TEXT("ANIMINFO") || Name == TEXT("ANIMKEYS") || Name == TEXT("SCALEKEYS");
        if (Known && Seen.Contains(Name)) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaDuplicateChunk", "The PSA contains duplicate chunks.").ToString(); return; }
        Seen.Add(Name);
        auto RequireSize = [&](int32 Expected)
        {
            if (Size != Expected) { Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "PsaRecordSize", "{0} record size should be {1}; found {2}."), FText::FromString(Name), Expected, Size).ToString(); return false; }
            return true;
        };
        if (Name == TEXT("BONENAMES"))
        {
            if (!RequireSize(120)) return;
            Bones.SetNum(Count);
            for (auto& Bone : Bones)
            {
                Ar.Serialize(Bone.Name, 64); Bone.Name[63] = 0;
                Ar << Bone.Flags << Bone.NumChildren << Bone.ParentIndex;
                Ar.Serialize(&Bone.BonePos.Orientation, 16);
                Ar.Serialize(&Bone.BonePos.Position, 12);
                Ar << Bone.BonePos.Length << Bone.BonePos.XSize << Bone.BonePos.YSize << Bone.BonePos.ZSize;
            }
        }
        else if (Name == TEXT("ANIMINFO"))
        {
            if (!RequireSize(168)) return;
            Sequences.SetNum(Count);
            for (auto& Info : Sequences)
            {
                Info.Name = ReadName(Ar, 64);
                ReadName(Ar, 64); // Group
                int32 RootInclude, Compression, KeyQuotum, StartBone;
                float Reduction, TrackTime;
                Ar << Info.TotalBones << RootInclude << Compression << KeyQuotum << Reduction << TrackTime << Info.AnimRate;
                Ar << StartBone << Info.FirstRawFrame << Info.NumRawFrames;
                if (StartBone != 0 || Compression != 0) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaUnsupportedData", "Partial-skeleton or compressed PSA data is not supported.").ToString(); return; }
            }
        }
        else if (Name == TEXT("ANIMKEYS"))
        {
            if (!RequireSize(32)) return;
            Keys.SetNum(Count);
            int32 KeyIndex = 0;
            for (auto& Key : Keys)
            {
                Ar.Serialize(&Key.Position, 12);
                Ar.Serialize(&Key.Rotation, 16);
                Ar << Key.Time;
                if (!FMath::IsFinite(Key.Time) || Key.Time < 0)
                { Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "PsaInvalidKeyTime", "Animation key {0} has an invalid time. Export it again."), KeyIndex).ToString(); return; }
                const float RotationLength = Key.Rotation.SizeSquared();
                const bool bInvalidPose = Key.Position.ContainsNaN() || Key.Rotation.ContainsNaN() ||
                    !FMath::IsFinite(RotationLength) || RotationLength < SMALL_NUMBER;
                if (bInvalidPose)
                {
                    if (!bRepairInvalidKeys)
                    { Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "PsaInvalidPose", "Animation key {0} contains an invalid position or rotation (such as a zero quaternion). Enable \"Repair Invalid Keys\" to attempt repair, or export it again."), KeyIndex).ToString(); return; }
                    InvalidPoseKeys.Add(KeyIndex);
                }
                else Key.Rotation.Normalize();
                ++KeyIndex;
            }
        }
        else if (Name == TEXT("SCALEKEYS"))
        {
            if (!RequireSize(16)) return;
            ScaleKeys.SetNum(Count);
            int32 KeyIndex = 0;
            for (auto& Key : ScaleKeys)
            {
                Ar.Serialize(&Key.Scale, 12); Ar << Key.Time;
                if (!FMath::IsFinite(Key.Time) || Key.Time < 0)
                { Error = NSLOCTEXT("UnrealPSKPSA", "PsaInvalidScaleTime", "The animation contains an invalid scale key time. Export it again.").ToString(); return; }
                if (Key.Scale.ContainsNaN())
                {
                    if (!bRepairInvalidKeys) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaInvalidScale", "The animation contains invalid scale values. Enable \"Repair Invalid Keys\" to attempt repair, or export it again.").ToString(); return; }
                    InvalidScaleKeys.Add(KeyIndex);
                }
                ++KeyIndex;
            }
        }
        Ar.Seek(End);
    }
    if (Bones.IsEmpty() || Sequences.IsEmpty() || Keys.IsEmpty()) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaMissingData", "The PSA is missing bones, animation information, or keys.").ToString(); return; }
    // UEViewer ExportPsk.cpp::DoExportPsa writes this exact metadata pattern.
    // Do not generalize to arbitrary parent conflicts: real hierarchies still need
    // strict validation, including the FModel files used by existing imports.
    bHasUEViewerBoneMetadata = Bones.Num() > 1 && Bones[0].ParentIndex == -1;
    for (int32 Index = 0; Index < Bones.Num() && bHasUEViewerBoneMetadata; ++Index)
    {
        const auto& Bone = Bones[Index];
        bHasUEViewerBoneMetadata = Bone.Flags == 0 && Bone.NumChildren == 0 &&
            Bone.ParentIndex == (Index == 0 ? -1 : 0) && Bone.BonePos.Length == 1.0f &&
            Bone.BonePos.XSize == 0 && Bone.BonePos.YSize == 0 && Bone.BonePos.ZSize == 0;
    }
    TSet<FName> BoneNames;
    for (int32 Index = 0; Index < Bones.Num(); ++Index)
    {
        auto& Bone = Bones[Index];
        const FName Name(UTF8_TO_TCHAR(Bone.Name));
        if (Name.IsNone() || BoneNames.Contains(Name) || (Index == 0 && Bone.ParentIndex != -1 && Bone.ParentIndex != 0) ||
            (Index > 0 && (Bone.ParentIndex < 0 || Bone.ParentIndex >= Index)))
        { Error = NSLOCTEXT("UnrealPSKPSA", "PsaInvalidHierarchy", "The PSA has duplicate or empty bone names, or an invalid hierarchy.").ToString(); return; }
        BoneNames.Add(Name);
        if (Index == 0) Bone.ParentIndex = INDEX_NONE;
    }
    for (const auto& Info : Sequences)
    {
        const int64 EndKey = (int64(Info.FirstRawFrame) + Info.NumRawFrames) * Info.TotalBones;
        if (Info.Name.IsEmpty() || Info.TotalBones != Bones.Num() || Info.FirstRawFrame < 0 || Info.NumRawFrames < 1 ||
            !FMath::IsFinite(Info.AnimRate) || Info.AnimRate <= 0 || Info.AnimRate > 1000 || EndKey > Keys.Num())
        { Error = NSLOCTEXT("UnrealPSKPSA", "PsaInvalidSequence", "The PSA animation bone count, frame range, or frame rate is invalid.").ToString(); return; }
    }
    if (!ScaleKeys.IsEmpty() && ScaleKeys.Num() != Keys.Num()) { Error = NSLOCTEXT("UnrealPSKPSA", "PsaScaleCountMismatch", "The PSA scale key count does not match the animation key count.").ToString(); return; }
    if (!InvalidPoseKeys.IsEmpty() || !InvalidScaleKeys.IsEmpty())
    {
        TBitArray<> Invalid(false, Keys.Num());
        for (int32 Index : InvalidPoseKeys) Invalid[Index] = true;
        for (int32 Index : InvalidScaleKeys) Invalid[Index] = true;
        if (!RepairPoseKeys(*this, Invalid)) return;
    }
    bIsValid = true;
}
