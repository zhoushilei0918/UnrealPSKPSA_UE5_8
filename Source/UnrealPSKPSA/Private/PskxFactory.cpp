#include "PskxFactory.h"
#include "PskFactory.h"
#include "UnrealPSKPSA.h"
#include "ActorXImportWindow.h"
#include "Misc/App.h"

#include "PskPsaUtils.h"
#include "PskReader.h"
#include "RawMesh.h"
#include "AssetCompilingManager.h"
#include "EditorFramework/AssetImportData.h"
#include "Materials/MaterialInstanceConstant.h"

bool UPskxFactory::FactoryCanImport(const FString& Filename)
{
    // AssetTools queries this before checking existing asset types. Reset for every
    // file so a mixed static/skeletal batch never inherits the preceding file's type.
    SupportedClass = UStaticMesh::StaticClass();
    if (!FPaths::GetExtension(Filename).Equals(FactoryExtension, ESearchCase::IgnoreCase)) return false;
    const FPskReader Data(Filename);
    if (!Data.bIsValid) return false;
    if (!Data.Bones.IsEmpty() || !Data.Influences.IsEmpty()) SupportedClass = USkeletalMesh::StaticClass();
    return true;
}

UObject* UPskxFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;
    if (!IsAutomatedImport() && !IsRunningCommandlet() && !FApp::IsUnattended() &&
        !ShowActorXImportOptions(Orientation, Filename))
    { bOutOperationCanceled = true; return nullptr; }
    UObject* Result = Import(Filename, InParent, InName, Flags, {}, Orientation);
    if (Result) SupportedClass = Result->GetClass();
    return Result;
}

UObject* UPskxFactory::Import(const FString& Filename, UObject* Parent, const FName Name, const EObjectFlags Flags, TMap<FString, FString> MaterialNameToPathMap, const FActorXOrientation& Orientation)
{
	auto Data = FPskReader(Filename);
	if (!Data.bIsValid) return nullptr;
    const bool bSkeletal = !Data.Bones.IsEmpty() || !Data.Influences.IsEmpty();
    UClass* MeshClass = bSkeletal ? USkeletalMesh::StaticClass() : UStaticMesh::StaticClass();
    Parent->GetOutermost()->FullyLoad();
    if (const UObject* Existing = FindObject<UObject>(Parent->GetOutermost(), *Name.ToString()))
    {
        if (!Existing->IsA(MeshClass))
        {
            UE_LOG(LogUnrealPSKPSA, Error, TEXT("%s"), *FText::Format(NSLOCTEXT("UnrealPSKPSA", "PskxTypeMismatch", "PSKX detected as {0}, but the existing asset type is {1}. Import with a new name or folder."), FText::FromString(MeshClass->GetName()), FText::FromString(Existing->GetClass()->GetName())).ToString());
            return nullptr;
        }
    }
    // PSKX is also used for extended skeletal meshes (e.g. 32-bit face indices).
    // Reuse the full skeletal path, including weights, morphs and saved orientation.
    // Incomplete skeletal data must fail validation instead of silently losing bones.
    if (bSkeletal) return UPskFactory::Import(Filename, Parent, Name, Flags, MoveTemp(MaterialNameToPathMap), Orientation);
	const FQuat4f BasisRotation = Orientation.Rotation();
	
	auto RawMesh = FRawMesh();
	for (auto Vertex : Data.Vertices)
	{
		auto FixedVertex = Vertex;
		FixedVertex.Y = -FixedVertex.Y; // MIRROR_MESH
		FixedVertex = BasisRotation.RotateVector(FixedVertex);
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
			
			RawMesh.WedgeTangentZ.Add(Data.bHasVertexNormals ? BasisRotation.RotateVector(Data.Normals[Data.Normals.Num() == Data.Vertices.Num() ? PskWedge.PointIndex : WedgeIndex] * FVector3f(1, -1, 1)) : FVector3f::ZeroVector);
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

    auto* ImportData = NewObject<UActorXMeshImportData>(StaticMesh);
    ImportData->Orientation = Orientation;
    ImportData->Update(Filename);
    StaticMesh->SetAssetImportData(ImportData);
	StaticMesh->Build();
	StaticMesh->PostEditChange();
	FAssetCompilingManager::Get().FinishCompilationForObjects({StaticMesh});
	FAssetRegistryModule::AssetCreated(StaticMesh);
	StaticMesh->MarkPackageDirty();
	

	return StaticMesh;
}
