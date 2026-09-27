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
    TSharedRef<SWindow> Window = SNew(SWindow).Title(FText::FromString(TEXT("PSK / PSKX 导入朝向")))
        .ClientSize(FVector2D(600, 320)).SupportsMinimize(false).SupportsMaximize(false);
    Window->SetContent(
        SNew(SBorder).Padding(16)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [SNew(STextBlock).Text(FText::FromString(FPaths::GetCleanFilename(Filename)))]
            + SVerticalBox::Slot().AutoHeight()[Details]
            + SVerticalBox::Slot().FillHeight(1).Padding(0,12)
            [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("身体、头发、服装等部件请使用相同的源正面和目标朝向。网格、法线、形变与骨骼将整体旋转；PSA 动画自动沿用目标网格的设置。\n修改已有模型的朝向后，需重新导入对应 PSA 动画。")))]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0)
                [SNew(SButton).Text(FText::FromString(TEXT("导入"))).OnClicked_Lambda([&] { bAccepted = true; Window->RequestDestroyWindow(); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).Text(FText::FromString(TEXT("取消"))).OnClicked_Lambda([&] { Window->RequestDestroyWindow(); return FReply::Handled(); })]
            ]
        ]);
    GEditor->EditorAddModalWindow(Window);
    if (bAccepted) Orientation = Settings->Orientation;
    return bAccepted;
}
