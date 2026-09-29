#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PsaImportLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPsaImportLogTest, "UnrealPSKPSA.PSA.LogFiltering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPsaImportLogTest::RunTest(const FString& Parameters)
{
    FPsaImportLog Log;
    Log.Add(EPsaImportLogLevel::Error, TEXT("broken.psa\n文件被截断。"));
    Log.Add(EPsaImportLogLevel::Warning, TEXT("fixed.psa\n补帧为估算姿态。"));
    Log.Add(EPsaImportLogLevel::Success, TEXT("fixed.psa：1 个动画。"));
    const FString FullText = Log.DisplayText();
    TestTrue(TEXT("All levels initially visible"), FullText.Contains(TEXT("[错误]")) && FullText.Contains(TEXT("[警告]")) && FullText.Contains(TEXT("[成功]")));
    for (int32 Mask = 0; Mask < 8; ++Mask)
    {
        Log.SetVisible(EPsaImportLogLevel::Error, (Mask & 1) != 0);
        Log.SetVisible(EPsaImportLogLevel::Warning, (Mask & 2) != 0);
        Log.SetVisible(EPsaImportLogLevel::Success, (Mask & 4) != 0);
        const FString Text = Log.DisplayText();
        TestEqual(TEXT("Error filter independent"), Text.Contains(TEXT("文件被截断")), (Mask & 1) != 0);
        TestEqual(TEXT("Warning filter independent of successful import"), Text.Contains(TEXT("补帧为估算姿态")), (Mask & 2) != 0);
        TestEqual(TEXT("Success filter independent"), Text.Contains(TEXT("1 个动画")), (Mask & 4) != 0);
        TestEqual(TEXT("Hidden errors retained in count"), Log.Count(EPsaImportLogLevel::Error), 1);
        TestEqual(TEXT("Hidden warnings retained in count"), Log.Count(EPsaImportLogLevel::Warning), 1);
        TestEqual(TEXT("Hidden successes retained in count"), Log.Count(EPsaImportLogLevel::Success), 1);
        if (Mask == 0) TestTrue(TEXT("All filters off explains empty output"), Text.Contains(TEXT("当前筛选条件下没有日志")));
    }
    TestEqual(TEXT("Restoring filters restores exact log and order"), Log.DisplayText(), FullText);
    Log.SetVisible(EPsaImportLogLevel::Success, false);
    Log.Reset();
    TestEqual(TEXT("New import clears old entries"), Log.Count(EPsaImportLogLevel::Error) + Log.Count(EPsaImportLogLevel::Warning) + Log.Count(EPsaImportLogLevel::Success), 0);
    TestFalse(TEXT("Filter preference retained across imports"), Log.IsVisible(EPsaImportLogLevel::Success));
    Log.Add(EPsaImportLogLevel::Success, TEXT("next.psa"));
    TestFalse(TEXT("New import honors existing filters"), Log.DisplayText().Contains(TEXT("next.psa")));
    Log.SetVisible(EPsaImportLogLevel::Success, true);
    TestTrue(TEXT("New hidden messages remain recoverable"), Log.DisplayText().Contains(TEXT("next.psa")));
    return true;
}
#endif
