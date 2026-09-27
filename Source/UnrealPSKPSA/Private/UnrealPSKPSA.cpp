#include "UnrealPSKPSA.h"
#include "PsaImportWindow.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/SkeletalMesh.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY(LogUnrealPSKPSA);

void FUnrealPSKPSAModule::StartupModule()
{
    if (!IsRunningCommandlet())
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealPSKPSAModule::RegisterMenus));
}

void FUnrealPSKPSAModule::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    UToolMenu* Tools = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    Tools->FindOrAddSection("UnrealPSKPSA").AddMenuEntry("OpenPsaImporter",
        FText::FromString(TEXT("导入 PSA 动画…")), FText::FromString(TEXT("选择骨骼网格并导入一个或多个 PSA 动画文件。")),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([] { OpenPsaImportWindow(); })));

    UToolMenu* MeshMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SkeletalMesh");
    MeshMenu->AddDynamicSection("UnrealPSKPSA", FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
    {
        const auto* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();
        if (!Context || Context->SelectedAssets.Num() != 1) return;
        const FAssetData MeshAsset = Context->SelectedAssets[0];
        Menu->FindOrAddSection("GetAssetActions").AddMenuEntry("ImportPsaForMesh",
            FText::FromString(TEXT("导入 PSA 动画…")), FText::FromString(TEXT("使用此骨骼网格的 Skeleton 导入 PSA 动画。")),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([MeshAsset]
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
}

IMPLEMENT_MODULE(FUnrealPSKPSAModule, UnrealPSKPSA)
