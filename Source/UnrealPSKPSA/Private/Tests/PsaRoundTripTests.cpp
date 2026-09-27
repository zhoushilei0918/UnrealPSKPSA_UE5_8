#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PsaImporter.h"
#include "PsaReader.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"

// Small independent fixture: two bones, two sequences, scale keys, and an unknown chunk.
// Exercises frame offsets and repeated import without requiring the external FModel exports.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaRoundTripTest, "UnrealPSKPSA.PSA.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaRoundTripTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PSAValidation/Mesh/girl023_LV2_body01_skm.girl023_LV2_body01_skm"));
    if (!TestNotNull(TEXT("Validation target mesh"), Mesh)) return false;
    FBufferArchive Bytes;
    auto Name = [&](const FString& Value, int32 Size)
    {
        ANSICHAR Buffer[64]{};
        FCStringAnsi::Strncpy(Buffer, TCHAR_TO_UTF8(*Value), Size);
        Bytes.Serialize(Buffer, Size);
    };
    auto Chunk = [&](const TCHAR* Id, int32 Size, int32 Count)
    {
        Name(Id, 20); int32 Type = 1999801; Bytes << Type << Size << Count;
    };
    auto Int = [&](int32 Value) { Bytes << Value; };
    auto Float = [&](float Value) { Bytes << Value; };
    Chunk(TEXT("ANIMHEAD"), 0, 0);
    Chunk(TEXT("BONENAMES"), 120, 2);
    for (int32 Bone = 0; Bone < 2; ++Bone)
    {
        Name(Mesh->GetRefSkeleton().GetBoneName(Bone).ToString(), 64);
        Int(0); Int(Bone == 0 ? 1 : 0); Int(0);
        Float(0); Float(0); Float(0); Float(1);
        for (int32 Index = 0; Index < 7; ++Index) Float(0);
    }
    Chunk(TEXT("ANIMINFO"), 168, 2);
    for (int32 Sequence = 0; Sequence < 2; ++Sequence)
    {
        Name(FString::Printf(TEXT("PSA_Fixture_%d"), Sequence), 64); Name(TEXT("Tests"), 64);
        Int(2); Int(0); Int(0); Int(0); Float(0); Float(3); Float(30);
        Int(0); Int(Sequence * 3); Int(3);
    }
    Chunk(TEXT("UNKNOWN"), 4, 2); Int(123); Int(456);
    Chunk(TEXT("ANIMKEYS"), 32, 12);
    for (int32 Key = 0; Key < 12; ++Key)
    {
        Float(100 + Key); Float(-200 - Key); Float(300 + Key);
        const FQuat4f Q(FVector3f::UpVector, Key * 0.1f);
        Float(Q.X); Float(-Q.Y); Float(Q.Z); Float(Key % 2 == 0 ? -Q.W : Q.W); Float(1);
    }
    Chunk(TEXT("SCALEKEYS"), 16, 12);
    for (int32 Key = 0; Key < 12; ++Key) { Float(1); Float(1.2f); Float(1); Float(1); }
    const FString Filename = FPaths::ProjectSavedDir() / TEXT("PSAImport/roundtrip.psa");
    if (!TestTrue(TEXT("Write independent fixture"), FFileHelper::SaveArrayToFile(Bytes, *Filename))) return false;
    TestTrue(TEXT("Parse fixture with unknown chunk"), FPsaReader(Filename).bIsValid);
    FPsaImportOptions Options; Options.bReplaceExisting = true; Options.bSaveAssets = false;
    TArray<UAnimSequence*> FirstAssets;
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        TArray<UAnimSequence*> Assets; FString Summary, Error;
        if (!FPsaImporter::ImportFile(Filename, Mesh, TEXT("/Game/PSAValidation/Fixtures"), Options, Assets, Summary, Error))
        { AddError(Error); return false; }
        if (!TestEqual(TEXT("Both sequences imported"), Assets.Num(), 2)) return false;
        for (int32 Sequence = 0; Sequence < 2; ++Sequence)
        {
            auto* Model = Assets[Sequence]->GetDataModel();
            TArray<FName> Tracks; Model->GetBoneTrackNames(Tracks);
            TestEqual(TEXT("Overwrite resets tracks"), Tracks.Num(), 2);
            TestEqual(TEXT("Frame count"), Model->GetNumberOfKeys(), 3);
            TestTrue(TEXT("FModel frame rate"), FMath::IsNearlyEqual(Model->GetFrameRate().AsDecimal(), 20.0));
            for (int32 Bone = 0; Bone < 2; ++Bone)
                for (int32 Frame = 0; Frame < 3; ++Frame)
                {
                    const int32 Key = Sequence * 6 + Frame * 2 + Bone;
                    const FTransform Actual = Model->GetBoneTrackTransform(Mesh->GetRefSkeleton().GetBoneName(Bone), FFrameNumber(Frame));
                    TestTrue(TEXT("Sequence offset and position conversion"), Actual.GetTranslation().Equals(FVector(100 + Key, 200 + Key, 300 + Key), 0.001));
                    TestTrue(TEXT("Root and child rotations"), Actual.GetRotation().AngularDistance(FQuat(FVector::UpVector, Key * 0.1)) < 0.001);
                    TestTrue(TEXT("Scale keys"), Actual.GetScale3D().Equals(FVector(1, 1.2, 1) * Mesh->GetRefSkeleton().GetRefBonePose()[Bone].GetScale3D(), 0.001));
                }
            if (Pass == 1) TestEqual(TEXT("Overwrite retains asset identity"), Assets[Sequence], FirstAssets[Sequence]);
        }
        FirstAssets = Assets;
    }
    Options.bReplaceExisting = false;
    TArray<UAnimSequence*> UniqueAssets; FString Summary, Error;
    TestTrue(TEXT("Default import creates unique assets"), FPsaImporter::ImportFile(Filename, Mesh, TEXT("/Game/PSAValidation/Fixtures"), Options, UniqueAssets, Summary, Error));
    if (UniqueAssets.Num() == 2) TestTrue(TEXT("Existing asset protected"), UniqueAssets[0] != FirstAssets[0]);
    const uint8 LastByte = Bytes.Last();
    Bytes.SetNum(Bytes.Num() - 1);
    FFileHelper::SaveArrayToFile(Bytes, *Filename);
    TestFalse(TEXT("Truncated fixture rejected"), FPsaReader(Filename).bIsValid);
    // Restore the complete fixture for manual panel checks.
    Bytes.Add(LastByte);
    FFileHelper::SaveArrayToFile(Bytes, *Filename);
    return true;
}
#endif
