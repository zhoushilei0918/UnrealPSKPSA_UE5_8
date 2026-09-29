#include "PskReader.h"

#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "UnrealPSKPSA.h"

FPskReader::FPskReader(const FString& Filepath)
{
    auto Fail = [this, &Filepath](const FString& Reason)
    {
        ErrorMessage = Reason;
        UE_LOG(LogUnrealPSKPSA, Error, TEXT("%s"), *FText::Format(NSLOCTEXT("UnrealPSKPSA", "PskImportError", "Cannot import '{0}': {1}"), FText::FromString(Filepath), FText::FromString(Reason)).ToString());
    };

    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Filepath))
    {
        Fail(NSLOCTEXT("UnrealPSKPSA", "PskReadFailed", "Cannot read file.").ToString());
        return;
    }
    FMemoryReader Ar(Bytes);
    bool bReadHeader = false;
    while (Ar.Tell() < Ar.TotalSize())
    {
        if (Ar.TotalSize() - Ar.Tell() < 32)
        {
            Fail(NSLOCTEXT("UnrealPSKPSA", "PskTruncatedHeader", "Truncated chunk header.").ToString());
            return;
        }
        VChunkHeader Header{};
        Ar.Serialize(&Header, 32);
        ANSICHAR ChunkName[21]{};
        FMemory::Memcpy(ChunkName, Header.ChunkID, 20);
        const FString Name = UTF8_TO_TCHAR(ChunkName);
        if (!bReadHeader && Name != TEXT("ACTRHEAD"))
        {
            Fail(NSLOCTEXT("UnrealPSKPSA", "PskMissingSignature", "Missing ACTRHEAD signature.").ToString());
            return;
        }
        bReadHeader = true;
        const int32 Count = Header.DataCount;
        const int32 Size = Header.DataSize;
        const int64 PayloadSize = int64(Size) * Count;
        if (Size < 0 || Count < 0 || (Count > 0 && Size == 0) || PayloadSize > Ar.TotalSize() - Ar.Tell())
        {
            Fail(FText::Format(NSLOCTEXT("UnrealPSKPSA", "PskInvalidChunk", "Invalid or truncated chunk {0}."), FText::FromString(Name)).ToString());
            return;
        }
        const int64 End = Ar.Tell() + PayloadSize;
        auto CheckSize = [&](int32 Expected)
        {
            if (Size != Expected && Count != 0)
            {
                Fail(FText::Format(NSLOCTEXT("UnrealPSKPSA", "PskRecordSize", "Chunk {0} has record size {1}; expected {2}."), FText::FromString(Name), Size, Expected).ToString());
                return false;
            }
            return true;
        };
        auto ReadArray = [&](auto& Values, int32 Expected)
        {
            if (!CheckSize(Expected)) return false;
            Values.SetNum(Count);
            if (Count) Ar.Serialize(Values.GetData(), PayloadSize);
            return true;
        };
        if (Name == TEXT("PNTS0000"))
        {
            if (!ReadArray(Vertices, 12)) return;
        }
        else if (Name == TEXT("VTXW0000"))
        {
            if (!ReadArray(Wedges, 16)) return;
            // The classic 16-bit index has two padding bytes; extended exporters use all 32 bits.
            if (Vertices.Num() <= 65536)
                for (VVertex& Wedge : Wedges) Wedge.PointIndex &= 0xffff;
        }
        else if (Name == TEXT("FACE0000") || Name == TEXT("FACE3200"))
        {
            const bool bWide = Name == TEXT("FACE3200");
            if (!CheckSize(bWide ? 18 : 12)) return;
            Faces.SetNumZeroed(Count);
            for (VTriangle& Face : Faces)
            {
                for (int32& Index : Face.WedgeIndex)
                {
                    if (bWide) Ar << Index;
                    else { uint16 ShortIndex; Ar << ShortIndex; Index = ShortIndex; }
                }
                Ar << Face.MatIndex << Face.AuxMatIndex << Face.SmoothingGroups;
            }
        }
        else if (Name == TEXT("MATT0000"))
        {
            if (!ReadArray(Materials, 88)) return;
            for (VMaterial& Material : Materials) Material.MaterialName[63] = '\0';
        }
        else if (Name == TEXT("VTXNORMS"))
        {
            if (!ReadArray(Normals, 12)) return;
        }
        else if (Name == TEXT("VERTEXCOLOR"))
        {
            if (!ReadArray(VertexColors, 4)) return;
        }
        else if (Name.StartsWith(TEXT("EXTRAUVS")))
        {
            const FString Suffix = Name.Mid(8);
            if (!Suffix.IsNumeric() || Suffix.Len() != 1 || FCString::Atoi(*Suffix) >= 7)
            {
                Fail(NSLOCTEXT("UnrealPSKPSA", "PskExtraUVLimit", "Unsupported extra UV channel (maximum total is 8).").ToString());
                return;
            }
            const int32 Channel = FCString::Atoi(*Suffix);
            if (ExtraUVs.Num() <= Channel) ExtraUVs.SetNum(Channel + 1);
            if (!ReadArray(ExtraUVs[Channel], 8)) return;
        }
        else if (Name == TEXT("REFSKELT") || Name == TEXT("REFSKEL0"))
        {
            if (!CheckSize(120)) return;
            Bones.SetNum(Count);
            for (VNamedBoneBinary& Bone : Bones)
            {
                // Read fields explicitly: UE quaternion alignment is not the ActorX disk layout.
                Ar.Serialize(Bone.Name, 64);
                Bone.Name[63] = '\0';
                Ar << Bone.Flags << Bone.NumChildren << Bone.ParentIndex;
                Ar.Serialize(&Bone.BonePos.Orientation, 16);
                Ar.Serialize(&Bone.BonePos.Position, 12);
                Ar << Bone.BonePos.Length << Bone.BonePos.XSize << Bone.BonePos.YSize << Bone.BonePos.ZSize;
            }
        }
        else if (Name == TEXT("RAWWEIGHTS") || Name == TEXT("RAWW0000"))
        {
            if (!ReadArray(Influences, 12)) return;
        }
        else if (Name == TEXT("MRPHINFO"))
        {
            if (!ReadArray(MorphInfos, 68)) return;
            for (VMorphInfo& Morph : MorphInfos) Morph.Name[63] = '\0';
        }
        else if (Name == TEXT("MRPHDATA"))
        {
            if (!ReadArray(MorphDatas, 28)) return;
        }
        // Also handles repeated ACTRHEAD and future optional chunks.
        Ar.Seek(End);
    }

    if (Vertices.IsEmpty() || Wedges.IsEmpty() || Faces.IsEmpty() || Materials.IsEmpty())
    {
        Fail(NSLOCTEXT("UnrealPSKPSA", "PskMissingData", "Missing points, wedges, faces or materials.").ToString());
        return;
    }
    for (const FVector3f& Point : Vertices)
        if (Point.ContainsNaN()) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskNonFinitePosition", "Non-finite vertex position.").ToString()); return; }
    for (const VVertex& Wedge : Wedges)
        if (!Vertices.IsValidIndex(Wedge.PointIndex) || !FMath::IsFinite(Wedge.U) || !FMath::IsFinite(Wedge.V))
        { Fail(NSLOCTEXT("UnrealPSKPSA", "PskInvalidWedge", "Invalid wedge point index or UV.").ToString()); return; }
    for (const VTriangle& Face : Faces)
    {
        for (int32 Index : Face.WedgeIndex)
            if (!Wedges.IsValidIndex(Index)) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskInvalidFaceWedge", "Invalid face wedge index.").ToString()); return; }
        if (!Materials.IsValidIndex(Face.MatIndex)) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskInvalidMaterial", "Invalid face material index.").ToString()); return; }
    }
    if ((!Normals.IsEmpty() && Normals.Num() != Vertices.Num() && Normals.Num() != Wedges.Num()) ||
        (!VertexColors.IsEmpty() && VertexColors.Num() != Wedges.Num() && VertexColors.Num() != Vertices.Num()))
    { Fail(NSLOCTEXT("UnrealPSKPSA", "PskAttributeCount", "Normal or color count does not match points or wedges.").ToString()); return; }
    for (const FVector3f& Normal : Normals)
        if (Normal.ContainsNaN()) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskNonFiniteNormal", "Non-finite normal.").ToString()); return; }
    for (const TArray<FVector2f>& UVs : ExtraUVs)
    {
        if (UVs.Num() != Wedges.Num()) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskExtraUVCount", "Extra UV count does not match wedges.").ToString()); return; }
        for (const FVector2f& UV : UVs)
            if (UV.ContainsNaN()) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskNonFiniteUV", "Non-finite UV.").ToString()); return; }
    }
    TSet<FName> BoneNames;
    for (int32 Index = 0; Index < Bones.Num(); ++Index)
    {
        VNamedBoneBinary& Bone = Bones[Index];
        const FName BoneName(UTF8_TO_TCHAR(Bone.Name));
        if (BoneName.IsNone() || BoneNames.Contains(BoneName) ||
            (Index == 0 && Bone.ParentIndex != -1 && Bone.ParentIndex != 0) ||
            (Index > 0 && (Bone.ParentIndex < 0 || Bone.ParentIndex >= Index)) ||
            Bone.BonePos.Position.ContainsNaN() || Bone.BonePos.Orientation.ContainsNaN())
        { Fail(NSLOCTEXT("UnrealPSKPSA", "PskInvalidBone", "Invalid bone name, hierarchy or transform.").ToString()); return; }
        BoneNames.Add(BoneName);
        if (Index == 0) Bone.ParentIndex = INDEX_NONE;
    }
    for (const VRawBoneInfluence& Influence : Influences)
        if (!Vertices.IsValidIndex(Influence.PointIdx) || !Bones.IsValidIndex(Influence.BoneIdx) ||
            !FMath::IsFinite(Influence.Weight) || Influence.Weight < 0)
        { Fail(NSLOCTEXT("UnrealPSKPSA", "PskInvalidWeight", "Invalid skin weight or bone/point index.").ToString()); return; }
    int64 MorphCount = 0;
    for (const VMorphInfo& Morph : MorphInfos)
    {
        if (Morph.VertexCount < 0) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskNegativeMorphCount", "Negative morph vertex count.").ToString()); return; }
        MorphCount += Morph.VertexCount;
    }
    if (MorphCount != MorphDatas.Num()) { Fail(NSLOCTEXT("UnrealPSKPSA", "PskMorphCount", "Morph data count mismatch.").ToString()); return; }
    for (const VMorphData& Morph : MorphDatas)
        if (!Vertices.IsValidIndex(Morph.PointIdx) || Morph.PositionDelta.ContainsNaN() || Morph.TangentZDelta.ContainsNaN())
        { Fail(NSLOCTEXT("UnrealPSKPSA", "PskInvalidMorph", "Invalid morph delta.").ToString()); return; }

    bHasVertexNormals = !Normals.IsEmpty();
    bHasVertexColors = !VertexColors.IsEmpty();
    bHasMorphData = !MorphInfos.IsEmpty() && !MorphDatas.IsEmpty();
    bIsValid = true;
    UE_LOG(LogUnrealPSKPSA, Display, TEXT("%s"), *FText::Format(NSLOCTEXT("UnrealPSKPSA", "PskReadSummary", "Read {0}: {1} points, {2} triangles, {3} bones, {4} UV channels"), FText::FromString(Filepath), Vertices.Num(), Faces.Num(), Bones.Num(), ExtraUVs.Num() + 1).ToString());
}
