#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PskReader.h"
#include "PskxFactory.h"
#include "PskFixtureUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AutomatedAssetImportData.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AssetCompilingManager.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "StaticMeshResources.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace PskxTests
{
    FString Source()
    {
        FString Path; FParse::Value(FCommandLine::Get(), TEXT("PskxTestFile="), Path); return Path;
    }
    const TCHAR* MeshPath = TEXT("/Game/PSKXSkeletalValidation/Mesh_Alet.Mesh_Alet");
    bool Save(UObject* Asset)
    {
        const FString File = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset->GetOutermost(), Asset, *File, Args);
    }
    void CheckMesh(FAutomationTestBase& Test, USkeletalMesh* Mesh, const FPskReader& Reader, const FActorXOrientation& Orientation = FActorXOrientation())
    {
        if (!Test.TestNotNull(TEXT("Skeletal PSKX creates skeletal mesh"), Mesh)) return;
        FAssetCompilingManager::Get().FinishCompilationForObjects({Mesh});
        Test.TestNotNull(TEXT("Skeleton asset"), Mesh->GetSkeleton());
        const auto& Ref = Mesh->GetRefSkeleton();
        if (!Test.TestEqual(TEXT("All bones retained"), Ref.GetRawBoneNum(), Reader.Bones.Num())) return;
        const FQuat4f Basis = Orientation.Rotation();
        bool bBonesOK = true;
        for (int32 Bone = 0; Bone < Reader.Bones.Num(); ++Bone)
        {
            const auto& SourceBone = Reader.Bones[Bone];
            FVector3f Position = SourceBone.BonePos.Position * FVector3f(1, -1, 1);
            const auto& Q = SourceBone.BonePos.Orientation;
            FQuat4f Rotation = FQuat4f(Q.X, -Q.Y, Q.Z, Q.W).GetNormalized();
            if (Bone == 0) { Position = Basis.RotateVector(Position); Rotation.W *= -1.0f; Rotation = Basis * Rotation; }
            bBonesOK &= Ref.GetBoneName(Bone) == FName(UTF8_TO_TCHAR(SourceBone.Name)) && Ref.GetParentIndex(Bone) == SourceBone.ParentIndex;
            bBonesOK &= Ref.GetRefBonePose()[Bone].GetTranslation().Equals(FVector(Position), 0.001) && Ref.GetRefBonePose()[Bone].GetRotation().Equals(FQuat(Rotation), 0.0001);
        }
        Test.TestTrue(TEXT("Bone names hierarchy and oriented bind transforms preserved"), bBonesOK);
        const auto* ImportData = Cast<UActorXMeshImportData>(Mesh->GetAssetImportData());
        Test.TestTrue(TEXT("PSKX orientation metadata retained for future PSA"), ImportData && ImportData->Orientation.TargetForward == Orientation.TargetForward);
        Test.TestEqual(TEXT("Material slots"), Mesh->GetMaterials().Num(), Reader.Materials.Num());
        if (!Test.TestTrue(TEXT("Built skeletal LOD"), Mesh->GetImportedModel()->LODModels.Num() > 0)) return;
        const auto& LOD = Mesh->GetImportedModel()->LODModels[0];
        int32 Triangles = 0;
        for (const auto& Section : LOD.Sections) Triangles += Section.NumTriangles;
        Test.TestEqual(TEXT("Wide-index triangles retained"), Triangles, Reader.Faces.Num());
        Test.TestEqual(TEXT("All UV channels retained"), int32(LOD.NumTexCoords), Reader.ExtraUVs.Num() + 1);
        const auto* Description = Mesh->GetMeshDescription(0);
        if (!Test.TestNotNull(TEXT("Persistent skeletal mesh description"), Description)) return;
        const FSkeletalMeshConstAttributes Attributes(*Description);
        const auto Positions = Attributes.GetVertexPositions();
        const auto Weights = Attributes.GetVertexSkinWeights();
        Test.TestEqual(TEXT("All points including indices above 65535 retained"), Description->Vertices().Num(), Reader.Vertices.Num());
        TArray<TMap<int32, float>> ExpectedWeights; ExpectedWeights.SetNum(Reader.Vertices.Num());
        for (const auto& Weight : Reader.Influences) ExpectedWeights[Weight.PointIdx].FindOrAdd(Weight.BoneIdx) += Weight.Weight;
        bool bPointsOK = true, bWeightsOK = true;
        for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
        {
            if (!Reader.Vertices.IsValidIndex(Vertex.GetValue())) { bPointsOK = false; continue; }
            bPointsOK &= Positions[Vertex].Equals(Basis.RotateVector(Reader.Vertices[Vertex.GetValue()] * FVector3f(1, -1, 1)), 0.002f);
            float Sum = 0, SourceSum = 0;
            const auto& Expected = ExpectedWeights[Vertex.GetValue()];
            for (const auto& Pair : Expected) SourceSum += Pair.Value;
            for (const auto Weight : Weights.Get(Vertex))
            {
                Sum += Weight.GetWeight();
                const float* SourceWeight = Expected.Find(Weight.GetBoneIndex());
                bWeightsOK &= SourceWeight && SourceSum > 0 && FMath::IsNearlyEqual(Weight.GetWeight(), *SourceWeight / SourceSum, 0.001f);
            }
            bWeightsOK &= FMath::IsNearlyEqual(Sum, 1.0f, 0.001f);
        }
        Test.TestTrue(TEXT("Mesh and skeleton share orientation"), bPointsOK);
        Test.TestTrue(TEXT("Normalized source skin weights preserved on every point"), bWeightsOK);

        // Independent spatial oracle: named leg bones must sit next to the surface
        // that their actual imported weights move, not on the opposite leg. Comparing
        // import arithmetic alone cannot catch a wrong source-root convention.
        TArray<FTransform> ComponentPose = Ref.GetRefBonePose();
        for (int32 Bone = 1; Bone < ComponentPose.Num(); ++Bone)
            ComponentPose[Bone] *= ComponentPose[Ref.GetParentIndex(Bone)];
        for (const TCHAR* Part : {TEXT("Thigh_twistX3"), TEXT("Foot")})
        {
            const int32 Left = Ref.FindBoneIndex(FName(*(FString(TEXT("L_")) + Part)));
            const int32 Right = Ref.FindBoneIndex(FName(*(FString(TEXT("R_")) + Part)));
            if (!Test.TestTrue(TEXT("Bilateral leg probe bones exist"), Left != INDEX_NONE && Right != INDEX_NONE)) continue;
            for (const int32 Bone : {Left, Right})
            {
                FVector Centroid = FVector::ZeroVector; double TotalWeight = 0;
                for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
                    for (const auto Weight : Weights.Get(Vertex))
                        if (Weight.GetBoneIndex() == Bone)
                        {
                            Centroid += FVector(Positions[Vertex]) * Weight.GetWeight();
                            TotalWeight += Weight.GetWeight();
                        }
                if (!Test.TestTrue(TEXT("Leg probe has weighted surface"), TotalWeight > 0)) continue;
                Centroid /= TotalWeight;
                const double OwnDistance = FVector::Distance(Centroid, ComponentPose[Bone].GetTranslation());
                const double OppositeDistance = FVector::Distance(Centroid, ComponentPose[Bone == Left ? Right : Left].GetTranslation());
                Test.TestTrue(*FString::Printf(TEXT("%s (%s): weighted surface near its own bone (%.2f cm)"), *Ref.GetBoneName(Bone).ToString(), *Orientation.Description(), OwnDistance), OwnDistance < 12.0);
                Test.TestTrue(TEXT("Weighted leg surface closer to own bone than opposite leg"), OwnDistance < OppositeDistance);
            }
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPskxSkeletalImportTest, "UnrealPSKPSA.PSKX.Import", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPskxSkeletalImportTest::RunTest(const FString& Parameters)
{
    const FString Source = PskxTests::Source();
    if (!TestTrue(TEXT("PSKX sample exists"), FPaths::FileExists(Source))) return false;
    const FPskReader Reader(Source);
    if (!TestTrue(*Reader.ErrorMessage, Reader.bIsValid)) return false;
    TestEqual(TEXT("Alet sample bones"), Reader.Bones.Num(), 396);
    TestTrue(TEXT("Sample needs 32-bit point indices"), Reader.Vertices.Num() > 65536);
    const FString FixtureFolder = FPaths::ProjectSavedDir() / TEXT("PSKXValidation");
    IFileManager::Get().MakeDirectory(*FixtureFolder, true);
    const FString StaticFile = FixtureFolder / TEXT("Alet_Static.pskx");
    if (!TestTrue(TEXT("Build true static fixture"), PskFixtureUtils::WriteStatic(Source, StaticFile))) return false;
    UPskxFactory* Factory = NewObject<UPskxFactory>();
    TestTrue(TEXT("Skeletal PSKX accepted"), Factory->FactoryCanImport(Source));
    TestEqual(TEXT("AssetTools sees skeletal type before import"), Factory->ResolveSupportedClass(), USkeletalMesh::StaticClass());
    TestTrue(TEXT("Static PSKX accepted by same factory"), Factory->FactoryCanImport(StaticFile));
    TestEqual(TEXT("Mixed batch resets static type"), Factory->ResolveSupportedClass(), UStaticMesh::StaticClass());
    TestTrue(TEXT("Switch back to skeletal type"), Factory->FactoryCanImport(Source));
    TestEqual(TEXT("Mixed batch resets skeletal type"), Factory->ResolveSupportedClass(), USkeletalMesh::StaticClass());
    // Exercise editor AssetTools routing, not only the static Import helper.
    auto* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->Filenames = { Source, StaticFile };
    ImportData->DestinationPath = TEXT("/Game/PSKXSkeletalValidation");
    ImportData->bReplaceExisting = true;
    ImportData->Factory = Factory;
    const auto Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetsAutomated(ImportData);
    if (!TestEqual(TEXT("Both mesh types imported in one batch"), Assets.Num(), 2)) return false;
    USkeletalMesh* Mesh = Cast<USkeletalMesh>(Assets[0]);
    PskxTests::CheckMesh(*this, Mesh, Reader);
    if (!Mesh || !Mesh->GetSkeleton()) return false;
    for (const auto Axis : {EActorXForwardAxis::PositiveX, EActorXForwardAxis::NegativeX, EActorXForwardAxis::NegativeY})
    {
        FActorXOrientation Orientation; Orientation.TargetForward = Axis;
        const FString Name = FString::Printf(TEXT("Alet_Direction%d"), int32(Axis));
        auto* RotatedMesh = Cast<USkeletalMesh>(UPskxFactory::Import(Source, CreatePackage(*(FString(TEXT("/Game/PSKXSkeletalValidation/")) + Name)), FName(*Name), RF_Public | RF_Standalone, {}, Orientation));
        PskxTests::CheckMesh(*this, RotatedMesh, Reader, Orientation);
    }
    UStaticMesh* Static = Cast<UStaticMesh>(Assets[1]);
    if (!TestNotNull(TEXT("Bone-free PSKX remains static"), Static)) return false;
    int32 ExpectedStaticTriangles = 0;
    for (const auto& Face : Reader.Faces)
    {
        FVector3f Points[3];
        for (int32 Corner = 0; Corner < 3; ++Corner)
            Points[Corner] = FActorXOrientation().Rotation().RotateVector(Reader.Vertices[Reader.Wedges[Face.WedgeIndex[Corner]].PointIndex] * FVector3f(1, -1, 1));
        // Static build settings deliberately remove triangles with coincident corners.
        if (!Points[0].Equals(Points[1], THRESH_POINTS_ARE_SAME) && !Points[0].Equals(Points[2], THRESH_POINTS_ARE_SAME) && !Points[1].Equals(Points[2], THRESH_POINTS_ARE_SAME))
            ++ExpectedStaticTriangles;
    }
    TestEqual(TEXT("Static geometry retained except configured degenerate removal"), int32(Static->GetRenderData()->LODResources[0].GetNumTriangles()), ExpectedStaticTriangles);
    TestTrue(TEXT("Save skeletal mesh"), PskxTests::Save(Mesh));
    TestTrue(TEXT("Save skeleton"), PskxTests::Save(Mesh->GetSkeleton()));
    TestTrue(TEXT("Save static mesh"), PskxTests::Save(Static));
    for (const auto& Material : Mesh->GetMaterials()) if (Material.MaterialInterface) TestTrue(TEXT("Save material"), PskxTests::Save(Material.MaterialInterface));
    // A previous static asset must never be silently replaced through the direct API.
    AddExpectedError(TEXT("同名资产类型"), EAutomationExpectedErrorFlags::Contains, 1);
    TestNull(TEXT("Different existing asset type protected"), UPskxFactory::Import(Source, Static->GetOutermost(), Static->GetFName(), RF_Public | RF_Standalone, {}));
    const FString IncompleteFile = FixtureFolder / TEXT("MissingWeights.pskx");
    TestTrue(TEXT("Build incomplete skeleton fixture"), PskFixtureUtils::WriteWithoutChunks(Source, IncompleteFile, {TEXT("RAWWEIGHTS")}));
    AddExpectedError(TEXT("骨骼网格必须同时包含骨骼和蒙皮权重"), EAutomationExpectedErrorFlags::Contains, 1);
    TestNull(TEXT("Missing weights cannot silently create static mesh"), UPskxFactory::Import(IncompleteFile, CreatePackage(TEXT("/Game/PSKXSkeletalValidation/MissingWeights")), TEXT("MissingWeights"), RF_Public | RF_Standalone, {}));
    AddInfo(FString::Printf(TEXT("PSKX imported: %d bones, %d points, %d triangles, %d source skin weights."), Reader.Bones.Num(), Reader.Vertices.Num(), Reader.Faces.Num(), Reader.Influences.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPskxSkeletalReloadTest, "UnrealPSKPSA.PSKX.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPskxSkeletalReloadTest::RunTest(const FString& Parameters)
{
    const FPskReader Reader(PskxTests::Source());
    if (!TestTrue(TEXT("Source valid"), Reader.bIsValid)) return false;
    PskxTests::CheckMesh(*this, LoadObject<USkeletalMesh>(nullptr, PskxTests::MeshPath), Reader);
    return true;
}
#endif
