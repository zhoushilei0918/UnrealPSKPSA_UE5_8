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
    if (!Mesh || !Mesh->GetSkeleton()) { Error = TEXT("请选择具有 Skeleton 的 Skeletal Mesh（骨骼网格体）。"); return false; }
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    if (Ref.GetRawBoneNum() == 0 || Ref.GetBoneName(0) != FName(UTF8_TO_TCHAR(Reader.Bones[0].Name)))
    { Error = TEXT("目标网格与 PSA 的根骨骼名称不同，请选择对应的网格。"); return false; }
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
            Conflicts.Add(FString::Printf(TEXT("%s（PSA 父骨骼：%s；目标：%s）"), *Name.ToString(), *SourceParent.ToString(), *TargetParent.ToString()));
        Mapping.Add({SourceIndex, TargetIndex});
    }
    if (!Conflicts.IsEmpty())
    {
        Error = TEXT("骨骼层级不兼容，需要对应的源网格或先进行动画重定向：\n") + FString::Join(Conflicts, TEXT("\n"));
        Mapping.Reset();
        return false;
    }
    if (Mapping.Num() < FMath::Min(2, Ref.GetRawBoneNum())) { Error = TEXT("没有足够的同名骨骼可匹配。"); return false; }
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
    { Error = TEXT("位置缩放必须是大于零的有限数值。"); return false; }
    FString Folder = Destination;
    Folder.TrimStartAndEndInline();
    Folder.RemoveFromEnd(TEXT("/"));
    FText PathError;
    if (!(Folder == TEXT("/Game") || Folder.StartsWith(TEXT("/Game/"))) || !FPackageName::IsValidLongPackageName(Folder / TEXT("PSAAsset"), false, &PathError))
    { Error = TEXT("保存路径必须是有效的内容路径，例如 /Game/Animations。"); return false; }

    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    int32 CreatedCount = 0;
    const FActorXOrientation Orientation = GetActorXMeshOrientation(Mesh);
    const FQuat4f BasisRotation = Orientation.Rotation();
    TArray<FString> Warnings;
    if (Reader.bHasUEViewerBoneMetadata)
        Warnings.Add(TEXT("此 PSA 使用 UEViewer 占位层级，按骨骼名称匹配并沿用目标网格层级；请确保选择对应人物的模型。"));
    if (Reader.Bones.Num() > Mapping.Num())
        Warnings.Add(FString::Printf(TEXT("忽略 %d 根目标网格中不存在的源骨骼。"), Reader.Bones.Num() - Mapping.Num()));
    if (Ref.GetRawBoneNum() > Mapping.Num())
        Warnings.Add(FString::Printf(TEXT("%d 根目标骨骼缺失动画轨道，保持参考姿态。"), Ref.GetRawBoneNum() - Mapping.Num()));
    if (Reader.InterpolatedKeyCount + Reader.CopiedKeyCount > 0)
        Warnings.Add(FString::Printf(TEXT("已修复 %d 个骨骼关键帧（中间插值 %d，首尾复制 %d）；补帧为估算姿态。"),
            Reader.InterpolatedKeyCount + Reader.CopiedKeyCount, Reader.InterpolatedKeyCount, Reader.CopiedKeyCount));
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
        { Error = TEXT("同名资产的类型或 Skeleton 不匹配，未覆盖：") + PackageName; return false; }
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
                const FVector3f ReferenceScale(Ref.GetRefBonePose()[Bone.TargetIndex].GetScale3D());
                FVector3f Scale = Reader.ScaleKeys.IsEmpty() ? FVector3f::OneVector : Reader.ScaleKeys[KeyIndex].Scale;
                if (bFModel) Scale *= ReferenceScale;
                Scales.Add(Scale);
            }
            bTracksOK &= Controller.AddBoneCurve(BoneName, false);
            bTracksOK &= Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, false);
        }
        Controller.NotifyPopulated();
        Controller.CloseBracket(false);
        if (!bTracksOK)
        {
            Error = TEXT("UE 写入动画轨道失败：") + AssetName;
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
            { Error = TEXT("动画已生成，但保存失败，请检查输出目录后手动保存：") + PackageName; return false; }
        }
    }
    Summary = FString::Printf(TEXT("%s：%d 个动画；匹配 %d/%d 根目标骨骼。"),
        *FPaths::GetCleanFilename(Filename), CreatedCount, Mapping.Num(), Ref.GetRawBoneNum());
    Summary += TEXT("朝向：") + Orientation.Description() + TEXT("。");
    Summary += FString::Printf(TEXT("来源：%s%s。"), bFModel ? TEXT("FModel") : TEXT("UEViewer / ActorX"), Options.bAutoDetectSource ? TEXT("（自动识别）") : TEXT("（手动选择）"));
    // Keep warnings in the legacy summary for callers that do not request separate diagnostics.
    if (!OutWarnings && !Warnings.IsEmpty()) Summary += FString::Join(Warnings, TEXT(""));
    return true;
}
