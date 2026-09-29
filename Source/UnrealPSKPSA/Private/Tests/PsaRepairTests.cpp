#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PsaReader.h"
#include "PsaImporter.h"
#include "PsaImportSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"
#include <limits>

namespace
{
    // Two independent five-frame animations, each with two bones, stored frame-major.
    bool WriteRepairFixture(const FString& Path, const TArray<FPsaKey>& Keys, const TArray<FPsaScaleKey>& Scales)
    {
        FBufferArchive Bytes;
        auto Name = [&](const FString& Value, int32 Size)
        {
            ANSICHAR Buffer[64]{};
            FCStringAnsi::Strncpy(Buffer, TCHAR_TO_UTF8(*Value), Size);
            Bytes.Serialize(Buffer, Size);
        };
        auto Int = [&](int32 Value) { Bytes << Value; };
        auto Float = [&](float Value) { Bytes << Value; };
        auto Chunk = [&](const TCHAR* Id, int32 Size, int32 Count)
        { Name(Id, 20); Int(1999801); Int(Size); Int(Count); };
        Chunk(TEXT("ANIMHEAD"), 0, 0);
        Chunk(TEXT("BONENAMES"), 120, 2);
        for (int32 Bone = 0; Bone < 2; ++Bone)
        {
            Name(Bone == 0 ? TEXT("Root") : TEXT("Child"), 64);
            Int(0); Int(Bone == 0 ? 1 : 0); Int(0);
            Float(0); Float(0); Float(0); Float(1);
            for (int32 I = 0; I < 7; ++I) Float(0);
        }
        Chunk(TEXT("ANIMINFO"), 168, 2);
        for (int32 Sequence = 0; Sequence < 2; ++Sequence)
        {
            Name(FString::Printf(TEXT("Repair_%d"), Sequence), 64); Name(TEXT("Test"), 64);
            Int(2); Int(0); Int(0); Int(0); Float(0); Float(5); Float(30);
            Int(0); Int(Sequence * 5); Int(5);
        }
        Chunk(TEXT("ANIMKEYS"), 32, Keys.Num());
        for (auto Key : Keys)
        { Bytes.Serialize(&Key.Position, 12); Bytes.Serialize(&Key.Rotation, 16); Float(Key.Time); }
        if (!Scales.IsEmpty())
        {
            Chunk(TEXT("SCALEKEYS"), 16, Scales.Num());
            for (auto Key : Scales) { Bytes.Serialize(&Key.Scale, 12); Float(Key.Time); }
        }
        return FFileHelper::SaveArrayToFile(Bytes, *Path);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaRepairKeysTest, "UnrealPSKPSA.PSA.RepairKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaRepairKeysTest::RunTest(const FString& Parameters)
{
    TestFalse(TEXT("Panel repair defaults off"), GetDefault<UPsaImportSettings>()->bRepairInvalidKeys);
    TestFalse(TEXT("Importer repair defaults off"), FPsaImportOptions().bRepairInvalidKeys);
    const FString Path = FPaths::ProjectSavedDir() / TEXT("PSAImport/repair_fixture.psa");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    TArray<FPsaKey> Keys;
    TArray<FPsaScaleKey> Scales;
    for (int32 Sequence = 0; Sequence < 2; ++Sequence)
        for (int32 Frame = 0; Frame < 5; ++Frame)
            for (int32 Bone = 0; Bone < 2; ++Bone)
            {
                FPsaKey Key;
                Key.Position = FVector3f(Sequence * 100 + Bone * 10 + Frame, Frame * 2, Frame * 3);
                Key.Rotation = FQuat4f(FVector3f::UpVector, Frame * 0.2f);
                // Quaternion sign changes represent the same orientation; interpolation must take the shortest arc.
                if (Frame == 3) Key.Rotation = Key.Rotation * -1.0f;
                Key.Time = 1;
                Keys.Add(Key);
                FPsaScaleKey Scale; Scale.Scale = FVector3f(1 + Frame * 0.1f, 1, 1); Scale.Time = 1;
                Scales.Add(Scale);
            }
    if (!TestTrue(TEXT("Write valid fixture"), WriteRepairFixture(Path, Keys, Scales))) return false;
    const FPsaReader Valid(Path, true);
    if (!TestTrue(TEXT("Identity and zero components are valid"), Valid.bIsValid)) return false;
    TestEqual(TEXT("Valid keys not counted as repairs"), Valid.InterpolatedKeyCount + Valid.CopiedKeyCount, 0);
    const auto OriginalKeys = Keys;
    auto BreakKey = [&](int32 Index) { Keys[Index].Rotation = FQuat4f(0, 0, 0, 0); Keys[Index].Position = FVector3f::ZeroVector; };
    for (int32 Index : {0, 2, 6, 8, 3, 5, 10, 12, 17, 19}) BreakKey(Index);
    WriteRepairFixture(Path, Keys, Scales);
    TestFalse(TEXT("Strict mode still rejects invalid keys"), FPsaReader(Path).bIsValid);
    const FPsaReader Fixed(Path, true);
    if (!TestTrue(*Fixed.Error, Fixed.bIsValid)) return false;
    TestEqual(TEXT("Interior gap count"), Fixed.InterpolatedKeyCount, 2);
    TestEqual(TEXT("All boundary gap keys copied"), Fixed.CopiedKeyCount, 8);
    for (int32 Index : {0, 2, 6, 8})
    {
        TestTrue(TEXT("Single anchor fills entire leading and trailing runs"), Fixed.Keys[Index].Position.Equals(OriginalKeys[4].Position));
        TestTrue(TEXT("Boundary rotation copied"), Fixed.Keys[Index].Rotation.Equals(OriginalKeys[4].Rotation));
        TestTrue(TEXT("Boundary scale copied"), Fixed.ScaleKeys[Index].Scale.Equals(Scales[4].Scale));
    }
    for (int32 Index : {3, 5})
    {
        TestTrue(TEXT("Interior position interpolated"), Fixed.Keys[Index].Position.Equals(OriginalKeys[Index].Position, 0.0001f));
        TestTrue(TEXT("Shortest-arc quaternion interpolation"), Fixed.Keys[Index].Rotation.Equals(OriginalKeys[Index].Rotation, 0.0001f));
        TestTrue(TEXT("Interior scale interpolated"), Fixed.ScaleKeys[Index].Scale.Equals(Scales[Index].Scale, 0.0001f));
    }
    for (int32 Index : {10, 12}) TestTrue(TEXT("Leading gap cannot borrow previous sequence"), Fixed.Keys[Index].Position.Equals(OriginalKeys[14].Position));
    for (int32 Index : {17, 19}) TestTrue(TEXT("Trailing gap cannot borrow another bone"), Fixed.Keys[Index].Position.Equals(OriginalKeys[15].Position));
    for (int32 Index : {1, 4, 7, 9, 11, 13, 14, 15, 16, 18})
        TestTrue(TEXT("Valid original pose unchanged"), Fixed.Keys[Index].Position.Equals(Valid.Keys[Index].Position) && Fixed.Keys[Index].Rotation.Equals(Valid.Keys[Index].Rotation));
    BreakKey(4);
    WriteRepairFixture(Path, Keys, Scales);
    const FPsaReader EmptyTrack(Path, true);
    TestFalse(TEXT("Entire invalid bone track fails despite valid next animation"), EmptyTrack.bIsValid);
    TestTrue(TEXT("Failure identifies bone and animation"), EmptyTrack.Error.Contains(TEXT("Root")) && EmptyTrack.Error.Contains(TEXT("Repair_0")));
    Keys = OriginalKeys;
    BreakKey(3);
    WriteRepairFixture(Path, Keys, {});
    TestTrue(TEXT("Repair also supports absent SCALEKEYS"), FPsaReader(Path, true).bIsValid);
    Keys = OriginalKeys;
    Keys[3].Position.X = std::numeric_limits<float>::quiet_NaN();
    WriteRepairFixture(Path, Keys, Scales);
    TestTrue(TEXT("Nonfinite position repaired"), FPsaReader(Path, true).bIsValid);
    Keys = OriginalKeys;
    Scales[3].Scale.Y = std::numeric_limits<float>::infinity();
    WriteRepairFixture(Path, Keys, Scales);
    TestFalse(TEXT("Strict scale validation retained"), FPsaReader(Path).bIsValid);
    TestTrue(TEXT("Invalid scale repaired from whole valid poses"), FPsaReader(Path, true).bIsValid);
    Keys[3].Time = -1;
    WriteRepairFixture(Path, Keys, Scales);
    TestFalse(TEXT("Invalid timing is not a repairable pose"), FPsaReader(Path, true).bIsValid);
    Keys = OriginalKeys;
    Scales.Pop();
    WriteRepairFixture(Path, Keys, Scales);
    TestFalse(TEXT("Mismatched scale count remains invalid"), FPsaReader(Path, true).bIsValid);
    TArray<uint8> Bytes;
    FFileHelper::LoadFileToArray(Bytes, *Path); Bytes.Pop(); FFileHelper::SaveArrayToFile(Bytes, *Path);
    TestFalse(TEXT("Truncated file remains invalid with repair enabled"), FPsaReader(Path, true).bIsValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaRepairSamplesTest, "UnrealPSKPSA.PSA.RepairSamples", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaRepairSamplesTest::RunTest(const FString& Parameters)
{
    FString Folder; FParse::Value(FCommandLine::Get(), TEXT("PsaTestDir="), Folder);
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PSAValidation/Mesh/girl023_LV2_body01_skm.girl023_LV2_body01_skm"));
    if (!TestNotNull(TEXT("Sample target mesh"), Mesh)) return false;
    TArray<FString> Audit;
    for (const TCHAR* Name : {TEXT("girl023_wp03a_001_aim_shoot_add"), TEXT("girl023_wp03a_001_shoot_add"), TEXT("girl023_wp03a_002_aim_shoot_add"), TEXT("girl023_wp03a_002_shoot_add")})
    {
        const FString Path = Folder / (FString(Name) + TEXT(".psa"));
        TArray<uint8> Before, After; FFileHelper::LoadFileToArray(Before, *Path);
        TestFalse(TEXT("Source invalid in strict mode"), FPsaReader(Path).bIsValid);
        const FPsaReader Fixed(Path, true);
        if (!TestTrue(*(Path + TEXT(": ") + Fixed.Error), Fixed.bIsValid)) continue;
        TestTrue(TEXT("Repair count records damaged poses"), Fixed.InterpolatedKeyCount + Fixed.CopiedKeyCount > 0);
        FPsaImportOptions Options; Options.bReplaceExisting = true;
        TArray<UAnimSequence*> Assets; FString Summary, Error;
        TArray<FString> Warnings { TEXT("Previous file warning") };
        TestFalse(TEXT("Importer defaults still reject corrupt source"), FPsaImporter::ImportFile(Path, Mesh, TEXT("/Game/PSARepairValidation"), Options, Assets, Summary, Error, &Warnings));
        TestTrue(TEXT("Failed import reports error without stale warnings or success"), !Error.IsEmpty() && Summary.IsEmpty() && Warnings.IsEmpty());
        Options.bRepairInvalidKeys = true;
        const bool bImported = FPsaImporter::ImportFile(Path, Mesh, TEXT("/Game/PSARepairValidation"), Options, Assets, Summary, Error, &Warnings);
        if (!TestTrue(*(Path + TEXT(": ") + Error), bImported) || !TestEqual(TEXT("One repaired sequence"), Assets.Num(), 1)) continue;
        const FString WarningText = FString::Join(Warnings, TEXT("\n"));
        TestTrue(TEXT("Repair and bone mapping issues are warnings"), WarningText.Contains(TEXT("已修复")) && WarningText.Contains(TEXT("忽略")) && WarningText.Contains(TEXT("参考姿态")));
        TestTrue(TEXT("Success is separate from repair warning"), !Summary.IsEmpty() && !Summary.Contains(TEXT("已修复")) && Error.IsEmpty());
        const auto& Info = Fixed.Sequences[0];
        UAnimSequence* Sequence = Assets[0];
        TestEqual(TEXT("Repair preserves frame count"), Sequence->GetDataModel()->GetNumberOfKeys(), Info.NumRawFrames);
        TestTrue(TEXT("Repair preserves duration"), FMath::IsNearlyEqual(Sequence->GetPlayLength(), double(Info.NumRawFrames) / Info.AnimRate, 0.0001));
        TArray<FPsaBoneMapping> Mapping; FPsaImporter::MatchBones(Fixed, Mesh, Mapping, Error);
        bool bPosesOK = true, bPlaybackOK = true;
        for (const auto& Bone : Mapping)
            for (int32 Frame = 0; Frame < Info.NumRawFrames; ++Frame)
            {
                const auto& Key = Fixed.Keys[(Info.FirstRawFrame + Frame) * Info.TotalBones + Bone.SourceIndex];
                const FTransform Raw = Sequence->GetDataModel()->GetBoneTrackTransform(Mesh->GetRefSkeleton().GetBoneName(Bone.TargetIndex), FFrameNumber(Frame));
                const FQuat Rotation(Key.Rotation.X, -Key.Rotation.Y, Key.Rotation.Z, Bone.SourceIndex == 0 ? -Key.Rotation.W : Key.Rotation.W);
                bPosesOK &= !Raw.ContainsNaN() && Raw.GetTranslation().Equals(FVector(Key.Position.X, -Key.Position.Y, Key.Position.Z), 0.001) && Raw.GetRotation().Equals(Rotation, 0.001);
                FTransform Evaluated;
                Sequence->GetBoneTransform(Evaluated, FSkeletonPoseBoneIndex(Bone.TargetIndex), FAnimExtractContext(Frame / Sequence->GetDataModel()->GetFrameRate().AsDecimal(), false), false);
                bPlaybackOK &= !Evaluated.ContainsNaN() && Evaluated.GetTranslation().Equals(Raw.GetTranslation(), 0.1) && Evaluated.GetRotation().AngularDistance(Raw.GetRotation()) < 0.01;
            }
        TestTrue(TEXT("All repaired raw poses correctly converted"), bPosesOK);
        TestTrue(TEXT("All repaired compressed playback poses valid"), bPlaybackOK);
        FFileHelper::LoadFileToArray(After, *Path);
        TestTrue(TEXT("Source PSA never modified"), Before == After);
        Audit.Add(Summary + TEXT("\n") + WarningText); AddInfo(Summary + TEXT("\n") + WarningText);
    }
    FFileHelper::SaveStringToFile(FString::Join(Audit, TEXT("\n\n")), *(FPaths::ProjectSavedDir() / TEXT("PSAImport/RepairAudit.txt")));
    return true;
}
#endif
