#include "UnrealPSKPSA.h"
#include "PsaImportWindow.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/SkeletalMesh.h"
#include "ToolMenus.h"
#include "Interfaces/IPluginManager.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

DEFINE_LOG_CATEGORY(LogUnrealPSKPSA);

void FUnrealPSKPSAModule::StartupModule()
{
    if (!IsRunningCommandlet())
    {
        Style = MakeShared<FSlateStyleSet>("UnrealPSKPSAStyle");
        Style->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("UnrealPSKPSA"))->GetBaseDir() / TEXT("Resources"));
        Style->Set("UnrealPSKPSA.OpenImporter", new FSlateImageBrush(Style->RootToContentDir(TEXT("Icon128.png")), FVector2D(40, 40)));
        Style->Set("UnrealPSKPSA.OpenImporter.Small", new FSlateImageBrush(Style->RootToContentDir(TEXT("Icon128.png")), FVector2D(20, 20)));
        FSlateStyleRegistry::RegisterSlateStyle(*Style);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealPSKPSAModule::RegisterMenus));
    }
}

void FUnrealPSKPSAModule::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    UToolMenu* Tools = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    Tools->FindOrAddSection("UnrealPSKPSA").AddMenuEntry("OpenPsaImporter",
        FText::FromString(TEXT("导入 PSA 动画…")), FText::FromString(TEXT("选择骨骼网格并导入一个或多个 PSA 动画文件。")),
        FSlateIcon("UnrealPSKPSAStyle", "UnrealPSKPSA.OpenImporter"), FUIAction(FExecuteAction::CreateLambda([] { OpenPsaImportWindow(); })));

    UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
    Toolbar->FindOrAddSection("UnrealPSKPSA").AddEntry(FToolMenuEntry::InitToolBarButton(
        "OpenPsaImporterToolbar", FUIAction(FExecuteAction::CreateLambda([] { OpenPsaImportWindow(); })),
        FText::FromString(TEXT("PSA 动画")), FText::FromString(TEXT("打开 PSA 动画导入面板，选择骨骼网格并导入动画。")),
        FSlateIcon("UnrealPSKPSAStyle", "UnrealPSKPSA.OpenImporter")));

    UToolMenu* MeshMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SkeletalMesh");
    MeshMenu->AddDynamicSection("UnrealPSKPSA", FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
    {
        const auto* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();
        if (!Context || Context->SelectedAssets.Num() != 1) return;
        const FAssetData MeshAsset = Context->SelectedAssets[0];
        Menu->FindOrAddSection("GetAssetActions").AddMenuEntry("ImportPsaForMesh",
            FText::FromString(TEXT("导入 PSA 动画…")), FText::FromString(TEXT("使用此骨骼网格的 Skeleton 导入 PSA 动画。")),
            FSlateIcon("UnrealPSKPSAStyle", "UnrealPSKPSA.OpenImporter"), FUIAction(FExecuteAction::CreateLambda([MeshAsset]
            {
                if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(MeshAsset.GetAsset())) OpenPsaImportWindow(Mesh);
            })));
    }));
}

void FUnrealPSKPSAModule::ShutdownModule()
{
    ClosePsaImportWindows();
    UToolMenus::UnRegisterStartupCallback(this);
    if (UToolMenus::IsToolMenuUIEnabled()) UToolMenus::UnregisterOwner(this);
    if (Style.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
        Style.Reset();
    }
}

IMPLEMENT_MODULE(FUnrealPSKPSAModule, UnrealPSKPSA)
