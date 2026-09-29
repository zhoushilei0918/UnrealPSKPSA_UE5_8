#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PsaReader.h"
#include "PsaImporter.h"
#include "PskxFactory.h"
#include "PsaImportSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UEViewerTests
{
    FString Root()
    {
        FString Path; FParse::Value(FCommandLine::Get(), TEXT("CeliaTestDir="), Path); return Path;
    }
    const TCHAR* MeshPath = TEXT("/Game/PSACeliaValidation/MeshCelia.MeshCelia");
    bool Save(UObject* Object)
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Object->GetOutermost(), Object, *Filename, Args);
    }
    TArray<FString> SampleFiles()
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *(Root() / TEXT("Anim/Idle")), TEXT("*.psa"), true, false);
        for (const TCHAR* Name : {TEXT("ALS_N_Walk_F"), TEXT("ALS_N_Run_F"), TEXT("ALS_CLF_Walk_F"), TEXT("ALS_CLF_Pose"), TEXT("ALS_N_JumpWalk_RF")})
            Files.Add(Root() / TEXT("Locomotion/Sequences") / (FString(Name) + TEXT(".psa")));
        Files.Sort(); return Files;
    }
    void CheckPlayback(FAutomationTestBase& Test, USkeletalMesh* Mesh, UAnimSequence* Anim, const FPsaReader& Reader, const FString& Source)
    {
        const auto& Info = Reader.Sequences[0];
        auto* Model = Anim->GetDataModel();
        Test.TestEqual(TEXT("Celia sample frame count"), Model->GetNumberOfKeys(), Info.NumRawFrames);
        Test.TestEqual(TEXT("Celia target skeleton"), Anim->GetSkeleton(), Mesh->GetSkeleton());
        Test.TestTrue(TEXT("Celia duration retains all exported frame intervals"), FMath::IsNearlyEqual(Anim->GetPlayLength(), double(Info.NumRawFrames) / Info.AnimRate, 0.0002));
        // The separately exported original asset properties provide an independent timing oracle.
        FString Props;
        if (FFileHelper::LoadFileToString(Props, *FPaths::ChangeExtension(Source, TEXT("props.txt"))))
        {
            float Length = 0, RateScale = 1;
            if (FParse::Value(*Props, TEXT("SequenceLength = "), Length))
            {
                FParse::Value(*Props, TEXT("RateScale = "), RateScale);
                Test.TestTrue(TEXT("Duration matches original UE asset metadata"), FMath::IsNearlyEqual(Anim->GetPlayLength(), double(Length) / FMath::Max(1.0f, RateScale), 0.001));
            }
        }
        TArray<FPsaBoneMapping> Mapping; FString Error;
        if (!Test.TestTrue(TEXT("Celia names match"), FPsaImporter::MatchBones(Reader, Mesh, Mapping, Error))) return;
        const auto& Ref = Mesh->GetRefSkeleton();
        const FQuat4f Basis = GetActorXMeshOrientation(Mesh).Rotation();
        bool bRawOK = true, bCompressedOK = true;
        for (const auto& Bone : Mapping)
            for (const int32 Frame : {0, Info.NumRawFrames / 2, Info.NumRawFrames - 1})
            {
                const int32 Index = (Info.FirstRawFrame + Frame) * Info.TotalBones + Bone.SourceIndex;
                const auto& Key = Reader.Keys[Index];
                FVector3f P(Key.Position.X, -Key.Position.Y, Key.Position.Z);
                FQuat4f Q(Key.Rotation.X, -Key.Rotation.Y, Key.Rotation.Z, Key.Rotation.W);
                if (Bone.SourceIndex == 0) { Q.W *= -1; Q = Basis * Q; P = Basis.RotateVector(P); }
                const FVector3f Scale = Reader.ScaleKeys.IsEmpty() ? FVector3f::OneVector : Reader.ScaleKeys[Index].Scale;
                const FTransform Raw = Model->GetBoneTrackTransform(Ref.GetBoneName(Bone.TargetIndex), FFrameNumber(Frame));
                bRawOK &= Raw.GetTranslation().Equals(FVector(P), 0.001) && Raw.GetRotation().Equals(FQuat(Q), 0.001) && Raw.GetScale3D().Equals(FVector(Scale), 0.001);
                FTransform Evaluated;
                Anim->GetBoneTransform(Evaluated, FSkeletonPoseBoneIndex(Bone.TargetIndex), FAnimExtractContext(Frame / Model->GetFrameRate().AsDecimal(), false), false);
                bCompressedOK &= !Evaluated.ContainsNaN() && Evaluated.GetTranslation().Equals(Raw.GetTranslation(), 0.1) && Evaluated.GetRotation().AngularDistance(Raw.GetRotation()) < 0.01 && Evaluated.GetScale3D().Equals(Raw.GetScale3D(), 0.001);
            }
        Test.TestTrue(*(FPaths::GetBaseFilename(Source) + TEXT(" raw transforms")), bRawOK);
        Test.TestTrue(*(FPaths::GetBaseFilename(Source) + TEXT(" compressed playback")), bCompressedOK);
        // A physical pose oracle catches the old ActorX branch twisting child rotations.
        if (Info.Name == TEXT("Celia_Idle_breathing"))
        {
            TArray<FTransform> Pose; Pose.SetNum(Ref.GetRawBoneNum());
            for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
            {
                Pose[Bone] = Model->GetBoneTrackTransform(Ref.GetBoneName(Bone), FFrameNumber(0));
                if (Bone) Pose[Bone] *= Pose[Ref.GetParentIndex(Bone)];
            }
            const FVector Hip = Pose[Ref.FindBoneIndex(TEXT("M_Hips"))].GetTranslation();
            const FVector Head = Pose[Ref.FindBoneIndex(TEXT("M_Head"))].GetTranslation();
            const FVector Left = Pose[Ref.FindBoneIndex(TEXT("L_Foot"))].GetTranslation();
            const FVector Right = Pose[Ref.FindBoneIndex(TEXT("R_Foot"))].GetTranslation();
            Test.TestTrue(TEXT("Standing breathing pose has head above hips"), Head.Z - Hip.Z > 40 && Head.Z - Hip.Z < 80);
            Test.TestTrue(TEXT("Both feet stay near the ground"), FMath::Abs(Left.Z) < 15 && FMath::Abs(Right.Z) < 15);
            Test.TestTrue(TEXT("Legs remain on opposite sides under +Y orientation"), (Left.Y - Hip.Y) * (Right.Y - Hip.Y) < 0);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEViewerImportTest, "UnrealPSKPSA.UEViewer.Import", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEViewerImportTest::RunTest(const FString& Parameters)
{
    using namespace UEViewerTests;
    TestTrue(TEXT("Panel defaults to automatic per-file source detection"), GetDefault<UPsaImportSettings>()->Source == EPsaSource::Auto);
    const FString MeshFile = Root() / TEXT("Body/MeshCelia.pskx");
    if (!TestTrue(TEXT("Celia body exists"), FPaths::FileExists(MeshFile))) return false;
    auto* Mesh = Cast<USkeletalMesh>(UPskxFactory::Import(MeshFile, CreatePackage(TEXT("/Game/PSACeliaValidation/MeshCelia")), TEXT("MeshCelia"), RF_Public | RF_Standalone, {}));
    if (!TestNotNull(TEXT("Celia body imported"), Mesh)) return false;
    TestEqual(TEXT("Celia model bones"), Mesh->GetRefSkeleton().GetRawBoneNum(), 517);
    TestTrue(TEXT("Save body"), Save(Mesh)); TestTrue(TEXT("Save skeleton"), Save(Mesh->GetSkeleton()));
    for (const auto& Material : Mesh->GetMaterials()) if (Material.MaterialInterface) Save(Material.MaterialInterface);
    TArray<FString> AllFiles;
    IFileManager::Get().FindFilesRecursive(AllFiles, *(Root() / TEXT("Anim")), TEXT("*.psa"), true, false);
    TArray<FString> LocomotionFiles;
    IFileManager::Get().FindFilesRecursive(LocomotionFiles, *(Root() / TEXT("Locomotion")), TEXT("*.psa"), true, false);
    AllFiles.Append(LocomotionFiles);
    int32 FullBodyFiles = 0, OtherFiles = 0;
    for (const auto& File : AllFiles)
    {
        const FPsaReader Reader(File);
        if (!TestTrue(*(FPaths::GetCleanFilename(File) + TEXT(": ") + Reader.Error), Reader.bIsValid)) continue;
        if (Reader.Bones.Num() != 643) { ++OtherFiles; continue; }
        TestTrue(TEXT("UEViewer placeholder detected"), Reader.bHasUEViewerBoneMetadata);
        TArray<FPsaBoneMapping> Mapping; FString Error;
        if (TestTrue(*Error, FPsaImporter::MatchBones(Reader, Mesh, Mapping, Error)))
            TestEqual(TEXT("All Celia bones map despite missing source hierarchy"), Mapping.Num(), 517);
        ++FullBodyFiles;
    }
    TestEqual(TEXT("All PSA files parsed"), AllFiles.Num(), 1561);
    TestEqual(TEXT("Celia full-body animation files"), FullBodyFiles, 1292);
    TestEqual(TEXT("Other rigs and props present in these folders"), OtherFiles, 269);
    for (const auto& Source : SampleFiles())
    {
        const FPsaReader Reader(Source);
        if (!TestTrue(*Reader.Error, Reader.bIsValid)) continue;
        FPsaImportOptions Options; Options.bReplaceExisting = true;
        TArray<UAnimSequence*> Assets; TArray<FString> Warnings; FString Summary, Error;
        if (!TestTrue(*(Source + TEXT(": ") + Error), FPsaImporter::ImportFile(Source, Mesh, TEXT("/Game/PSACeliaValidation/Animations"), Options, Assets, Summary, Error, &Warnings))) { AddError(Error); continue; }
        if (!TestEqual(TEXT("One sample animation"), Assets.Num(), 1)) continue;
        TestTrue(TEXT("Detected source shown"), Summary.Contains(TEXT("UEViewer")));
        TestTrue(TEXT("Missing hierarchy disclosed in warning log"), FString::Join(Warnings, TEXT(" ")).Contains(TEXT("占位层级")));
        CheckPlayback(*this, Mesh, Assets[0], Reader, Source);
    }
    AddInfo(FString::Printf(TEXT("Parsed %d PSA files; %d fully match Celia; imported and checked %d representative clips."), AllFiles.Num(), FullBodyFiles, SampleFiles().Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEViewerReloadTest, "UnrealPSKPSA.UEViewer.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEViewerReloadTest::RunTest(const FString& Parameters)
{
    using namespace UEViewerTests;
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
    if (!TestNotNull(TEXT("Saved Celia body"), Mesh)) return false;
    for (const auto& Source : SampleFiles())
    {
        const FPsaReader Reader(Source); if (!TestTrue(*Reader.Error, Reader.bIsValid)) continue;
        const FString Name = Reader.Sequences[0].Name;
        auto* Anim = LoadObject<UAnimSequence>(nullptr, *(FString(TEXT("/Game/PSACeliaValidation/Animations/")) + Name + TEXT(".") + Name));
        if (!TestNotNull(TEXT("Saved Celia animation"), Anim)) continue;
        Anim->WaitOnExistingCompression();
        CheckPlayback(*this, Mesh, Anim, Reader, Source);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEViewerMetadataTest, "UnrealPSKPSA.UEViewer.Metadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEViewerMetadataTest::RunTest(const FString& Parameters)
{
    using namespace UEViewerTests;
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
    if (!TestNotNull(TEXT("Celia validation mesh"), Mesh)) return false;
    const FString Source = Root() / TEXT("Anim/Idle/Celia_Idle_breathing.psa");
    FPsaReader Reader(Source);
    if (!TestTrue(TEXT("UEViewer reader valid"), Reader.bIsValid)) return false;
    TArray<FPsaBoneMapping> Mapping; FString Error;
    // Once metadata is known to represent actual parents, a conflict must still fail.
    Reader.bHasUEViewerBoneMetadata = false;
    TestFalse(TEXT("Real hierarchy conflict remains rejected"), FPsaImporter::MatchBones(Reader, Mesh, Mapping, Error));
    TestTrue(TEXT("Mismatch explained"), Error.Contains(TEXT("骨骼层级不兼容")));
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes, *Source);
    // The first two headers are ANIMHEAD and BONENAMES. Remove the distinctive length marker.
    const float Zero = 0;
    FMemory::Memcpy(Bytes.GetData() + 64 + 104, &Zero, 4);
    const FString Fixture = FPaths::ProjectSavedDir() / TEXT("PSACeliaValidation/real_hierarchy.psa");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Fixture), true);
    FFileHelper::SaveArrayToFile(Bytes, *Fixture);
    const FPsaReader RealHierarchy(Fixture);
    TestTrue(TEXT("Structurally valid genuine hierarchy"), RealHierarchy.bIsValid);
    TestFalse(TEXT("Flat parents alone do not trigger compatibility fallback"), RealHierarchy.bHasUEViewerBoneMetadata);
    TestFalse(TEXT("Unrecognized conflicting hierarchy rejected"), FPsaImporter::MatchBones(RealHierarchy, Mesh, Mapping, Error));
    return true;
}
#endif
