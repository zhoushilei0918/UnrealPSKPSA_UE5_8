#include "PsaImportWindow.h"
#include "PsaImportSettings.h"
#include "PsaImporter.h"
#include "ActorXImportSettings.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    TArray<TWeakPtr<SWindow>> ImportWindows;

    class SPsaImportPanel : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SPsaImportPanel) {} SLATE_ARGUMENT(USkeletalMesh*, TargetMesh) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Settings.Reset(NewObject<UPsaImportSettings>());
            Settings->TargetMesh = Args._TargetMesh;
            if (Args._TargetMesh) Settings->Destination = FPackageName::GetLongPackagePath(Args._TargetMesh->GetOutermost()->GetName()) / TEXT("Animations");
            FDetailsViewArgs DetailsArgs;
            DetailsArgs.bAllowSearch = false;
            DetailsArgs.bHideSelectionTip = true;
            DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
            auto Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
            Details->SetObject(Settings.Get());
            ChildSlot
            [
                SNew(SBorder).Padding(16)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("选择已导入 UE 的骨骼网格，再选择 PSA 动画。网格可来自 PSK 或 FBX。"))).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight()[Details]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                    {
                        if (!Settings->TargetMesh) return FText::FromString(TEXT("动画朝向：选择目标网格后自动读取，网格与动画保持一致。"));
                        if (const auto* Data = Cast<UActorXMeshImportData>(Settings->TargetMesh->GetAssetImportData()))
                            return FText::FromString(TEXT("动画朝向：沿用目标网格（") + Data->Orientation.Description() + TEXT("）。如需改变，请先用新朝向导入模型，再导入动画。"));
                        return FText::FromString(TEXT("动画朝向：目标网格未记录 ActorX 朝向，保持原有坐标。旧模型如需改为 +Y，请重新导入 PSK 并选择目标朝向。"));
                    })]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                        [SNew(SButton).Text(FText::FromString(TEXT("选择 PSA 文件…"))).OnClicked(this, &SPsaImportPanel::ChooseFiles)]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                        [SNew(SButton).Text(FText::FromString(TEXT("从文件夹添加…"))).OnClicked(this, &SPsaImportPanel::ChooseFolder)]
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SButton).Text(FText::FromString(TEXT("清空"))).OnClicked_Lambda([this] { Files.Reset(); RefreshFiles(); return FReply::Handled(); })]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                    [SAssignNew(FileCount, STextBlock).Text(FText::FromString(TEXT("尚未选择 PSA 文件")))]
                    + SVerticalBox::Slot().FillHeight(0.35f)
                    [SAssignNew(FileList, SMultiLineEditableTextBox).IsReadOnly(true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
                        [SNew(SButton).Text(FText::FromString(TEXT("导入动画"))).IsEnabled_Lambda([this] { return Settings->TargetMesh != nullptr && !Files.IsEmpty(); }).OnClicked(this, &SPsaImportPanel::Import)]
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SButton).Text(FText::FromString(TEXT("显示导入结果"))).IsEnabled_Lambda([this] { return !LastImported.IsEmpty(); }).OnClicked_Lambda([this]
                        {
                            TArray<UObject*> Objects;
                            for (auto& Asset : LastImported) if (Asset.IsValid()) Objects.Add(Asset.Get());
                            GEditor->SyncBrowserToObjects(Objects);
                            return FReply::Handled();
                        })]
                    ]
                    + SVerticalBox::Slot().FillHeight(0.65f)
                    [SAssignNew(Results, SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true).Text(FText::FromString(TEXT("同名骨骼自动匹配；缺失轨道保持参考姿态；层级不兼容会报错。\nPSA 不包含 UE 通知、曲线和完整增量动画设置，动画按采样姿态导入。")))]
                ]
            ];
        }
    private:
        TStrongObjectPtr<UPsaImportSettings> Settings;
        TArray<FString> Files;
        TArray<TWeakObjectPtr<UAnimSequence>> LastImported;
        TSharedPtr<SMultiLineEditableTextBox> FileList, Results;
        TSharedPtr<STextBlock> FileCount;
        FString LastDirectory;
        void RefreshFiles()
        {
            Files.Sort();
            FileList->SetText(FText::FromString(FString::Join(Files, TEXT("\n"))));
            FileCount->SetText(FText::FromString(FString::Printf(TEXT("已选择 %d 个 PSA 文件"), Files.Num())));
        }
        FReply ChooseFiles()
        {
            if (IDesktopPlatform* Desktop = FDesktopPlatformModule::Get())
            {
                TArray<FString> Selected;
                if (Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()), TEXT("选择 PSA 动画"), LastDirectory, TEXT(""), TEXT("ActorX Animation (*.psa)|*.psa"), EFileDialogFlags::Multiple, Selected))
                {
                    for (const FString& File : Selected) Files.AddUnique(FPaths::ConvertRelativePathToFull(File));
                    if (!Selected.IsEmpty()) LastDirectory = FPaths::GetPath(Selected[0]);
                    RefreshFiles();
                }
            }
            return FReply::Handled();
        }
        FReply ChooseFolder()
        {
            if (IDesktopPlatform* Desktop = FDesktopPlatformModule::Get())
            {
                FString Directory;
                if (Desktop->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()), TEXT("选择包含 PSA 的文件夹（仅当前目录）"), LastDirectory, Directory))
                {
                    LastDirectory = Directory;
                    TArray<FString> Names;
                    IFileManager::Get().FindFiles(Names, *(Directory / TEXT("*.psa")), true, false);
                    for (const FString& Name : Names) Files.AddUnique(FPaths::ConvertRelativePathToFull(Directory / Name));
                    RefreshFiles();
                }
            }
            return FReply::Handled();
        }
        FReply Import()
        {
            FPsaImportOptions Options;
            Options.bFModel = Settings->Source == EPsaSource::FModel;
            Options.bReplaceExisting = Settings->bReplaceExisting;
            Options.bRepairInvalidKeys = Settings->bRepairInvalidKeys;
            Options.TranslationScale = Settings->TranslationScale;
            LastImported.Reset();
            TArray<FString> Messages;
            int32 Succeeded = 0, Failed = 0;
            bool bCanceled = false;
            FScopedSlowTask Progress(Files.Num(), FText::FromString(TEXT("正在导入 PSA 动画")));
            Progress.MakeDialog(true);
            for (const FString& File : Files)
            {
                if (Progress.ShouldCancel()) { bCanceled = true; break; }
                Progress.EnterProgressFrame(1, FText::FromString(FPaths::GetCleanFilename(File)));
                TArray<UAnimSequence*> Imported;
                FString Summary, Error;
                if (FPsaImporter::ImportFile(File, Settings->TargetMesh, Settings->Destination, Options, Imported, Summary, Error))
                { ++Succeeded; Messages.Add(TEXT("成功：") + Summary); }
                else { ++Failed; Messages.Add(TEXT("失败：") + FPaths::GetCleanFilename(File) + TEXT("\n") + Error); }
                for (auto* Asset : Imported) LastImported.Add(Asset);
            }
            const FString Heading = FString::Printf(TEXT("%s成功 %d 个文件，失败 %d 个文件，生成 %d 个动画。\n保存路径：%s\n\n"),
                bCanceled ? TEXT("已取消剩余文件。") : TEXT("导入完成。"), Succeeded, Failed, LastImported.Num(), *Settings->Destination);
            Results->SetText(FText::FromString(Heading + FString::Join(Messages, TEXT("\n\n"))));
            return FReply::Handled();
        }
    };
}

void OpenPsaImportWindow(USkeletalMesh* TargetMesh)
{
    auto Window = SNew(SWindow).Title(FText::FromString(TEXT("导入 PSA 动画"))).ClientSize(FVector2D(760, 720)).SupportsMaximize(true).SupportsMinimize(false)
        [SNew(SPsaImportPanel).TargetMesh(TargetMesh)];
    ImportWindows.Add(Window);
    FSlateApplication::Get().AddWindow(Window);
}

void ClosePsaImportWindows()
{
    for (auto& Window : ImportWindows) if (auto Pinned = Window.Pin()) Pinned->RequestDestroyWindow();
    ImportWindows.Reset();
}
