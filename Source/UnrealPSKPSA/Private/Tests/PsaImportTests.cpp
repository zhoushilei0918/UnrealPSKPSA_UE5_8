#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PsaReader.h"
#include "PsaImporter.h"
#include "PskFactory.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FString PsaFolder()
    {
        FString Folder;
        FParse::Value(FCommandLine::Get(), TEXT("PsaTestDir="), Folder);
        return Folder;
    }
    const TCHAR* MeshPath = TEXT("/Game/PSAValidation/Mesh/girl023_LV2_body01_skm.girl023_LV2_body01_skm");
    bool SavePsaTestAsset(UObject* Asset)
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaDirectoryImportTest, "UnrealPSKPSA.PSA.DirectoryImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaDirectoryImportTest::RunTest(const FString& Parameters)
{
    FString PskFile;
    FParse::Value(FCommandLine::Get(), TEXT("PskTestFile="), PskFile);
    if (!TestTrue(TEXT("PSK exists"), FPaths::FileExists(PskFile))) return false;
    UPackage* Package = CreatePackage(TEXT("/Game/PSAValidation/Mesh/girl023_LV2_body01_skm"));
    USkeletalMesh* Mesh = Cast<USkeletalMesh>(UPskFactory::Import(PskFile, Package, TEXT("girl023_LV2_body01_skm"), RF_Public | RF_Standalone, {}, FActorXOrientation::Unchanged()));
    if (!TestNotNull(TEXT("Create target mesh"), Mesh)) return false;
    TestTrue(TEXT("Save target mesh"), SavePsaTestAsset(Mesh));
    TestTrue(TEXT("Save skeleton"), SavePsaTestAsset(Mesh->GetSkeleton()));
    for (auto& Material : Mesh->GetMaterials()) if (Material.MaterialInterface) SavePsaTestAsset(Material.MaterialInterface);

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(PsaFolder() / TEXT("*.psa")), true, false);
    Files.Sort();
    if (!TestEqual(TEXT("Supplied sample count"), Files.Num(), 142)) return false;
    int32 ImportedCount = 0, RejectedCount = 0, InvalidCount = 0;
    TArray<FString> Audit;
    for (const FString& File : Files)
    {
        const FString FullPath = PsaFolder() / File;
        const FPsaReader Reader(FullPath);
        if (!Reader.bIsValid)
        {
            const bool bKnownBrokenExport = File == TEXT("girl023_wp03a_001_aim_shoot_add.psa") || File == TEXT("girl023_wp03a_001_shoot_add.psa") ||
                File == TEXT("girl023_wp03a_002_aim_shoot_add.psa") || File == TEXT("girl023_wp03a_002_shoot_add.psa");
            TestTrue(*(TEXT("Reject zero-quaternion source export: ") + File), bKnownBrokenExport);
            ++InvalidCount;
            Audit.Add(TEXT("INVALID ") + File + TEXT("\n") + Reader.Error);
            continue;
        }
        TArray<FPsaBoneMapping> Mapping;
        FString Error, Summary;
        if (Reader.Bones.Num() == 176)
        {
            TestFalse(TEXT("Reject known incompatible hierarchy"), FPsaImporter::MatchBones(Reader, Mesh, Mapping, Error));
            ++RejectedCount;
            Audit.Add(TEXT("INCOMPATIBLE ") + File + TEXT("\n") + Error);
            continue;
        }
        if (!TestTrue(TEXT("Match compatible bones"), FPsaImporter::MatchBones(Reader, Mesh, Mapping, Error))) continue;
        TestEqual(TEXT("Match names, not array indices"), Mapping.Num(), 235);
        FPsaImportOptions Options; Options.bReplaceExisting = true;
        TArray<UAnimSequence*> Assets;
        if (!TestTrue(*(TEXT("Import ") + File + TEXT(": ") + Error), FPsaImporter::ImportFile(FullPath, Mesh, TEXT("/Game/PSAValidation/Animations"), Options, Assets, Summary, Error)))
        { AddError(Error); continue; }
        TestEqual(TEXT("One sequence per sample file"), Assets.Num(), 1);
        UAnimSequence* Sequence = Assets[0];
        const FPsaSequenceInfo& Info = Reader.Sequences[0];
        TestEqual(TEXT("Target skeleton"), Sequence->GetSkeleton(), Mesh->GetSkeleton());
        TestEqual(TEXT("Sample count"), Sequence->GetDataModel()->GetNumberOfKeys(), Info.NumRawFrames);
        TestTrue(TEXT("FModel duration retained"), FMath::IsNearlyEqual(Sequence->GetPlayLength(), double(Info.NumRawFrames) / Info.AnimRate, 0.0001));
        TArray<FName> TrackNames;
        Sequence->GetDataModel()->GetBoneTrackNames(TrackNames);
        TestEqual(TEXT("Only matched bones get tracks"), TrackNames.Num(), Mapping.Num());
        // Independent oracle: FModel's original JSON export supplies the source duration.
        FString Json;
        if (FFileHelper::LoadFileToString(Json, *FPaths::ChangeExtension(FullPath, TEXT("json"))))
        {
            TArray<TSharedPtr<FJsonValue>> Objects;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Objects))
                for (const auto& Object : Objects)
                {
                    const auto Obj = Object->AsObject();
                    if (Obj->GetStringField(TEXT("Type")) != TEXT("AnimSequence")) continue;
                    const auto Props = Obj->GetObjectField(TEXT("Properties"));
                    double Length, Scale = 1;
                    Props->TryGetNumberField(TEXT("RateScale"), Scale);
                    if (Props->TryGetNumberField(TEXT("SequenceLength"), Length))
                        TestTrue(*(TEXT("Original JSON duration: ") + File), FMath::IsNearlyEqual(Sequence->GetPlayLength(), Length / FMath::Max(1.0, Scale), 0.0002));
                }
        }
        // Verify evaluated/compressed playback rather than just successful asset creation.
        for (const auto& Bone : Mapping)
        {
            const FName Name = Mesh->GetRefSkeleton().GetBoneName(Bone.TargetIndex);
            for (const int32 Frame : {0, Info.NumRawFrames / 2, Info.NumRawFrames - 1})
            {
                const auto& Key = Reader.Keys[Frame * Info.TotalBones + Bone.SourceIndex];
                const FTransform Raw = Sequence->GetDataModel()->GetBoneTrackTransform(Name, FFrameNumber(Frame));
                const FVector ExpectedPosition(Key.Position.X, -Key.Position.Y, Key.Position.Z);
                const FQuat ExpectedRotation(Key.Rotation.X, -Key.Rotation.Y, Key.Rotation.Z, Bone.SourceIndex == 0 ? -Key.Rotation.W : Key.Rotation.W);
                TestTrue(TEXT("Position restored to UE coordinates"), Raw.GetTranslation().Equals(ExpectedPosition, 0.001));
                TestTrue(TEXT("Root/non-root quaternion conversion"), Raw.GetRotation().Equals(ExpectedRotation, 0.001));
                FTransform Evaluated;
                const double Time = Frame / Sequence->GetDataModel()->GetFrameRate().AsDecimal();
                Sequence->GetBoneTransform(Evaluated, FSkeletonPoseBoneIndex(Bone.TargetIndex), FAnimExtractContext(Time, false), false);
                TestFalse(TEXT("Playback transform finite"), Evaluated.ContainsNaN());
                TestTrue(TEXT("Compressed playback translation"), Evaluated.GetTranslation().Equals(Raw.GetTranslation(), 0.1));
                TestTrue(*FString::Printf(TEXT("Compressed playback rotation %s %s frame %d error %.6f"), *File, *Name.ToString(), Frame,
                    Evaluated.GetRotation().AngularDistance(Raw.GetRotation())), Evaluated.GetRotation().AngularDistance(Raw.GetRotation()) < 0.01);
            }
        }
        Audit.Add(TEXT("OK ") + Summary);
        ++ImportedCount;
    }
    TestEqual(TEXT("Compatible valid animations imported"), ImportedCount, 124);
    TestEqual(TEXT("Other skeleton rejected explicitly"), RejectedCount, 14);
    TestEqual(TEXT("Corrupt source animations rejected explicitly"), InvalidCount, 4);
    FFileHelper::SaveStringToFile(FString::Join(Audit, TEXT("\n\n")), *(FPaths::ProjectSavedDir() / TEXT("PSAImport/DirectoryAudit.txt")));

    // Corrupted data must never reach the animation controller.
    TArray<uint8> Bytes;
    FFileHelper::LoadFileToArray(Bytes, *(PsaFolder() / Files[0]));
    Bytes.SetNum(Bytes.Num() - 1);
    const FString Broken = FPaths::ProjectSavedDir() / TEXT("PSAImport/truncated.psa");
    FFileHelper::SaveArrayToFile(Bytes, *Broken);
    TestFalse(TEXT("Truncated scale chunk rejected"), FPsaReader(Broken).bIsValid);
    AddInfo(FString::Printf(TEXT("Imported %d; incompatible %d; invalid %d. Full report: Saved/PSAImport/DirectoryAudit.txt"), ImportedCount, RejectedCount, InvalidCount));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaReloadTest, "UnrealPSKPSA.PSA.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaReloadTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
    if (!TestNotNull(TEXT("Reload mesh"), Mesh)) return false;
    for (const TCHAR* Name : {TEXT("girl023_wp03a_base_run_loop"), TEXT("girl023_wp03a_shoot_stand"), TEXT("girl023_shoot_pose_down")})
    {
        const FString Path = FString::Printf(TEXT("/Game/PSAValidation/Animations/%s.%s"), Name, Name);
        UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path);
        if (!TestNotNull(TEXT("Reload animation"), Sequence)) continue;
        FAssetCompilingManager::Get().FinishCompilationForObjects({Sequence});
        TestEqual(TEXT("Skeleton reference survives save"), Sequence->GetSkeleton(), Mesh->GetSkeleton());
        TArray<FName> Names; Sequence->GetDataModel()->GetBoneTrackNames(Names);
        TestEqual(TEXT("Track count survives save"), Names.Num(), 235);
        const int32 BoneIndex = Mesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(TEXT("Bip001"));
        FTransform Transform;
        Sequence->GetBoneTransform(Transform, FSkeletonPoseBoneIndex(BoneIndex), FAnimExtractContext(Sequence->GetPlayLength() * 0.5, false), false);
        TestFalse(TEXT("Saved animation evaluates"), Transform.ContainsNaN());
        TestTrue(TEXT("Saved animation moves hip above ground"), Transform.GetTranslation().Z > 20);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaSavedPlaybackTest, "UnrealPSKPSA.PSA.SavedPlayback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaSavedPlaybackTest::RunTest(const FString& Parameters)
{
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(FPaths::ProjectContentDir() / TEXT("PSAValidation/Animations/*.uasset")), true, false);
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
    if (!TestNotNull(TEXT("Saved target mesh"), Mesh)) return false;
    TestEqual(TEXT("Saved sample animations"), Files.Num(), 124);
    for (const FString& File : Files)
    {
        const FString Name = FPaths::GetBaseFilename(File);
        UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *(TEXT("/Game/PSAValidation/Animations/") + Name + TEXT(".") + Name));
        if (!TestNotNull(*Name, Sequence)) continue;
        FAssetCompilingManager::Get().FinishCompilationForObjects({Sequence});
        if (FParse::Param(FCommandLine::Get(), TEXT("PsaRefreshCompression")))
        {
            Sequence->BoneCompressionSettings = LoadObject<UAnimBoneCompressionSettings>(nullptr,
                TEXT("/Engine/Animation/DefaultRecorderBoneCompression.DefaultRecorderBoneCompression"));
            FPropertyChangedEvent Change(FindFProperty<FProperty>(UAnimSequence::StaticClass(), GET_MEMBER_NAME_CHECKED(UAnimSequence, BoneCompressionSettings)));
            Sequence->PostEditChangeProperty(Change);
            FAssetCompilingManager::Get().FinishCompilationForObjects({Sequence});
            TestTrue(TEXT("Save updated playback compression"), SavePsaTestAsset(Sequence));
        }
        const IAnimationDataModel* Model = Sequence->GetDataModel();
        TArray<FName> Tracks; Model->GetBoneTrackNames(Tracks);
        for (FName Bone : Tracks)
        {
            const int32 Index = Mesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Bone);
            if (!TestTrue(TEXT("Track exists in skeleton"), Index != INDEX_NONE)) continue;
            for (int32 Frame : {0, Model->GetNumberOfKeys() / 2, Model->GetNumberOfKeys() - 1})
            {
                const FTransform Raw = Model->GetBoneTrackTransform(Bone, FFrameNumber(Frame));
                FTransform Evaluated;
                Sequence->GetBoneTransform(Evaluated, FSkeletonPoseBoneIndex(Index), FAnimExtractContext(Frame / Model->GetFrameRate().AsDecimal(), false), false);
                TestTrue(*(Name + TEXT(" compressed translation")), Evaluated.GetTranslation().Equals(Raw.GetTranslation(), 0.1));
                const double Error = Evaluated.GetRotation().AngularDistance(Raw.GetRotation());
                TestTrue(*FString::Printf(TEXT("%s %s frame %d compressed rotation error %.6f"), *Name, *Bone.ToString(), Frame, Error), Error < 0.01);
            }
        }
    }
    return true;
}
#endif
