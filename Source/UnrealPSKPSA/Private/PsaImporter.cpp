#include "PsaImporter.h"
#include "PsaReader.h"
#include "ActorXImportSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

bool FPsaImporter::MatchBones(const FPsaReader& Reader, const USkeletalMesh* Mesh, TArray<FPsaBoneMapping>& Mapping, FString& Error)
{
    Mapping.Reset();
    Error.Reset();
    if (!Reader.bIsValid) { Error = Reader.Error; return false; }
    if (!Mesh || !Mesh->GetSkeleton()) { Error = NSLOCTEXT("UnrealPSKPSA", "MissingTargetSkeleton", "Select a Skeletal Mesh that has a Skeleton.").ToString(); return false; }
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    if (Ref.GetRawBoneNum() == 0 || Ref.GetBoneName(0) != FName(UTF8_TO_TCHAR(Reader.Bones[0].Name)))
    { Error = NSLOCTEXT("UnrealPSKPSA", "RootMismatch", "The target mesh and PSA root bone names differ. Select the matching mesh.").ToString(); return false; }
    TArray<FString> Conflicts;
    for (int32 SourceIndex = 0; SourceIndex < Reader.Bones.Num(); ++SourceIndex)
    {
        const auto& Bone = Reader.Bones[SourceIndex];
        const FName Name(UTF8_TO_TCHAR(Bone.Name));
        const int32 TargetIndex = Ref.FindBoneIndex(Name);
        if (TargetIndex == INDEX_NONE || TargetIndex >= Ref.GetRawBoneNum()) continue;
        const FName SourceParent = Bone.ParentIndex == INDEX_NONE ? NAME_None : FName(UTF8_TO_TCHAR(Reader.Bones[Bone.ParentIndex].Name));
        const int32 TargetParentIndex = Ref.GetParentIndex(TargetIndex);
        const FName TargetParent = TargetParentIndex == INDEX_NONE ? NAME_None : Ref.GetBoneName(TargetParentIndex);
        if (!Reader.bHasUEViewerBoneMetadata && SourceParent != TargetParent)
            Conflicts.Add(FText::Format(NSLOCTEXT("UnrealPSKPSA", "ParentMismatch", "{0} (PSA parent: {1}; target parent: {2})"), FText::FromString(Name.ToString()), FText::FromString(SourceParent.ToString()), FText::FromString(TargetParent.ToString())).ToString());
        Mapping.Add({SourceIndex, TargetIndex});
    }
    if (!Conflicts.IsEmpty())
    {
        Error = NSLOCTEXT("UnrealPSKPSA", "HierarchyMismatch", "Incompatible bone hierarchy. Use the matching source mesh or retarget the animation first:\n").ToString() + FString::Join(Conflicts, TEXT("\n"));
        Mapping.Reset();
        return false;
    }
    if (Mapping.Num() < FMath::Min(2, Ref.GetRawBoneNum())) { Error = NSLOCTEXT("UnrealPSKPSA", "InsufficientMatches", "Not enough bones with matching names were found.").ToString(); return false; }
    return true;
}

bool FPsaImporter::ImportFile(const FString& Filename, USkeletalMesh* Mesh, const FString& Destination,
    const FPsaImportOptions& Options, TArray<UAnimSequence*>& Imported, FString& Summary, FString& Error, TArray<FString>* OutWarnings)
{
    Error.Reset(); Summary.Reset();
    if (OutWarnings) OutWarnings->Reset();
    const FPsaReader Reader(Filename, Options.bRepairInvalidKeys);
    const bool bFModel = Options.bAutoDetectSource ? !Reader.bHasUEViewerBoneMetadata : Options.bFModel;
    TArray<FPsaBoneMapping> Mapping;
    if (!MatchBones(Reader, Mesh, Mapping, Error)) return false;
    if (!FMath::IsFinite(Options.TranslationScale) || Options.TranslationScale <= 0)
    { Error = NSLOCTEXT("UnrealPSKPSA", "InvalidTranslationScale", "Translation scale must be a finite number greater than zero.").ToString(); return false; }
    FString Folder = Destination;
    Folder.TrimStartAndEndInline();
    Folder.RemoveFromEnd(TEXT("/"));
    FText PathError;
    if (!(Folder == TEXT("/Game") || Folder.StartsWith(TEXT("/Game/"))) || !FPackageName::IsValidLongPackageName(Folder / TEXT("PSAAsset"), false, &PathError))
    { Error = NSLOCTEXT("UnrealPSKPSA", "InvalidDestination", "Destination must be a valid content path, such as /Game/Animations.").ToString(); return false; }

    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    int32 CreatedCount = 0;
    const FActorXOrientation Orientation = GetActorXMeshOrientation(Mesh);
    const FQuat4f BasisRotation = Orientation.Rotation();
    TArray<FString> Warnings;
    if (Options.bUseReferenceScale)
        Warnings.Add(NSLOCTEXT("UnrealPSKPSA", "UseReferenceScaleWarning", "Using mesh reference scale: PSA animation scale effects are ignored; position and rotation are imported normally.").ToString());
    if (Reader.bHasUEViewerBoneMetadata)
        Warnings.Add(NSLOCTEXT("UnrealPSKPSA", "UEViewerHierarchyWarning", "This PSA uses a UEViewer placeholder hierarchy. Bones are matched by name using the target mesh hierarchy; select the corresponding character mesh.").ToString());
    if (Reader.Bones.Num() > Mapping.Num())
        Warnings.Add(FText::Format(NSLOCTEXT("UnrealPSKPSA", "UnmatchedSourceBones", "Ignored {0} source bones absent from the target mesh."), Reader.Bones.Num() - Mapping.Num()).ToString());
    if (Ref.GetRawBoneNum() > Mapping.Num())
        Warnings.Add(FText::Format(NSLOCTEXT("UnrealPSKPSA", "MissingTargetTracks", "{0} target bones have no animation tracks and keep their reference pose."), Ref.GetRawBoneNum() - Mapping.Num()).ToString());
    if (Reader.InterpolatedKeyCount + Reader.CopiedKeyCount > 0)
        Warnings.Add(FText::Format(NSLOCTEXT("UnrealPSKPSA", "RepairedKeys", "Repaired {0} bone keys ({1} interpolated, {2} copied at the ends); repaired poses are estimates."), Reader.InterpolatedKeyCount + Reader.CopiedKeyCount, Reader.InterpolatedKeyCount, Reader.CopiedKeyCount).ToString());
    if (OutWarnings) *OutWarnings = Warnings;
    for (const FPsaSequenceInfo& Info : Reader.Sequences)
    {
        FString AssetName = ObjectTools::SanitizeObjectName(Info.Name);
        if (AssetName.IsEmpty() || AssetName == TEXT("None")) AssetName = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(Filename));
        FString PackageName = Folder / AssetName;
        if (!Options.bReplaceExisting)
            FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateUniqueAssetName(PackageName, TEXT(""), PackageName, AssetName);
        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();
        UObject* Existing = FindObject<UObject>(Package, *AssetName);
        UAnimSequence* Sequence = Cast<UAnimSequence>(Existing);
        if (Existing && (!Sequence || Sequence->GetSkeleton() != Mesh->GetSkeleton()))
        { Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "ExistingAssetMismatch", "The existing asset type or Skeleton does not match; not overwritten: {0}"), FText::FromString(PackageName)).ToString(); return false; }
        if (Sequence)
        {
            Sequence->WaitOnExistingCompression();
            Sequence->Modify();
        }
        else Sequence = NewObject<UAnimSequence>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        Sequence->SetSkeleton(Mesh->GetSkeleton());
        Sequence->SetPreviewMesh(Mesh);
        // PSA stores sampled poses, not UE additive settings. FModel exports baked poses.
        Sequence->AdditiveAnimType = AAT_None;
        Sequence->RefPoseSeq = nullptr;
        Sequence->RateScale = 1.0f;
        // Keep sampled local rotations, including small/zero-length terminal bones.
        // Default project compression may discard their rotation as visually insignificant.
        Sequence->BoneCompressionSettings = LoadObject<UAnimBoneCompressionSettings>(nullptr,
            TEXT("/Engine/Animation/DefaultRecorderBoneCompression.DefaultRecorderBoneCompression"));
        IAnimationDataController& Controller = Sequence->GetController();
        Controller.OpenBracket(NSLOCTEXT("UnrealPSKPSA", "ImportPSA", "Import PSA animation"), false);
        Controller.InitializeModel();
        Controller.ResetModel(false);
        // FModel writes NumFrames / SequenceLength. UE uses NumFrames - 1 frame intervals.
        double Rate = Info.AnimRate;
        // UEViewer UE4 exports also encode NumFrames / SequenceLength.
        if ((bFModel || Reader.bHasUEViewerBoneMetadata) && Info.NumRawFrames > 1) Rate *= double(Info.NumRawFrames - 1) / Info.NumRawFrames;
        if (FMath::Abs(Rate - FMath::RoundToDouble(Rate)) < 0.0001) Rate = FMath::RoundToDouble(Rate);
        const FFrameRate FrameRate(FMath::Max(1, FMath::RoundToInt(Rate * 10000.0)), 10000);
        if (Existing && Sequence->GetDataModel()->GetFrameRate() != FrameRate)
        {
            // ResetModel clears keys but keeps the model marked populated. UE only allows
            // integral rate conversions in this state. Bridge through a common sub-rate,
            // with whole-frame temporary durations, before writing the new sampled keys.
            const FFrameRate Previous = Sequence->GetDataModel()->GetFrameRate();
            Controller.SetNumberOfFrames(FFrameNumber(Previous.Numerator), false);
            Controller.SetFrameRate(FFrameRate(1, Previous.Denominator), false);
            Controller.SetNumberOfFrames(FFrameNumber(FrameRate.Denominator), false);
            Controller.SetFrameRate(FFrameRate(1, Previous.Denominator * FrameRate.Denominator), false);
        }
        Controller.SetFrameRate(FrameRate, false);
        Controller.SetNumberOfFrames(FFrameNumber(FMath::Max(1, Info.NumRawFrames - 1)), false);
        bool bTracksOK = true;
        for (const FPsaBoneMapping& Bone : Mapping)
        {
            const FName BoneName = Ref.GetBoneName(Bone.TargetIndex);
            const FVector3f ReferenceScale(Ref.GetRefBonePose()[Bone.TargetIndex].GetScale3D());
            TArray<FVector3f> Positions, Scales;
            TArray<FQuat4f> Rotations;
            Positions.Reserve(Info.NumRawFrames); Rotations.Reserve(Info.NumRawFrames); Scales.Reserve(Info.NumRawFrames);
            for (int32 Frame = 0; Frame < Info.NumRawFrames; ++Frame)
            {
                const int64 KeyIndex = (int64(Info.FirstRawFrame) + Frame) * Info.TotalBones + Bone.SourceIndex;
                const FPsaKey& Key = Reader.Keys[KeyIndex];
                FVector3f Position = Key.Position * FVector3f(1, -1, 1) * Options.TranslationScale;
                FQuat4f Rotation(Key.Rotation.X, -Key.Rotation.Y, Key.Rotation.Z, Key.Rotation.W);
                // UEViewer UE4 child tracks use the same local bind convention as
                // the PSK skeleton: restore Y, with the extra W correction on root.
                // The legacy explicit mode remains available for other exporters.
                if ((!bFModel && !Reader.bHasUEViewerBoneMetadata) || Bone.SourceIndex == 0) Rotation.W *= -1;
                // The mesh basis is baked into the root only; child local tracks inherit it.
                // Rotating every local track would accumulate the yaw down the hierarchy.
                if (Bone.SourceIndex == 0)
                {
                    Position = BasisRotation.RotateVector(Position);
                    Rotation = BasisRotation * Rotation;
                }
                Positions.Add(Position);
                Rotation.Normalize();
                if (!Rotations.IsEmpty() && (Rotations.Last() | Rotation) < 0) Rotation = Rotation * -1.0f;
                Rotations.Add(Rotation);
                FVector3f Scale = ReferenceScale;
                if (!Options.bUseReferenceScale)
                {
                    Scale = Reader.ScaleKeys.IsEmpty() ? FVector3f::OneVector : Reader.ScaleKeys[KeyIndex].Scale;
                    if (bFModel) Scale *= ReferenceScale;
                }
                Scales.Add(Scale);
            }
            bTracksOK &= Controller.AddBoneCurve(BoneName, false);
            bTracksOK &= Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, false);
        }
        Controller.NotifyPopulated();
        Controller.CloseBracket(false);
        if (!bTracksOK)
        {
            Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "WriteTracksFailed", "UE could not write animation tracks: {0}"), FText::FromString(AssetName)).ToString();
            if (!Existing) Sequence->MarkAsGarbage();
            return false;
        }
        if (!Sequence->AssetImportData) Sequence->AssetImportData = NewObject<UAssetImportData>(Sequence);
        Sequence->AssetImportData->Update(Filename);
        Sequence->PostEditChange();
        // UE 5.8's animation compiler does not implement the generic
        // FinishCompilationForObjects hook. Wait on animation compression directly
        // before saving or exposing the sequence for playback/another overwrite.
        Sequence->WaitOnExistingCompression();
        Sequence->MarkPackageDirty();
        if (!Existing) FAssetRegistryModule::AssetCreated(Sequence);
        Imported.Add(Sequence);
        ++CreatedCount;
        if (Options.bSaveAssets)
        {
            const FString SavePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(SavePath), true);
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            if (!UPackage::SavePackage(Package, Sequence, *SavePath, SaveArgs))
            { Error = FText::Format(NSLOCTEXT("UnrealPSKPSA", "SaveAnimationFailed", "The animation was created but could not be saved. Check the output folder and save manually: {0}"), FText::FromString(PackageName)).ToString(); return false; }
        }
    }
    Summary = FText::Format(NSLOCTEXT("UnrealPSKPSA", "FileSummary", "{0}: {1} animations; matched {2}/{3} target bones."), FText::FromString(FPaths::GetCleanFilename(Filename)), CreatedCount, Mapping.Num(), Ref.GetRawBoneNum()).ToString();
    Summary += TEXT(" ") + FText::Format(NSLOCTEXT("UnrealPSKPSA", "OrientationSummary", "Orientation: {0}."), FText::FromString(Orientation.Description())).ToString();
    Summary += TEXT(" ") + FText::Format(NSLOCTEXT("UnrealPSKPSA", "SourceSummary", "Source: {0} {1}."), FText::FromString(bFModel ? TEXT("FModel") : TEXT("UEViewer / ActorX")), Options.bAutoDetectSource ? NSLOCTEXT("UnrealPSKPSA", "AutomaticSource", "(auto-detected)") : NSLOCTEXT("UnrealPSKPSA", "ManualSource", "(manual)")).ToString();
    // Keep warnings in the legacy summary for callers that do not request separate diagnostics.
    if (!OutWarnings && !Warnings.IsEmpty()) Summary += FString::Join(Warnings, TEXT(""));
    return true;
}
