#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PsaImporter.h"
#include "PsaImportSettings.h"
#include "LocalizationTestUtils.h"
#include "PskFactory.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace PsaScaleTests
{
    const FString Root = TEXT("/Game/PSAScaleValidation");
    const FName ChainTip(TEXT("Bone_A_pd_09_006"));

    bool Save(UObject* Asset)
    {
        const FString Path = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Path, Args);
    }

    FVector ChainScale(USkeletalMesh* Mesh, UAnimSequence* Sequence, double Time)
    {
        FVector Scale = FVector::OneVector;
        const auto& Ref = Mesh->GetRefSkeleton();
        TArray<FName> Tracks;
        Sequence->GetDataModel()->GetBoneTrackNames(Tracks);
        for (int32 Bone = Ref.FindBoneIndex(ChainTip); Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
        {
            FTransform Pose = Ref.GetRefBonePose()[Bone];
            if (Tracks.Contains(Ref.GetBoneName(Bone)))
                Sequence->GetBoneTransform(Pose, FSkeletonPoseBoneIndex(Mesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(Bone))), FAnimExtractContext(Time, false), false);
            Scale *= Pose.GetScale3D();
        }
        return Scale;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaScaleImportTest, "UnrealPSKPSA.Scale.Import", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaScaleImportTest::RunTest(const FString& Parameters)
{
    using namespace PsaScaleTests;
    TestFalse(TEXT("Importer defaults to original animation scale"), FPsaImportOptions().bUseReferenceScale);
    TestFalse(TEXT("Panel defaults to original animation scale"), GetDefault<UPsaImportSettings>()->bUseReferenceScale);
    FString PskFile, PsaDirectory;
    FParse::Value(FCommandLine::Get(), TEXT("PskTestFile="), PskFile);
    FParse::Value(FCommandLine::Get(), TEXT("PsaTestDir="), PsaDirectory);
    if (!TestTrue(TEXT("Source mesh exists"), FPaths::FileExists(PskFile))) return false;
    UPackage* Package = CreatePackage(*(Root / TEXT("Mesh/girl023")));
    USkeletalMesh* Mesh = Cast<USkeletalMesh>(UPskFactory::Import(PskFile, Package, TEXT("girl023"), RF_Public | RF_Standalone, {}, FActorXOrientation()));
    if (!TestNotNull(TEXT("Import mesh with default +Y orientation"), Mesh)) return false;
    TestTrue(TEXT("Save mesh"), Save(Mesh));
    TestTrue(TEXT("Save skeleton"), Save(Mesh->GetSkeleton()));
    for (const auto& Material : Mesh->GetMaterials()) if (Material.MaterialInterface) Save(Material.MaterialInterface);
    const auto& Ref = Mesh->GetRefSkeleton();
    if (!TestTrue(TEXT("Sample contains affected chain"), Ref.FindBoneIndex(ChainTip) != INDEX_NONE)) return false;

    auto Import = [&](const FString& File, USkeletalMesh* Target, const FString& Folder, const FPsaImportOptions& Options)
    {
        TArray<UAnimSequence*> Assets;
        TArray<FString> Warnings;
        FString Summary, Error;
        if (!FPsaImporter::ImportFile(File, Target, Folder, Options, Assets, Summary, Error, &Warnings))
        { AddError(Error); return static_cast<UAnimSequence*>(nullptr); }
        TestEqual(TEXT("One sequence imported"), Assets.Num(), 1);
        TestEqual(TEXT("Reference scale override is reported"), Warnings.ContainsByPredicate([](const FString& Warning)
            { return Warning == ActorXTestText(TEXT("UseReferenceScaleWarning")).ToString(); }), Options.bUseReferenceScale);
        return Assets.IsEmpty() ? nullptr : Assets[0];
    };
    FPsaImportOptions OriginalOptions; OriginalOptions.bReplaceExisting = true;
    FPsaImportOptions ReferenceOptions = OriginalOptions; ReferenceOptions.bUseReferenceScale = true;
    for (const TCHAR* Name : {TEXT("girl023_shoot_pose_down"), TEXT("girl023_wp03a_base_run_loop")})
    {
        const FString File = PsaDirectory / (FString(Name) + TEXT(".psa"));
        UAnimSequence* Original = Import(File, Mesh, Root / TEXT("Original"), OriginalOptions);
        UAnimSequence* Fixed = Import(File, Mesh, Root / TEXT("Reference"), ReferenceOptions);
        if (!Original || !Fixed) return false;
        const auto* OriginalModel = Original->GetDataModel();
        const auto* FixedModel = Fixed->GetDataModel();
        TestEqual(TEXT("Sample count unchanged"), FixedModel->GetNumberOfKeys(), OriginalModel->GetNumberOfKeys());
        TestEqual(TEXT("Frame rate unchanged"), FixedModel->GetFrameRate(), OriginalModel->GetFrameRate());
        TestEqual(TEXT("Skeleton unchanged"), Fixed->GetSkeleton(), Original->GetSkeleton());
        TestTrue(TEXT("Duration unchanged"), FMath::IsNearlyEqual(Fixed->GetPlayLength(), Original->GetPlayLength()));
        TArray<FName> Tracks; FixedModel->GetBoneTrackNames(Tracks);
        TestEqual(TEXT("Matching still produces 235 tracks"), Tracks.Num(), 235);
        for (FName Bone : Tracks)
        {
            const FVector ExpectedScale = Ref.GetRefBonePose()[Ref.FindBoneIndex(Bone)].GetScale3D();
            const int32 SkeletonIndex = Mesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Bone);
            for (int32 Frame = 0; Frame < FixedModel->GetNumberOfKeys(); ++Frame)
            {
                const FTransform Before = OriginalModel->GetBoneTrackTransform(Bone, FFrameNumber(Frame));
                const FTransform After = FixedModel->GetBoneTrackTransform(Bone, FFrameNumber(Frame));
                TestTrue(TEXT("All local positions unchanged"), After.GetTranslation().Equals(Before.GetTranslation(), 0.0001));
                TestTrue(TEXT("All local rotations unchanged"), After.GetRotation().Equals(Before.GetRotation(), 0.0001));
                TestTrue(TEXT("All keys use target mesh reference scale"), After.GetScale3D().Equals(ExpectedScale, 0.0001));
                FTransform Evaluated;
                Fixed->GetBoneTransform(Evaluated, FSkeletonPoseBoneIndex(SkeletonIndex), FAnimExtractContext(Frame / FixedModel->GetFrameRate().AsDecimal(), false), false);
                TestTrue(TEXT("Playback preserves reference scale"), Evaluated.GetScale3D().Equals(ExpectedScale, 0.001));
                TestTrue(TEXT("Playback preserves translation"), Evaluated.GetTranslation().Equals(After.GetTranslation(), 0.1));
                TestTrue(TEXT("Playback preserves rotation"), Evaluated.GetRotation().AngularDistance(After.GetRotation()) < 0.01);
                if (FString(Name).Contains(TEXT("run_loop")))
                    TestTrue(TEXT("Unit-scale run animation remains identical"), Before.GetScale3D().Equals(After.GetScale3D(), 0.001));
            }
        }
        if (FString(Name).Contains(TEXT("pose_down")))
        {
            for (double Time : {0.0, double(Fixed->GetPlayLength())})
            {
                TestTrue(TEXT("Original file retains 128x accumulated chain scale"), ChainScale(Mesh, Original, Time).Equals(FVector(128), 0.01));
                TestTrue(TEXT("Reference scale removes 128x stretch"), ChainScale(Mesh, Fixed, Time).Equals(FVector::OneVector, 0.001));
            }
            // Cover replacing an existing sequence in both directions; no stale scale keys.
            UAnimSequence* Replaced = Import(File, Mesh, Root / TEXT("Replace"), OriginalOptions);
            if (!Replaced) return false;
            TestEqual(TEXT("Overwrite reuses sequence"), Import(File, Mesh, Root / TEXT("Replace"), ReferenceOptions), Replaced);
            TestTrue(TEXT("Overwrite applies reference scale"), ChainScale(Mesh, Replaced, 0).Equals(FVector::OneVector, 0.001));
            TestEqual(TEXT("Can restore original scale"), Import(File, Mesh, Root / TEXT("Replace"), OriginalOptions), Replaced);
            TestTrue(TEXT("Disabling override restores original keys"), ChainScale(Mesh, Replaced, 0).Equals(FVector(128), 0.01));
        }
    }

    // Reference scales are not necessarily 1, and differ from the Skeleton asset.
    USkeletalMesh* ScaledMesh = NewObject<USkeletalMesh>();
    ScaledMesh->SetRefSkeleton(Ref);
    ScaledMesh->SetSkeleton(Mesh->GetSkeleton());
    const int32 BoneIndex = Ref.FindBoneIndex(ChainTip);
    const FVector NonUnitScale(1.5, 0.75, 2.0);
    FTransform BonePose = Ref.GetRefBonePose()[BoneIndex]; BonePose.SetScale3D(NonUnitScale);
    {
        FReferenceSkeletonModifier Modifier(ScaledMesh->GetRefSkeleton(), ScaledMesh->GetSkeleton());
        Modifier.UpdateRefPoseTransform(BoneIndex, BonePose);
    }
    ReferenceOptions.bSaveAssets = false;
    ReferenceOptions.bAutoDetectSource = false;
    for (bool bFModel : {true, false})
    {
        ReferenceOptions.bFModel = bFModel;
        UAnimSequence* Sequence = Import(PsaDirectory / TEXT("girl023_shoot_pose_down.psa"), ScaledMesh, Root / TEXT("NonUnit"), ReferenceOptions);
        if (!Sequence) return false;
        TestTrue(TEXT("Uses mesh reference exactly once in both source modes"), Sequence->GetDataModel()->GetBoneTrackTransform(ChainTip, FFrameNumber(0)).GetScale3D().Equals(NonUnitScale, 0.0001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaScaleReloadTest, "UnrealPSKPSA.Scale.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaScaleReloadTest::RunTest(const FString& Parameters)
{
    using namespace PsaScaleTests;
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Root / TEXT("Mesh/girl023.girl023")));
    if (!TestNotNull(TEXT("Saved mesh"), Mesh)) return false;
    for (const TCHAR* Mode : {TEXT("Original"), TEXT("Reference")})
    {
        UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *(Root / Mode / TEXT("girl023_shoot_pose_down.girl023_shoot_pose_down")));
        if (!TestNotNull(TEXT("Saved animation"), Sequence)) return false;
        Sequence->WaitOnExistingCompression();
        const FVector Expected = FString(Mode) == TEXT("Reference") ? FVector::OneVector : FVector(128);
        for (double Time : {0.0, double(Sequence->GetPlayLength())})
            TestTrue(TEXT("Scale selection survives save and compressed reload"), ChainScale(Mesh, Sequence, Time).Equals(Expected, 0.01));
    }
    return true;
}
#endif
