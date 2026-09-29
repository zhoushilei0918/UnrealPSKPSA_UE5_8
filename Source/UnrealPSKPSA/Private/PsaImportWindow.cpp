#include "PsaImportWindow.h"
#include "PsaImportSettings.h"
#include "PsaImporter.h"
#include "PsaImportLog.h"
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
#include "Widgets/Input/SCheckBox.h"
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
                    [SNew(STextBlock).Text(NSLOCTEXT("UnrealPSKPSA", "PanelIntroduction", "Select a skeletal mesh imported from PSK or FBX, then choose PSA animations.")).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight()[Details]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                    {
                        if (!Settings->TargetMesh) return NSLOCTEXT("UnrealPSKPSA", "OrientationNoMesh", "Animation orientation: select a target mesh to inherit its orientation.");
                        if (const auto* Data = Cast<UActorXMeshImportData>(Settings->TargetMesh->GetAssetImportData()))
                            return FText::Format(NSLOCTEXT("UnrealPSKPSA", "OrientationInherited", "Animation orientation: inherit the target mesh ({0}). To change it, import the mesh with the new orientation, then import the animation again."), FText::FromString(Data->Orientation.Description()));
                        return NSLOCTEXT("UnrealPSKPSA", "OrientationNoMetadata", "Animation orientation: this mesh has no ActorX orientation metadata; keep its original coordinates. To change an older mesh to +Y, import the PSK again and choose the target direction.");
                    })]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                        [SNew(SButton).Text(NSLOCTEXT("UnrealPSKPSA", "ChooseFiles", "Choose PSA Files…")).OnClicked(this, &SPsaImportPanel::ChooseFiles)]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                        [SNew(SButton).Text(NSLOCTEXT("UnrealPSKPSA", "ChooseFolder", "Add from Folder…")).OnClicked(this, &SPsaImportPanel::ChooseFolder)]
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SButton).Text(NSLOCTEXT("UnrealPSKPSA", "Clear", "Clear")).OnClicked_Lambda([this] { Files.Reset(); RefreshFiles(); return FReply::Handled(); })]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                    [SAssignNew(FileCount, STextBlock).Text(NSLOCTEXT("UnrealPSKPSA", "NoFiles", "No PSA files selected"))]
                    + SVerticalBox::Slot().FillHeight(0.35f)
                    [SAssignNew(FileList, SMultiLineEditableTextBox).IsReadOnly(true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
                        [SNew(SButton).Text(NSLOCTEXT("UnrealPSKPSA", "ImportAnimations", "Import Animations")).IsEnabled_Lambda([this] { return Settings->TargetMesh != nullptr && !Files.IsEmpty(); }).OnClicked(this, &SPsaImportPanel::Import)]
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SButton).Text(NSLOCTEXT("UnrealPSKPSA", "ShowResults", "Show Imported Assets")).IsEnabled_Lambda([this] { return !LastImported.IsEmpty(); }).OnClicked_Lambda([this]
                        {
                            TArray<UObject*> Objects;
                            for (auto& Asset : LastImported) if (Asset.IsValid()) Objects.Add(Asset.Get());
                            GEditor->SyncBrowserToObjects(Objects);
                            return FReply::Handled();
                        })]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                    [SAssignNew(ResultSummary, STextBlock).AutoWrapText(true).Text(NSLOCTEXT("UnrealPSKPSA", "SampledPosesNote", "PSA does not include UE notifies, curves, or complete additive settings. Animations are imported as sampled poses."))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
                        [SNew(STextBlock).Text(NSLOCTEXT("UnrealPSKPSA", "ShowLog", "Show log:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)[MakeLogFilter(EPsaImportLogLevel::Error)]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)[MakeLogFilter(EPsaImportLogLevel::Warning)]
                        + SHorizontalBox::Slot().AutoWidth()[MakeLogFilter(EPsaImportLogLevel::Success)]
                    ]
                    + SVerticalBox::Slot().FillHeight(0.65f)
                    [SAssignNew(Results, SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true).Text(FText::FromString(ImportLog.DisplayText()))]
                ]
            ];
        }
    private:
        TStrongObjectPtr<UPsaImportSettings> Settings;
        TArray<FString> Files;
        TArray<TWeakObjectPtr<UAnimSequence>> LastImported;
        TSharedPtr<SMultiLineEditableTextBox> FileList, Results;
        TSharedPtr<STextBlock> FileCount, ResultSummary;
        FPsaImportLog ImportLog;
        FString LastDirectory;
        TSharedRef<SWidget> MakeLogFilter(EPsaImportLogLevel Level)
        {
            const FLinearColor Color = Level == EPsaImportLogLevel::Error ? FLinearColor(1.0f, 0.3f, 0.3f) :
                Level == EPsaImportLogLevel::Warning ? FLinearColor(1.0f, 0.7f, 0.15f) : FLinearColor(0.3f, 0.85f, 0.4f);
            return SNew(SCheckBox)
                .ToolTipText(NSLOCTEXT("UnrealPSKPSA", "LogFilterTooltip", "Show or hide this log level. Hiding entries does not discard them. The number is the entry count for this import."))
                .IsChecked_Lambda([this, Level] { return ImportLog.IsVisible(Level) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this, Level](ECheckBoxState State)
                {
                    ImportLog.SetVisible(Level, State == ECheckBoxState::Checked);
                    Results->SetText(FText::FromString(ImportLog.DisplayText()));
                })
                [SNew(STextBlock).ColorAndOpacity(Color).Text_Lambda([this, Level]
                {
                    return FText::Format(NSLOCTEXT("UnrealPSKPSA", "LogFilterCount", "{0} ({1})"), FPsaImportLog::Label(Level), ImportLog.Count(Level));
                })];
        }
        void RefreshFiles()
        {
            Files.Sort();
            FileList->SetText(FText::FromString(FString::Join(Files, TEXT("\n"))));
            FileCount->SetText(FText::Format(NSLOCTEXT("UnrealPSKPSA", "SelectedFiles", "{0} PSA files selected"), Files.Num()));
        }
        FReply ChooseFiles()
        {
            if (IDesktopPlatform* Desktop = FDesktopPlatformModule::Get())
            {
                TArray<FString> Selected;
                if (Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()), NSLOCTEXT("UnrealPSKPSA", "ChooseFilesTitle", "Choose PSA Animations").ToString(), LastDirectory, TEXT(""), TEXT("ActorX Animation (*.psa)|*.psa"), EFileDialogFlags::Multiple, Selected))
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
                if (Desktop->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()), NSLOCTEXT("UnrealPSKPSA", "ChooseFolderTitle", "Choose a PSA Folder (Current Directory Only)").ToString(), LastDirectory, Directory))
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
            Options.bAutoDetectSource = Settings->Source == EPsaSource::Auto;
            Options.bFModel = Settings->Source == EPsaSource::FModel;
            Options.bReplaceExisting = Settings->bReplaceExisting;
            Options.bRepairInvalidKeys = Settings->bRepairInvalidKeys;
            Options.bUseReferenceScale = Settings->bUseReferenceScale;
            Options.TranslationScale = Settings->TranslationScale;
            LastImported.Reset();
            ImportLog.Reset();
            int32 Succeeded = 0, Failed = 0;
            bool bCanceled = false;
            FScopedSlowTask Progress(Files.Num(), NSLOCTEXT("UnrealPSKPSA", "ImportProgress", "Importing PSA animations"));
            Progress.MakeDialog(true);
            for (const FString& File : Files)
            {
                if (Progress.ShouldCancel()) { bCanceled = true; break; }
                Progress.EnterProgressFrame(1, FText::FromString(FPaths::GetCleanFilename(File)));
                TArray<UAnimSequence*> Imported;
                TArray<FString> Warnings;
                FString Summary, Error;
                const bool bImported = FPsaImporter::ImportFile(File, Settings->TargetMesh, Settings->Destination, Options, Imported, Summary, Error, &Warnings);
                for (const FString& Warning : Warnings)
                    ImportLog.Add(EPsaImportLogLevel::Warning, FPaths::GetCleanFilename(File) + TEXT("\n") + Warning);
                if (bImported)
                { ++Succeeded; ImportLog.Add(EPsaImportLogLevel::Success, Summary); }
                else
                {
                    ++Failed;
                    ImportLog.Add(EPsaImportLogLevel::Error, FPaths::GetCleanFilename(File) + TEXT("\n") + Error);
                    if (!Imported.IsEmpty())
                        ImportLog.Add(EPsaImportLogLevel::Warning, FText::Format(NSLOCTEXT("UnrealPSKPSA", "PartialImport", "{0}: import was incomplete; {1} animations were created. Check the results and save status."), FText::FromString(FPaths::GetCleanFilename(File)), Imported.Num()).ToString());
                }
                for (auto* Asset : Imported) LastImported.Add(Asset);
            }
            if (bCanceled)
                ImportLog.Add(EPsaImportLogLevel::Warning, FText::Format(NSLOCTEXT("UnrealPSKPSA", "ImportCanceled", "Import canceled; {0} files were not processed."), Files.Num() - Succeeded - Failed).ToString());
            const FText Heading = FText::Format(NSLOCTEXT("UnrealPSKPSA", "BatchSummary", "{0} {1} files succeeded, {2} failed; {3} animations created.\nDestination: {4}"), bCanceled ? NSLOCTEXT("UnrealPSKPSA", "RemainingCanceled", "Remaining files canceled.") : NSLOCTEXT("UnrealPSKPSA", "ImportComplete", "Import complete."), Succeeded, Failed, LastImported.Num(), FText::FromString(Settings->Destination));
            ResultSummary->SetText(Heading);
            Results->SetText(FText::FromString(ImportLog.DisplayText()));
            return FReply::Handled();
        }
    };
}

void OpenPsaImportWindow(USkeletalMesh* TargetMesh)
{
    auto Window = SNew(SWindow).Title(NSLOCTEXT("UnrealPSKPSA", "PanelTitle", "Import PSA Animations")).ClientSize(FVector2D(760, 720)).SupportsMaximize(true).SupportsMinimize(false)
        [SNew(SPsaImportPanel).TargetMesh(TargetMesh)];
    ImportWindows.Add(Window);
    FSlateApplication::Get().AddWindow(Window);
}

void ClosePsaImportWindows()
{
    for (auto& Window : ImportWindows) if (auto Pinned = Window.Pin()) Pinned->RequestDestroyWindow();
    ImportWindows.Reset();
}
