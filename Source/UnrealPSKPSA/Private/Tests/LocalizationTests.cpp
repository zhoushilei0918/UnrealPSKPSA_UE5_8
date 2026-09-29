#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "PsaImportSettings.h"
#include "ActorXImportSettings.h"
#include "PsaImporter.h"
#include "PsaImportLog.h"
#include "PsaReader.h"
#include "ObjectEditorUtils.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorXLocalizationTest, "UnrealPSKPSA.Localization.SwitchLanguages", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActorXLocalizationTest::RunTest(const FString& Parameters)
{
    auto& I18N = FInternationalization::Get();
    const FString OriginalLanguage = I18N.GetCurrentLanguage()->GetName();
    ON_SCOPE_EXIT { I18N.SetCurrentLanguage(OriginalLanguage); FTextLocalizationManager::Get().WaitForAsyncTasks(); };
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealPSKPSA"));
    if (!TestTrue(TEXT("Plugin is registered"), Plugin.IsValid())) return false;
    FString Json;
    if (!TestTrue(TEXT("Read shipped Chinese archive"), FFileHelper::LoadFileToString(Json, *(Plugin->GetContentDir() / TEXT("Localization/UnrealPSKPSA/zh-Hans/UnrealPSKPSA.archive"))))) return false;
    TSharedPtr<FJsonObject> Archive;
    if (!TestTrue(TEXT("Parse translation catalog"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Archive))) return false;
    const FQuat4f OriginalRotation = FActorXOrientation().Rotation();
    for (const TCHAR* Language : {TEXT("en"), TEXT("zh-Hans"), TEXT("zh-CN"), TEXT("en-US"), TEXT("ja"), TEXT("en")})
    {
        TestTrue(TEXT("Set editor language"), I18N.SetCurrentLanguage(Language));
        FTextLocalizationManager::Get().WaitForAsyncTasks();
        const bool bChinese = FString(Language).StartsWith(TEXT("zh"));
        int32 Checked = 0;
        for (const auto& NamespaceValue : Archive->GetArrayField(TEXT("Subnamespaces")))
        {
            const auto Namespace = NamespaceValue->AsObject();
            const FString NamespaceName = Namespace->GetStringField(TEXT("Namespace"));
            // UE's shared category names may also be translated by the engine.
            if (NamespaceName == TEXT("UObjectCategory")) continue;
            for (const auto& EntryValue : Namespace->GetArrayField(TEXT("Children")))
            {
                const auto Entry = EntryValue->AsObject();
                const FString Key = Entry->GetStringField(TEXT("Key"));
                const FString Source = Entry->GetObjectField(TEXT("Source"))->GetStringField(TEXT("Text"));
                const FString Translation = Entry->GetObjectField(TEXT("Translation"))->GetStringField(TEXT("Text"));
                TestFalse(*(TEXT("Complete translation: ") + Key), Translation.IsEmpty());
                FText Localized;
                if (TestTrue(*(TEXT("Loaded localization key: ") + Key), FText::FindTextInLiveTable_Advanced(NamespaceName, Key, Localized, &Source)))
                    TestEqual(*(FString(Language) + TEXT(": ") + Key), Localized.ToString(), bChinese ? Translation : Source);
                ++Checked;
            }
        }
        TestTrue(TEXT("Catalog includes UI, metadata and diagnostics"), Checked > 130);
        const auto* ScaleProperty = FindFProperty<FProperty>(UPsaImportSettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UPsaImportSettings, bUseReferenceScale));
        if (!TestNotNull(TEXT("Reference scale property"), ScaleProperty)) return false;
        TestEqual(TEXT("Reflection property display name"), ScaleProperty->GetDisplayNameText().ToString(), FString(bChinese ? TEXT("使用模型参考缩放") : TEXT("Use Mesh Reference Scale")));
        TestTrue(TEXT("Reflection tooltip"), ScaleProperty->GetToolTipText().ToString().Contains(bChinese ? TEXT("异常拉伸") : TEXT("unusually stretched")));
        TestEqual(TEXT("Enum display name"), StaticEnum<EPsaSource>()->GetDisplayNameTextByValue(int64(EPsaSource::Auto)).ToString(), FString(bChinese ? TEXT("自动识别（推荐）") : TEXT("Auto Detect (Recommended)")));
        TestEqual(TEXT("Log label"), FPsaImportLog::Label(EPsaImportLogLevel::Warning).ToString(), FString(bChinese ? TEXT("警告") : TEXT("Warning")));
        FPsaImportLog Log;
        TestEqual(TEXT("Empty log"), Log.DisplayText(), FString(bChinese ? TEXT("尚无导入日志。") : TEXT("No import log yet.")));
        const FPsaReader MissingFile(Plugin->GetBaseDir() / TEXT("MissingLocalizationTest.psa"));
        TestEqual(TEXT("Reader error follows language"), MissingFile.Error, FString(bChinese ? TEXT("无法读取 PSA 文件。") : TEXT("Cannot read the PSA file.")));
        TestTrue(TEXT("Orientation summary follows language"), FActorXOrientation().Description().StartsWith(bChinese ? TEXT("源 ") : TEXT("Source ")));
        TestTrue(TEXT("Language does not alter orientation"), FActorXOrientation().Rotation().Equals(OriginalRotation));
        TestFalse(TEXT("Language does not enable key repair"), GetDefault<UPsaImportSettings>()->bRepairInvalidKeys);
        TestFalse(TEXT("Language does not enable scale override"), GetDefault<UPsaImportSettings>()->bUseReferenceScale);
    }
    return true;
}
#endif
