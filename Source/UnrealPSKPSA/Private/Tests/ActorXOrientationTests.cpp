#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ActorXImportSettings.h"
#include "PskFactory.h"
#include "PskxFactory.h"
#include "PskReader.h"
#include "PsaImporter.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "AssetCompilingManager.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"

namespace ActorXOrientationTests
{
    const TCHAR* Root = TEXT("/Game/PSAOrientationValidation");
    FString Source(const TCHAR* Key)
    {
        FString Value; FParse::Value(FCommandLine::Get(), Key, Value); return Value;
    }
    bool Save(UObject* Object)
    {
        const FString Path = FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Object->GetOutermost(), Object, *Path, Args);
    }
    TArray<FTransform> ComponentPose(const USkeletalMesh* Mesh, UAnimSequence* Sequence = nullptr, double Time = 0)
    {
        const auto& Ref = Mesh->GetRefSkeleton();
        TArray<FTransform> Pose = Ref.GetRefBonePose();
        TArray<FName> Tracks;
        if (Sequence) Sequence->GetDataModel()->GetBoneTrackNames(Tracks);
        for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
        {
            if (Sequence && Tracks.Contains(Ref.GetBoneName(Bone)))
                Sequence->GetBoneTransform(Pose[Bone], FSkeletonPoseBoneIndex(Mesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(Bone))), FAnimExtractContext(Time, false), false);
            const int32 Parent = Ref.GetParentIndex(Bone);
            if (Parent != INDEX_NONE) Pose[Bone] *= Pose[Parent];
        }
        return Pose;
    }
    void AppendChunk(TArray<uint8>& Bytes, const ANSICHAR* Name, int32 Size, int32 Count, const void* Data)
    {
        VChunkHeader Header{}; FCStringAnsi::Strncpy(Header.ChunkID, Name, 20);
        Header.TypeFlag = 1999801; Header.DataSize = Size; Header.DataCount = Count;
        Bytes.Append(reinterpret_cast<const uint8*>(&Header), 32);
        Bytes.Append(reinterpret_cast<const uint8*>(Data), Size * Count);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorXOrientationTest, "UnrealPSKPSA.Orientation.Import", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActorXOrientationTest::RunTest(const FString& Parameters)
{
    using namespace ActorXOrientationTests;
    const FString Psk = Source(TEXT("PskTestFile="));
    const FString Psa = Source(TEXT("PsaTestDir=")) / TEXT("girl023_wp03a_base_run_loop.psa");
    if (!TestTrue(TEXT("Source PSK exists"), FPaths::FileExists(Psk)) || !TestTrue(TEXT("Source PSA exists"), FPaths::FileExists(Psa))) return false;
    FPskReader Reader(Psk);
    if (!TestTrue(TEXT("Source parses"), Reader.bIsValid)) return false;
    // Add explicit non-axis-aligned normals and a morph delta to a private fixture copy.
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes, *Psk);
    TArray<FVector3f> Normals; Normals.Init(FVector3f(1, -2, 3).GetSafeNormal(), Reader.Vertices.Num());
    AppendChunk(Bytes, "VTXNORMS", 12, Normals.Num(), Normals.GetData());
    VMorphInfo Morph{}; FCStringAnsi::Strcpy(Morph.Name, "OrientationProbe"); Morph.VertexCount = 1;
    VMorphData Delta{}; Delta.PointIdx = 0; Delta.PositionDelta = FVector3f(2, 3, 5);
    AppendChunk(Bytes, "MRPHINFO", 68, 1, &Morph);
    AppendChunk(Bytes, "MRPHDATA", 28, 1, &Delta);
    const FString Fixture = FPaths::ProjectSavedDir() / TEXT("PSAImport/orientation.psk");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Fixture), true);
    if (!TestTrue(TEXT("Write fixture"), FFileHelper::SaveArrayToFile(Bytes, *Fixture))) return false;

    auto ImportMesh = [&](const FString& Name, const FActorXOrientation& Orientation)
    {
        UPackage* Package = CreatePackage(*(FString(Root) / Name / Name));
        return Cast<USkeletalMesh>(UPskFactory::Import(Fixture, Package, FName(*Name), RF_Public | RF_Standalone, {}, Orientation));
    };
    auto ImportAnimation = [&](USkeletalMesh* Mesh, const FString& Folder)
    {
        FPsaImportOptions Options; Options.bReplaceExisting = true;
        FString Summary, Error; TArray<UAnimSequence*> Assets;
        if (!FPsaImporter::ImportFile(Psa, Mesh, Folder, Options, Assets, Summary, Error)) { AddError(Error); return (UAnimSequence*)nullptr; }
        return Assets.IsEmpty() ? nullptr : Assets[0];
    };
    USkeletalMesh* Base = ImportMesh(TEXT("Base"), FActorXOrientation::Unchanged());
    if (!TestNotNull(TEXT("Baseline mesh"), Base)) return false;
    TestTrue(TEXT("Save baseline mesh"), Save(Base));
    TestTrue(TEXT("Save baseline skeleton"), Save(Base->GetSkeleton()));
    for (const auto& Mat : Base->GetMaterials()) if (Mat.MaterialInterface) Save(Mat.MaterialInterface);
    UAnimSequence* BaseAnimation = ImportAnimation(Base, FString(Root) / TEXT("Base/Animations"));
    if (!TestNotNull(TEXT("Baseline animation"), BaseAnimation)) return false;
    const auto BaseRef = ComponentPose(Base);
    const FSkeletalMeshConstAttributes BaseAttributes(*Base->GetMeshDescription(0));
    const auto BasePoints = BaseAttributes.GetVertexPositions();
    const auto BaseNormals = BaseAttributes.GetVertexInstanceNormals();
    const auto BaseMorph = BaseAttributes.GetVertexMorphPositionDelta(TEXT("OrientationProbe"));
    if (!TestTrue(TEXT("Morph data present"), BaseMorph.IsValid())) return false;

    for (int32 Axis = 0; Axis < 4; ++Axis)
    {
        FActorXOrientation Orientation; Orientation.TargetForward = EActorXForwardAxis(Axis);
        const FQuat Q(Orientation.Rotation());
        const FTransform Basis(Q);
        const FVector ExpectedForward[] = {FVector(1,0,0), FVector(0,1,0), FVector(-1,0,0), FVector(0,-1,0)};
        TestTrue(TEXT("Requested UE direction"), Q.RotateVector(FVector::ForwardVector).Equals(ExpectedForward[Axis], 0.00001));
        USkeletalMesh* Mesh = ImportMesh(FString::Printf(TEXT("Direction%d"), Axis), Orientation);
        if (!TestNotNull(TEXT("Oriented mesh"), Mesh)) return false;
        const auto Ref = ComponentPose(Mesh);
        const FSkeletalMeshConstAttributes Attributes(*Mesh->GetMeshDescription(0));
        const auto Points = Attributes.GetVertexPositions();
        const auto VertexNormals = Attributes.GetVertexInstanceNormals();
        const auto Morphs = Attributes.GetVertexMorphPositionDelta(TEXT("OrientationProbe"));
        for (const FVertexID Vertex : Mesh->GetMeshDescription(0)->Vertices().GetElementIDs())
        {
            TestTrue(TEXT("All mesh positions rotate"), FVector(Points[Vertex]).Equals(Q.RotateVector(FVector(BasePoints[Vertex])), 0.002));
            TestTrue(TEXT("Morph delta rotates in the same basis"), FVector(Morphs[Vertex]).Equals(Q.RotateVector(FVector(BaseMorph[Vertex])), 0.002));
        }
        for (const FVertexInstanceID Vertex : Mesh->GetMeshDescription(0)->VertexInstances().GetElementIDs())
            TestTrue(TEXT("Imported normals rotate"), FVector(VertexNormals[Vertex]).Equals(Q.RotateVector(FVector(BaseNormals[Vertex])), 0.0001));
        for (int32 Bone = 0; Bone < Ref.Num(); ++Bone)
        {
            TestTrue(TEXT("Whole reference skeleton rotates once"), Ref[Bone].Equals(BaseRef[Bone] * Basis, 0.002));
            if (Bone > 0) TestTrue(TEXT("Child local bind transform unchanged"), Mesh->GetRefSkeleton().GetRefBonePose()[Bone].Equals(Base->GetRefSkeleton().GetRefBonePose()[Bone], 0.0001));
        }
        TestTrue(TEXT("Save oriented mesh"), Save(Mesh));
        TestTrue(TEXT("Save oriented skeleton"), Save(Mesh->GetSkeleton()));
        for (const auto& Mat : Mesh->GetMaterials()) if (Mat.MaterialInterface) Save(Mat.MaterialInterface);
        UAnimSequence* Animation = ImportAnimation(Mesh, FString(Root) / FString::Printf(TEXT("Direction%d/Animations"), Axis));
        if (!TestNotNull(TEXT("Oriented animation"), Animation)) return false;
        for (double Fraction : {0.0, 0.37, 1.0})
        {
            const auto BasePose = ComponentPose(Base, BaseAnimation, BaseAnimation->GetPlayLength() * Fraction);
            const auto Pose = ComponentPose(Mesh, Animation, Animation->GetPlayLength() * Fraction);
            for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
                TestTrue(TEXT("Compressed animation follows whole mesh basis"), Pose[Bone].Equals(BasePose[Bone] * Basis, 0.02));
            // Independently skin source points using their original weights. Rotation must
            // commute with skinning, including vertices influenced by several bones.
            for (int32 Point = 0; Point < Reader.Vertices.Num(); Point += 499)
            {
                const FVector V(Reader.Vertices[Point] * FVector3f(1,-1,1));
                FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
                for (const auto& Weight : Reader.Influences)
                    if (Weight.PointIdx == Point)
                    {
                        A += BasePose[Weight.BoneIdx].TransformPosition(BaseRef[Weight.BoneIdx].InverseTransformPosition(V)) * Weight.Weight;
                        B += Pose[Weight.BoneIdx].TransformPosition(Ref[Weight.BoneIdx].InverseTransformPosition(Q.RotateVector(V))) * Weight.Weight;
                    }
                TestTrue(TEXT("Skinned surface matches rotated animation"), B.Equals(Q.RotateVector(A), 0.02));
            }
        }
        // PSKX body accessories use exactly the same basis.
        const FString StaticName = FString::Printf(TEXT("Static%d"), Axis);
        UStaticMesh* Static = Cast<UStaticMesh>(UPskxFactory::Import(Fixture, CreatePackage(*(FString(Root) / StaticName)), FName(*StaticName), RF_Public | RF_Standalone, {}, Orientation));
        if (!TestNotNull(TEXT("Static accessory"), Static)) return false;
        const auto StaticPositions = FStaticMeshConstAttributes(*Static->GetMeshDescription(0)).GetVertexPositions();
        // Static mesh construction may reorder/weld vertices; compare geometry, not IDs.
        TSet<FVector3f> MeshPointSet;
        for (const FVertexID Vertex : Mesh->GetMeshDescription(0)->Vertices().GetElementIDs()) MeshPointSet.Add(Points[Vertex]);
        bool bAligned = true;
        for (const FVertexID Vertex : Static->GetMeshDescription(0)->Vertices().GetElementIDs())
            bAligned &= MeshPointSet.Contains(StaticPositions[Vertex]);
        TestTrue(TEXT("Static and skeletal pieces align"), bAligned);
    }
    TestEqual(TEXT("Default target is +Y"), FActorXOrientation().TargetForward, EActorXForwardAxis::PositiveY);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorXOrientationReloadTest, "UnrealPSKPSA.Orientation.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActorXOrientationReloadTest::RunTest(const FString& Parameters)
{
    using namespace ActorXOrientationTests;
    for (int32 Axis = 0; Axis < 4; ++Axis)
    {
        const FString Name = FString::Printf(TEXT("Direction%d"), Axis);
        const FString Path = FString(Root) / Name / Name + TEXT(".") + Name;
        USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
        if (!TestNotNull(TEXT("Reload oriented mesh"), Mesh)) return false;
        const auto* Data = Cast<UActorXMeshImportData>(Mesh->GetAssetImportData());
        if (!TestNotNull(TEXT("Orientation metadata survives restart"), Data)) return false;
        TestEqual(TEXT("Saved direction"), Data->Orientation.TargetForward, EActorXForwardAxis(Axis));
        const FString Folder = FString(Root) / Name / TEXT("Animations");
        UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, *(Folder / TEXT("girl023_wp03a_base_run_loop.girl023_wp03a_base_run_loop")));
        if (!TestNotNull(TEXT("Reload oriented animation"), Animation)) return false;
        FAssetCompilingManager::Get().FinishCompilationForObjects({Animation});
        TestEqual(TEXT("Skeleton link preserved"), Animation->GetSkeleton(), Mesh->GetSkeleton());
        // Import again after reload to prove PSA obtains its rotation from the persisted mesh.
        FPsaImportOptions Options; Options.bReplaceExisting = true; Options.bSaveAssets = false;
        FString Summary, Error; TArray<UAnimSequence*> Imported;
        const auto Before = ComponentPose(Mesh, Animation, Animation->GetPlayLength() * 0.37);
        TArray<FName> TrackNames;
        Animation->GetDataModel()->GetBoneTrackNames(TrackNames);
        const int32 KeyCount = Animation->GetDataModel()->GetNumberOfKeys();
        TArray<FTransform> RawBefore;
        for (FName Track : TrackNames)
            for (int32 Key = 0; Key < KeyCount; ++Key)
                RawBefore.Add(Animation->GetDataModel()->GetBoneTrackTransform(Track, FFrameNumber(Key)));
        if (!TestTrue(TEXT("PSA orientation after reload"), FPsaImporter::ImportFile(Source(TEXT("PsaTestDir=")) / TEXT("girl023_wp03a_base_run_loop.psa"), Mesh, Folder, Options, Imported, Summary, Error))) { AddError(Error); return false; }
        const auto After = ComponentPose(Mesh, Animation, Animation->GetPlayLength() * 0.37);
        int32 RawIndex = 0;
        bool bRawIdentical = true;
        for (FName Track : TrackNames)
            for (int32 Key = 0; Key < KeyCount; ++Key)
                bRawIdentical &= RawBefore[RawIndex++].Equals(Animation->GetDataModel()->GetBoneTrackTransform(Track, FFrameNumber(Key)), 0.00001);
        TestTrue(TEXT("All raw keys identical after reimport; no accumulated rotation"), bRawIdentical);
        double MaxTranslationError = 0, MaxRotationError = 0;
        for (int32 Bone = 0; Bone < Before.Num(); ++Bone)
        {
            MaxTranslationError = FMath::Max(MaxTranslationError, FVector::Distance(Before[Bone].GetTranslation(), After[Bone].GetTranslation()));
            MaxRotationError = FMath::Max(MaxRotationError, Before[Bone].GetRotation().AngularDistance(After[Bone].GetRotation()));
        }
        // Compressed interpolation uses floats and can differ slightly after recompression.
        TestTrue(*FString::Printf(TEXT("Reimport direction %d translation drift %.8f"), Axis, MaxTranslationError), MaxTranslationError < 0.01);
        TestTrue(*FString::Printf(TEXT("Reimport direction %d rotation drift %.8f"), Axis, MaxRotationError), MaxRotationError < 0.002);
    }
    return true;
}
#endif
