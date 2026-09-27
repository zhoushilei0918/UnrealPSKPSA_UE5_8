#include "PskFactory.h"

#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "EditorFramework/AssetImportData.h"
#include "MeshDescription.h"
#include "UnrealPSKPSA.h"
#include "PskPsaUtils.h"
#include "PskReader.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

UObject* UPskFactory::Import(const FString& Filename, UObject* Parent, const FName Name, const EObjectFlags Flags, TMap<FString, FString> MaterialNameToPathMap)
{
	auto Data = FPskReader(Filename);
	if (!Data.bIsValid) return nullptr;

    if (Data.Bones.IsEmpty() || Data.Influences.IsEmpty())
    {
        UE_LOG(LogUnrealPSKPSA, Error, TEXT("PSK skeletal meshes require bones and weights. Use PSKX for static meshes."));
        return nullptr;
    }

	FSkeletalMeshImportData SkeletalMeshImportData;

	for (auto i = 0; i < Data.Normals.Num(); i++)
	{
		Data.Normals[i].Y = -Data.Normals[i].Y; // MIRROR_MESH
	}

	for (auto Vertex : Data.Vertices)
	{
		auto FixedVertex = Vertex;
		FixedVertex.Y = -FixedVertex.Y; // MIRROR_MESH
		SkeletalMeshImportData.Points.Add(FixedVertex);
		SkeletalMeshImportData.PointToRawMap.Add(SkeletalMeshImportData.Points.Num()-1);
	}
	
	auto WindingOrder = {2, 1, 0};
	for (const auto PskFace : Data.Faces)
	{
		SkeletalMeshImportData::FTriangle Face;
		Face.MatIndex = PskFace.MatIndex;
		Face.SmoothingGroups = PskFace.SmoothingGroups;
		Face.AuxMatIndex = 0;

		for (auto VertexIndex : WindingOrder)
		{
			const auto WedgeIndex = PskFace.WedgeIndex[VertexIndex];
			const auto PskWedge = Data.Wedges[WedgeIndex];
			
			SkeletalMeshImportData::FVertex Wedge;
			Wedge.MatIndex = PskFace.MatIndex;
			Wedge.VertexIndex = PskWedge.PointIndex;
			Wedge.Color = FColor::White;
            if (Data.bHasVertexColors)
            {
                Wedge.Color = Data.VertexColors[Data.VertexColors.Num() == Data.Wedges.Num() ? WedgeIndex : PskWedge.PointIndex];
                Swap(Wedge.Color.R, Wedge.Color.B); // ActorX stores RGBA; FColor uses BGRA on Windows.
            }
			Wedge.UVs[0] = FVector2f(PskWedge.U, PskWedge.V);
			for (auto UVIdx = 0; UVIdx < Data.ExtraUVs.Num(); UVIdx++)
			{
				auto UV =  Data.ExtraUVs[UVIdx][PskFace.WedgeIndex[VertexIndex]];
				Wedge.UVs[UVIdx+1] = UV;
			}
			
			Face.WedgeIndex[VertexIndex] = SkeletalMeshImportData.Wedges.Add(Wedge);
			Face.TangentZ[VertexIndex] = Data.bHasVertexNormals ? Data.Normals[Data.Normals.Num() == Data.Vertices.Num() ? PskWedge.PointIndex : WedgeIndex] : FVector3f::ZeroVector;
			Face.TangentY[VertexIndex] = FVector3f::ZeroVector;
			Face.TangentX[VertexIndex] = FVector3f::ZeroVector;
		}
		Swap(Face.WedgeIndex[0], Face.WedgeIndex[2]);
		Swap(Face.TangentZ[0], Face.TangentZ[2]);

		SkeletalMeshImportData.Faces.Add(Face);
	}

	TArray<FString> AddedBoneNames;
	for (auto PskBone : Data.Bones)
	{
		SkeletalMeshImportData::FBone Bone;
		Bone.Name = PskBone.Name;
		if (AddedBoneNames.Contains(Bone.Name)) continue;
		
		Bone.NumChildren = PskBone.NumChildren;
		Bone.ParentIndex = PskBone.ParentIndex == -1 ? INDEX_NONE : PskBone.ParentIndex;
		
		auto PskBonePos = PskBone.BonePos;
		FTransform3f PskTransform;
		PskTransform.SetLocation(FVector3f(PskBonePos.Position.X, -PskBonePos.Position.Y, PskBonePos.Position.Z));
		PskTransform.SetRotation(FQuat4f(PskBonePos.Orientation.X, -PskBonePos.Orientation.Y, PskBonePos.Orientation.Z, PskBonePos.Orientation.W).GetNormalized());

		SkeletalMeshImportData::FJointPos BonePos;
		BonePos.Transform = PskTransform;
		BonePos.Length = PskBonePos.Length;
		BonePos.XSize = PskBonePos.XSize;
		BonePos.YSize = PskBonePos.YSize;
		BonePos.ZSize = PskBonePos.ZSize;

		Bone.BonePos = BonePos;
		SkeletalMeshImportData.RefBonesBinary.Add(Bone);
		AddedBoneNames.Add(Bone.Name);
	}

	for (auto PskInfluence : Data.Influences)
	{
		SkeletalMeshImportData::FRawBoneInfluence Influence;
		Influence.BoneIndex = PskInfluence.BoneIdx;
		Influence.VertexIndex = PskInfluence.PointIdx;
		Influence.Weight = PskInfluence.Weight;
		SkeletalMeshImportData.Influences.Add(Influence);
	}

	for (auto PskMaterial : Data.Materials)
	{
		SkeletalMeshImportData::FMaterial Material;
		Material.MaterialImportName = PskMaterial.MaterialName;

		UObject* MatParent;
		auto FoundMaterialPath = MaterialNameToPathMap.Find(*Material.MaterialImportName);
		if (FoundMaterialPath != nullptr)
		{
			MatParent = CreatePackage(**FoundMaterialPath);
		}
		else
		{
			MatParent = Parent;
		}
		
		auto MaterialAdd = FPskPsaUtils::LocalFindOrCreate<UMaterialInstanceConstant>(UMaterialInstanceConstant::StaticClass(), MatParent, PskMaterial.MaterialName, Flags);
		Material.Material = MaterialAdd;
		SkeletalMeshImportData.Materials.Add(Material);
	}
	
	SkeletalMeshImportData.MaxMaterialIndex = SkeletalMeshImportData.Materials.Num()-1;

	SkeletalMeshImportData.bHasNormals = Data.bHasVertexNormals;
	SkeletalMeshImportData.bHasTangents = false;
	SkeletalMeshImportData.bHasVertexColors = Data.bHasVertexColors;
	SkeletalMeshImportData.NumTexCoords = 1 + Data.ExtraUVs.Num(); 
	
	const auto Skeleton = FPskPsaUtils::LocalCreate<USkeleton>(USkeleton::StaticClass(), Parent,  Name.ToString().Append("_Skeleton"), Flags);

	FReferenceSkeleton RefSkeleton;
	auto SkeletalDepth = 0;
	ProcessSkeleton(SkeletalMeshImportData, Skeleton, RefSkeleton, SkeletalDepth);

	const auto SkeletalMesh = FPskPsaUtils::LocalCreate<USkeletalMesh>(USkeletalMesh::StaticClass(), Parent, Name.ToString(), Flags);
	SkeletalMesh->PreEditChange(nullptr);
	SkeletalMesh->InvalidateDeriveDataCacheGUID();
	SkeletalMesh->UnregisterAllMorphTarget();

	SkeletalMesh->GetRefBasesInvMatrix().Empty();
	SkeletalMesh->GetMaterials().Empty();
	SkeletalMesh->SetHasVertexColors(Data.bHasVertexColors);
	if (Data.bHasVertexColors) SkeletalMesh->SetVertexColorGuid(FGuid::NewGuid());

	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	SkeletalMesh->SetNumSourceModels(0);
	FSkeletalMeshLODInfo& LODInfo = SkeletalMesh->AddLODInfo();
	LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
	LODInfo.LODHysteresis = 0.02f;

	ImportedResource->LODModels.Empty();
	ImportedResource->LODModels.Add(new FSkeletalMeshLODModel);
	SkeletalMesh->SetRefSkeleton(RefSkeleton);
	SkeletalMesh->CalculateInvRefMatrices();

	FSkeletalMeshBuildSettings BuildOptions;
	BuildOptions.bRemoveDegenerates = true;
	BuildOptions.bRecomputeNormals = !Data.bHasVertexNormals;
	BuildOptions.bRecomputeTangents = true;
	BuildOptions.bUseMikkTSpace = true;
	SkeletalMesh->GetLODInfo(0)->BuildSettings = BuildOptions;
	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(FBoxSphereBounds3f(FBox3f(SkeletalMeshImportData.Points))));

    for (const auto& Material : SkeletalMeshImportData.Materials)
    {
        const FName SlotName(*Material.MaterialImportName);
        SkeletalMesh->GetMaterials().Add(FSkeletalMaterial(Material.Material.Get(), true, false, SlotName, SlotName));
    }

    // Keep morph source positions in the mesh description so rebuilding/saving preserves them.
    int32 MorphOffset = 0;
    for (const VMorphInfo& Info : Data.MorphInfos)
    {
        SkeletalMeshImportData.MorphTargetNames.Add(UTF8_TO_TCHAR(Info.Name));
        FSkeletalMeshImportData& Morph = SkeletalMeshImportData.MorphTargets.AddDefaulted_GetRef();
        TSet<uint32>& Modified = SkeletalMeshImportData.MorphTargetModifiedPoints.AddDefaulted_GetRef();
        for (int32 Index = 0; Index < Info.VertexCount; ++Index)
        {
            const VMorphData& Delta = Data.MorphDatas[MorphOffset + Index];
            if (!Modified.Contains(Delta.PointIdx))
            {
                Modified.Add(Delta.PointIdx);
                Morph.Points.Add(SkeletalMeshImportData.Points[Delta.PointIdx] + Delta.PositionDelta * FVector3f(1, -1, 1));
            }
        }
        MorphOffset += Info.VertexCount;
    }

    FMeshDescription MeshDescription;
    if (!SkeletalMeshImportData.GetMeshDescription(SkeletalMesh, &BuildOptions, MeshDescription))
    {
        UE_LOG(LogUnrealPSKPSA, Error, TEXT("Failed to create skeletal mesh description for %s"), *Filename);
        SkeletalMesh->MarkAsGarbage();
        Skeleton->MarkAsGarbage();
        return nullptr;
    }
    SkeletalMesh->CreateMeshDescription(0, MoveTemp(MeshDescription));
    SkeletalMesh->CommitMeshDescription(0);
    SkeletalMesh->GetAssetImportData()->Update(Filename);
    SkeletalMesh->SetSkeleton(Skeleton);
    Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);
    SkeletalMesh->PostEditChange();
    FAssetCompilingManager::Get().FinishCompilationForObjects({SkeletalMesh});
	
	FAssetRegistryModule::AssetCreated(SkeletalMesh);
	SkeletalMesh->MarkPackageDirty();

	Skeleton->PostEditChange();
	FAssetRegistryModule::AssetCreated(Skeleton);
	Skeleton->MarkPackageDirty();

	return SkeletalMesh;
}

void UPskFactory::ProcessSkeleton(const FSkeletalMeshImportData& ImportData, const USkeleton* Skeleton, FReferenceSkeleton& OutRefSkeleton, int& OutSkeletalDepth)
{
	const auto RefBonesBinary = ImportData.RefBonesBinary;
	OutRefSkeleton.Empty();
	
	FReferenceSkeletonModifier RefSkeletonModifier(OutRefSkeleton, Skeleton);
	
	for (const auto Bone : RefBonesBinary)
	{
		const FMeshBoneInfo BoneInfo(FName(*Bone.Name), Bone.Name, Bone.ParentIndex);

		RefSkeletonModifier.Add(BoneInfo, FTransform(Bone.BonePos.Transform));
	}

    OutSkeletalDepth = 0;

    TArray<int> SkeletalDepths;
    SkeletalDepths.Empty(ImportData.RefBonesBinary.Num());
    SkeletalDepths.AddZeroed(ImportData.RefBonesBinary.Num());
    for (auto b = 0; b < OutRefSkeleton.GetNum(); b++)
    {
        const auto Parent = OutRefSkeleton.GetParentIndex(b);
        int32 Depth = 1;

        SkeletalDepths[b] = 1;
        if (Parent != INDEX_NONE)
        {
            Depth += SkeletalDepths[Parent];
        }
        if (OutSkeletalDepth < Depth)
        {
            OutSkeletalDepth = Depth;
        }
        SkeletalDepths[b] = Depth;
    }
}
