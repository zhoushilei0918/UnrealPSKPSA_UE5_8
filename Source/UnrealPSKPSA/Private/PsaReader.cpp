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
}

FPsaReader::FPsaReader(const FString& Filename)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Filename)) { Error = TEXT("无法读取 PSA 文件。"); return; }
    FMemoryReader Ar(Bytes);
    TSet<FString> Seen;
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
                if (Key.Position.ContainsNaN() || Key.Rotation.ContainsNaN() || Key.Rotation.SizeSquared() < SMALL_NUMBER || !FMath::IsFinite(Key.Time) || Key.Time < 0)
                { Error = FString::Printf(TEXT("第 %d 个动画关键帧包含无效的位置、零旋转四元数或时间。源 PSA 数据无法还原为有效姿态，请重新导出。"), KeyIndex); return; }
                Key.Rotation.Normalize();
                ++KeyIndex;
            }
        }
        else if (Name == TEXT("SCALEKEYS"))
        {
            if (!RequireSize(16)) return;
            ScaleKeys.SetNum(Count);
            for (auto& Key : ScaleKeys)
            {
                Ar.Serialize(&Key.Scale, 12); Ar << Key.Time;
                if (Key.Scale.ContainsNaN() || !FMath::IsFinite(Key.Time) || Key.Time < 0)
                { Error = TEXT("动画包含无效的缩放或时间。"); return; }
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
    bIsValid = true;
}
