#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "PskFactory.h"
#include "PskxFactory.h"
#include "PskReader.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace PskTests
{
    FString SourceFile()
    {
        FString Result;
        FParse::Value(FCommandLine::Get(), TEXT("PskTestFile="), Result);
        return Result;
    }

    bool SaveAsset(UObject* Asset)
    {
        UPackage* Package = Asset->GetOutermost();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(Package, Asset, *Filename, Args);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPskReaderValidationTest, "UnrealPSKPSA.ReaderValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPskReaderValidationTest::RunTest(const FString& Parameters)
{
    const FString Source = PskTests::SourceFile();
    if (!TestTrue(TEXT("Supply -PskTestFile with an existing PSK"), FPaths::FileExists(Source))) return false;
    const FPskReader Reader(Source);
    if (!TestTrue(TEXT("Source parses"), Reader.bIsValid)) return false;
    TestTrue(TEXT("Source has skeletal data"), !Reader.Bones.IsEmpty() && !Reader.Influences.IsEmpty());

    const FString FixtureDir = FPaths::ProjectSavedDir() / TEXT("UE58Migration/Fixtures");
    IFileManager::Get().MakeDirectory(*FixtureDir, true);
    TArray<uint8> Bytes;
    FFileHelper::LoadFileToArray(Bytes, *Source);
    // Unknown chunks must skip DataSize * DataCount, then resume parsing normally.
    VChunkHeader Unknown{};
    FCStringAnsi::Strcpy(Unknown.ChunkID, "FUTURECHUNK");
    Unknown.DataSize = 3;
    Unknown.DataCount = 2;
    TArray<uint8> Extended;
    Extended.Append(Bytes.GetData(), 32);
    Extended.Append(reinterpret_cast<const uint8*>(&Unknown), 32);
    Extended.AddZeroed(6);
    Extended.Append(Bytes.GetData() + 32, Bytes.Num() - 32);
    const FString ExtendedFile = FixtureDir / TEXT("unknown_chunk_中文.psk");
    FFileHelper::SaveArrayToFile(Extended, *ExtendedFile);
    const FPskReader ExtendedReader(ExtendedFile);
    TestTrue(TEXT("Unknown chunk and Unicode path parse"), ExtendedReader.bIsValid);
    TestEqual(TEXT("Unknown chunk preserves faces"), ExtendedReader.Faces.Num(), Reader.Faces.Num());
    TestEqual(TEXT("All extra UV channels survive"), ExtendedReader.ExtraUVs.Num(), Reader.ExtraUVs.Num());

    Bytes.SetNum(Bytes.Num() - 1);
    const FString TruncatedFile = FixtureDir / TEXT("truncated.psk");
    FFileHelper::SaveArrayToFile(Bytes, *TruncatedFile);
    AddExpectedError(TEXT("Cannot import"), EAutomationExpectedErrorFlags::Contains, 1);
    const FPskReader Truncated(TruncatedFile);
    TestFalse(TEXT("Truncated file is rejected"), Truncated.bIsValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPskImportValidationTest, "UnrealPSKPSA.ImportValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPskImportValidationTest::RunTest(const FString& Parameters)
{
    const FString Source = PskTests::SourceFile();
    if (!TestTrue(TEXT("Supply -PskTestFile with an existing PSK"), FPaths::FileExists(Source))) return false;
    const FPskReader Reader(Source);
    if (!TestTrue(TEXT("Source parses"), Reader.bIsValid)) return false;
    const FString AssetName = FPaths::GetBaseFilename(Source);
    const FString PackageName = TEXT("/Game/PSKValidation/") + AssetName;
    UPackage* Package = CreatePackage(*PackageName);
    bool bCanceled = false;
    UPskFactory* Factory = NewObject<UPskFactory>();
    USkeletalMesh* Mesh = Cast<USkeletalMesh>(Factory->ImportObject(USkeletalMesh::StaticClass(), Package,
        FName(*AssetName), RF_Public | RF_Standalone, Source, nullptr, bCanceled));
    if (!TestNotNull(TEXT("PSK factory creates skeletal mesh"), Mesh)) return false;
    TestFalse(TEXT("Import was not canceled"), bCanceled);
    TestNotNull(TEXT("Skeleton exists"), Mesh->GetSkeleton());
    TestEqual(TEXT("Bone count"), Mesh->GetRefSkeleton().GetRawBoneNum(), Reader.Bones.Num());
    TestEqual(TEXT("Material slot count"), Mesh->GetMaterials().Num(), Reader.Materials.Num());
    TestTrue(TEXT("Persistent mesh description exists"), Mesh->HasMeshDescription(0));
    TestTrue(TEXT("Built LOD exists"), Mesh->GetImportedModel()->LODModels.Num() > 0);
    if (Mesh->GetImportedModel()->LODModels.Num() == 0) return false;
    const FSkeletalMeshLODModel& LOD = Mesh->GetImportedModel()->LODModels[0];
    TestEqual(TEXT("UV channel count"), int32(LOD.NumTexCoords), Reader.ExtraUVs.Num() + 1);
    uint32 Triangles = 0;
    for (const FSkelMeshSection& Section : LOD.Sections) Triangles += Section.NumTriangles;
    TestEqual(TEXT("Triangle count"), int32(Triangles), Reader.Faces.Num());
    TestTrue(TEXT("Vertices built"), LOD.NumVertices > 0);
    TestTrue(TEXT("Save skeletal mesh"), PskTests::SaveAsset(Mesh));
    TestTrue(TEXT("Save skeleton"), PskTests::SaveAsset(Mesh->GetSkeleton()));
    for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
        if (Material.MaterialInterface) TestTrue(TEXT("Save material"), PskTests::SaveAsset(Material.MaterialInterface));

    // ActorX PSKX shares the geometry chunks; derive a static test fixture from the supplied mesh.
    const FString StaticName = AssetName + TEXT("_Static");
    const FString StaticSource = FPaths::ProjectSavedDir() / TEXT("UE58Migration/") + StaticName + TEXT(".pskx");
    TestTrue(TEXT("Create PSKX fixture"), IFileManager::Get().Copy(*StaticSource, *Source, true) == COPY_OK);
    UPskxFactory* StaticFactory = NewObject<UPskxFactory>();
    UPackage* StaticPackage = CreatePackage(*(TEXT("/Game/PSKValidation/") + StaticName));
    UStaticMesh* StaticMesh = Cast<UStaticMesh>(StaticFactory->ImportObject(UStaticMesh::StaticClass(), StaticPackage,
        FName(*StaticName), RF_Public | RF_Standalone, StaticSource, nullptr, bCanceled));
    if (!TestNotNull(TEXT("PSKX factory creates static mesh"), StaticMesh)) return false;
    TestEqual(TEXT("Static material slots"), StaticMesh->GetStaticMaterials().Num(), Reader.Materials.Num());
    if (!TestNotNull(TEXT("Static render data"), StaticMesh->GetRenderData())) return false;
    const FStaticMeshLODResources& StaticLOD = StaticMesh->GetRenderData()->LODResources[0];
    TestEqual(TEXT("Static UV channels"), int32(StaticLOD.GetNumTexCoords()), Reader.ExtraUVs.Num() + 1);
    TestEqual(TEXT("Static triangles"), int32(StaticLOD.GetNumTriangles()), Reader.Faces.Num());
    TestTrue(TEXT("Save static mesh"), PskTests::SaveAsset(StaticMesh));
    AddInfo(FString::Printf(TEXT("Validated %s: %d triangles, %d bones, %d materials, %d UVs"),
        *PackageName, Reader.Faces.Num(), Reader.Bones.Num(), Reader.Materials.Num(), Reader.ExtraUVs.Num() + 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPskReloadValidationTest, "UnrealPSKPSA.ReloadValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPskReloadValidationTest::RunTest(const FString& Parameters)
{
    const FPskReader Reader(PskTests::SourceFile());
    if (!TestTrue(TEXT("Source parses"), Reader.bIsValid)) return false;
    const FString AssetName = FPaths::GetBaseFilename(PskTests::SourceFile());
    const FString Path = TEXT("/Game/PSKValidation/") + AssetName + TEXT(".") + AssetName;
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
    if (!TestNotNull(TEXT("Reload saved skeletal mesh"), Mesh)) return false;
    FAssetCompilingManager::Get().FinishCompilationForObjects({Mesh});
    TestNotNull(TEXT("Reload saved skeleton"), Mesh->GetSkeleton());
    TestTrue(TEXT("Mesh description survives reload"), Mesh->HasMeshDescription(0));
    TestEqual(TEXT("Reload bone count"), Mesh->GetRefSkeleton().GetRawBoneNum(), Reader.Bones.Num());
    TestEqual(TEXT("Reload UV channels"), int32(Mesh->GetImportedModel()->LODModels[0].NumTexCoords), Reader.ExtraUVs.Num() + 1);
    TestEqual(TEXT("Reload material slots"), Mesh->GetMaterials().Num(), Reader.Materials.Num());
    return true;
}
#endif
