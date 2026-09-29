#pragma once
#include "CoreMinimal.h"

enum class EPsaImportLogLevel : uint8 { Error, Warning, Success };

// The full batch remains available when a level is hidden; filters only affect rendering.
class FPsaImportLog
{
public:
    static const TCHAR* Label(EPsaImportLogLevel Level)
    {
        switch (Level)
        {
        case EPsaImportLogLevel::Error: return TEXT("错误");
        case EPsaImportLogLevel::Warning: return TEXT("警告");
        default: return TEXT("成功");
        }
    }
    void Reset() { Entries.Reset(); }
    void Add(EPsaImportLogLevel Level, const FString& Message) { Entries.Add({Level, Message}); }
    bool IsVisible(EPsaImportLogLevel Level) const { return !HiddenLevels.Contains(Level); }
    void SetVisible(EPsaImportLogLevel Level, bool bVisible)
    {
        if (bVisible) HiddenLevels.Remove(Level);
        else HiddenLevels.Add(Level);
    }
    int32 Count(EPsaImportLogLevel Level) const
    {
        int32 Result = 0;
        for (const auto& Entry : Entries) if (Entry.Level == Level) ++Result;
        return Result;
    }
    FString DisplayText() const
    {
        if (Entries.IsEmpty()) return TEXT("尚无导入日志。");
        TArray<FString> Lines;
        for (const auto& Entry : Entries)
            if (IsVisible(Entry.Level)) Lines.Add(FString::Printf(TEXT("[%s] %s"), Label(Entry.Level), *Entry.Message));
        return Lines.IsEmpty() ? FString::Printf(TEXT("当前筛选条件下没有日志（共 %d 条）。"), Entries.Num()) : FString::Join(Lines, TEXT("\n\n"));
    }
private:
    struct FEntry { EPsaImportLogLevel Level; FString Message; };
    TArray<FEntry> Entries;
    TSet<EPsaImportLogLevel> HiddenLevels;
};
