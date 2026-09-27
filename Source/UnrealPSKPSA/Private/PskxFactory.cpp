#include "PskxFactory.h"

#include "PskPsaUtils.h"
#include "PskReader.h"
#include "RawMesh.h"
#include "AssetCompilingManager.h"
#include "EditorFramework/AssetImportData.h"
#include "Materials/MaterialInstanceConstant.h"

UObject* UPskxFactory::Import(const FString& Filename, UObject* Parent, const FName Name, const EObjectFlags Flags, TMap<FString, FString> MaterialNameToPathMap)
{
	auto Data = FPskReader(Filename);
	if (!Data.bIsValid) return nullptr;
	
	auto RawMesh = FRawMesh();
	for (auto Vertex : Data.Vertices)
	{
		auto FixedVertex = Vertex;
		FixedVertex.Y = -FixedVertex.Y; // MIRROR_MESH
		RawMesh.VertexPositions.Add(FixedVertex);
	}

	auto WindingOrder = {2, 1, 0};
	for (const auto PskFace : Data.Faces)
	{
		RawMesh.FaceMaterialIndices.Add(PskFace.MatIndex);
		RawMesh.FaceSmoothingMasks.Add(PskFace.SmoothingGroups);

		for (auto VertexIndex : WindingOrder)
		{
			const auto WedgeIndex = PskFace.WedgeIndex[VertexIndex];
			const auto PskWedge = Data.Wedges[WedgeIndex];

			RawMesh.WedgeIndices.Add(PskWedge.PointIndex);
			FColor Color = FColor::White;
            if (Data.bHasVertexColors)
            {
                Color = Data.VertexColors[Data.VertexColors.Num() == Data.Wedges.Num() ? WedgeIndex : PskWedge.PointIndex];
                Swap(Color.R, Color.B);
            }
            RawMesh.WedgeColors.Add(Color);
			RawMesh.WedgeTexCoords[0].Add(FVector2f(PskWedge.U, PskWedge.V));
			for (auto UVIndex = 0; UVIndex < Data.ExtraUVs.Num(); UVIndex++)
			{
				auto UV =  Data.ExtraUVs[UVIndex][PskFace.WedgeIndex[VertexIndex]];
				RawMesh.WedgeTexCoords[UVIndex+1].Add(UV);
			}
			
			RawMesh.WedgeTangentZ.Add(Data.bHasVertexNormals ? Data.Normals[Data.Normals.Num() == Data.Vertices.Num() ? PskWedge.PointIndex : WedgeIndex] * FVector3f(1, -1, 1) : FVector3f::ZeroVector);
			RawMesh.WedgeTangentY.Add(FVector3f::ZeroVector);
			RawMesh.WedgeTangentX.Add(FVector3f::ZeroVector);
		}
	}
	
	const auto StaticMesh = FPskPsaUtils::LocalCreate<UStaticMesh>(UStaticMesh::StaticClass(), Parent, Name.ToString(), Flags);

	for (auto i = 0; i < Data.Materials.Num(); i++)
	{
		auto PskMaterial = Data.Materials[i];
		
		UObject* MatParent;
		auto FoundMaterialPath = MaterialNameToPathMap.Find(PskMaterial.MaterialName);
		if (FoundMaterialPath != nullptr)
		{
			MatParent = CreatePackage(**FoundMaterialPath);
		}
		else
		{
			MatParent = Parent;
		}
		
		auto MaterialAdd = FPskPsaUtils::LocalFindOrCreate<UMaterialInstanceConstant>(UMaterialInstanceConstant::StaticClass(), MatParent, PskMaterial.MaterialName, Flags);

		StaticMesh->GetStaticMaterials().Add(FStaticMaterial(MaterialAdd, FName(UTF8_TO_TCHAR(PskMaterial.MaterialName)), FName(UTF8_TO_TCHAR(PskMaterial.MaterialName))));
		StaticMesh->GetSectionInfoMap().Set(0, i, FMeshSectionInfo(i));
	}
	
	auto& SourceModel = StaticMesh->AddSourceModel();
	SourceModel.BuildSettings.bGenerateLightmapUVs = false;
	SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;
	SourceModel.BuildSettings.bRemoveDegenerates = true;
	SourceModel.BuildSettings.bRecomputeNormals = !Data.bHasVertexNormals;
	SourceModel.BuildSettings.bRecomputeTangents = true;
	SourceModel.BuildSettings.bUseMikkTSpace = true;
	SourceModel.SaveRawMesh(RawMesh);

	StaticMesh->GetAssetImportData()->Update(Filename);
	StaticMesh->Build();
	StaticMesh->PostEditChange();
	FAssetCompilingManager::Get().FinishCompilationForObjects({StaticMesh});
	FAssetRegistryModule::AssetCreated(StaticMesh);
	StaticMesh->MarkPackageDirty();
	

	return StaticMesh;
}
