#include "ActorXImportWindow.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

bool ShowActorXImportOptions(FActorXOrientation& Orientation, const FString& Filename)
{
    TStrongObjectPtr<UActorXImportSettings> Settings(NewObject<UActorXImportSettings>());
    Settings->Orientation = Orientation;
    FDetailsViewArgs Args;
    Args.bAllowSearch = false;
    Args.bHideSelectionTip = true;
    Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    auto Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
    Details->SetObject(Settings.Get());
    bool bAccepted = false;
    TSharedRef<SWindow> Window = SNew(SWindow).Title(NSLOCTEXT("UnrealPSKPSA", "MeshPanelTitle", "PSK / PSKX Import Orientation"))
        .ClientSize(FVector2D(600, 320)).SupportsMinimize(false).SupportsMaximize(false);
    Window->SetContent(
        SNew(SBorder).Padding(16)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [SNew(STextBlock).Text(FText::FromString(FPaths::GetCleanFilename(Filename)))]
            + SVerticalBox::Slot().AutoHeight()[Details]
            + SVerticalBox::Slot().FillHeight(1).Padding(0,12)
            [SNew(STextBlock).AutoWrapText(true).Text(NSLOCTEXT("UnrealPSKPSA", "MeshOrientationNote", "Use the same source and target forward directions for all body, hair, and clothing parts. Meshes, normals, morphs, and bones rotate together; PSA animations inherit the target mesh settings.\nAfter changing an existing mesh orientation, import its PSA animations again."))]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0)
                [SNew(SButton).Text(NSLOCTEXT("UnrealPSKPSA", "Import", "Import")).OnClicked_Lambda([&] { bAccepted = true; Window->RequestDestroyWindow(); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).Text(NSLOCTEXT("UnrealPSKPSA", "Cancel", "Cancel")).OnClicked_Lambda([&] { Window->RequestDestroyWindow(); return FReply::Handled(); })]
            ]
        ]);
    GEditor->EditorAddModalWindow(Window);
    if (bAccepted) Orientation = Settings->Orientation;
    return bAccepted;
}
