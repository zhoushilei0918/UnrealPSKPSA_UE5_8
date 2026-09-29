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
                        Reader.Error = FString::Printf(TEXT("动画 %s 的骨骼 %s 全部帧无效，没有可用于补帧的有效帧。"),
                            *Sequence.Name, UTF8_TO_TCHAR(Reader.Bones[Bone].Name));
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
                            Reader.Error = TEXT("有效帧数值超出补帧计算范围，请重新导出动画。");
                            return false;
                        }
                        // Overlapping sequence ranges must not silently overwrite one another
                        // with different repairs determined by different animation boundaries.
                        if (Repaired[KeyIndex] && (!Reader.Keys[KeyIndex].Position.Equals(Key.Position, 0.00001f) ||
                            !Reader.Keys[KeyIndex].Rotation.Equals(Key.Rotation, 0.00001f) ||
                            (!Reader.ScaleKeys.IsEmpty() && !Reader.ScaleKeys[KeyIndex].Scale.Equals(Scale, 0.00001f))))
                        {
                            Reader.Error = TEXT("动画帧区间重叠，补帧结果存在冲突，请分别导出动画后再导入。");
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
                Reader.Error = TEXT("无效关键帧位于动画范围之外，无法确定补帧依据。");
                return false;
            }
        return true;
    }
}

FPsaReader::FPsaReader(const FString& Filename, bool bRepairInvalidKeys)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Filename)) { Error = TEXT("无法读取 PSA 文件。"); return; }
    FMemoryReader Ar(Bytes);
    TSet<FString> Seen;
    TArray<int32> InvalidPoseKeys, InvalidScaleKeys;
    while (Ar.Tell() < Ar.TotalSize())
    {
        if (Ar.TotalSize() - Ar.Tell() < 32) { Error = TEXT("PSA 数据块头被截断。"); return; }
        const FString Name = ReadName(Ar, 20);
        int32 Type, Size, Count;
        Ar << Type << Size << Count;
        if (Seen.IsEmpty() && Name != TEXT("ANIMHEAD")) { Error = TEXT("文件不是有效的 PSA（缺少 ANIMHEAD）。"); return; }
        const int64 Payload = int64(Size) * Count;
        if (Size < 0 || Count < 0 || (Count > 0 && Size == 0) || Payload > Ar.TotalSize() - Ar.Tell())
        { Error = FString::Printf(TEXT("PSA 数据块 %s 长度无效或文件被截断。"), *Name); return; }
        const int64 End = Ar.Tell() + Payload;
        const bool Known = Name == TEXT("BONENAMES") || Name == TEXT("ANIMINFO") || Name == TEXT("ANIMKEYS") || Name == TEXT("SCALEKEYS");
        if (Known && Seen.Contains(Name)) { Error = TEXT("PSA 包含重复的数据块。"); return; }
        Seen.Add(Name);
        auto RequireSize = [&](int32 Expected)
        {
            if (Size != Expected) { Error = FString::Printf(TEXT("%s 记录大小应为 %d，实际为 %d。"), *Name, Expected, Size); return false; }
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
                if (StartBone != 0 || Compression != 0) { Error = TEXT("暂不支持局部骨骼或压缩的 PSA 数据。"); return; }
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
                { Error = FString::Printf(TEXT("第 %d 个动画关键帧时间无效，请重新导出。"), KeyIndex); return; }
                const float RotationLength = Key.Rotation.SizeSquared();
                const bool bInvalidPose = Key.Position.ContainsNaN() || Key.Rotation.ContainsNaN() ||
                    !FMath::IsFinite(RotationLength) || RotationLength < SMALL_NUMBER;
                if (bInvalidPose)
                {
                    if (!bRepairInvalidKeys)
                    { Error = FString::Printf(TEXT("第 %d 个动画关键帧包含无效的位置或旋转（如零四元数）。可勾选“修复无效关键帧”尝试补帧，或重新导出。"), KeyIndex); return; }
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
                { Error = TEXT("动画包含无效的缩放关键帧时间，请重新导出。"); return; }
                if (Key.Scale.ContainsNaN())
                {
                    if (!bRepairInvalidKeys) { Error = TEXT("动画包含无效的缩放。可勾选“修复无效关键帧”尝试补帧，或重新导出。"); return; }
                    InvalidScaleKeys.Add(KeyIndex);
                }
                ++KeyIndex;
            }
        }
        Ar.Seek(End);
    }
    if (Bones.IsEmpty() || Sequences.IsEmpty() || Keys.IsEmpty()) { Error = TEXT("PSA 缺少骨骼、动画信息或关键帧。"); return; }
    TSet<FName> BoneNames;
    for (int32 Index = 0; Index < Bones.Num(); ++Index)
    {
        auto& Bone = Bones[Index];
        const FName Name(UTF8_TO_TCHAR(Bone.Name));
        if (Name.IsNone() || BoneNames.Contains(Name) || (Index == 0 && Bone.ParentIndex != -1 && Bone.ParentIndex != 0) ||
            (Index > 0 && (Bone.ParentIndex < 0 || Bone.ParentIndex >= Index)))
        { Error = TEXT("PSA 骨骼名称重复、为空或层级无效。"); return; }
        BoneNames.Add(Name);
        if (Index == 0) Bone.ParentIndex = INDEX_NONE;
    }
    for (const auto& Info : Sequences)
    {
        const int64 EndKey = (int64(Info.FirstRawFrame) + Info.NumRawFrames) * Info.TotalBones;
        if (Info.Name.IsEmpty() || Info.TotalBones != Bones.Num() || Info.FirstRawFrame < 0 || Info.NumRawFrames < 1 ||
            !FMath::IsFinite(Info.AnimRate) || Info.AnimRate <= 0 || Info.AnimRate > 1000 || EndKey > Keys.Num())
        { Error = TEXT("PSA 动画骨骼数量、帧范围或帧率无效。"); return; }
    }
    if (!ScaleKeys.IsEmpty() && ScaleKeys.Num() != Keys.Num()) { Error = TEXT("PSA 缩放帧数量与动画帧数量不一致。"); return; }
    if (!InvalidPoseKeys.IsEmpty() || !InvalidScaleKeys.IsEmpty())
    {
        TBitArray<> Invalid(false, Keys.Num());
        for (int32 Index : InvalidPoseKeys) Invalid[Index] = true;
        for (int32 Index : InvalidScaleKeys) Invalid[Index] = true;
        if (!RepairPoseKeys(*this, Invalid)) return;
    }
    bIsValid = true;
}
