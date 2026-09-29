#pragma once
#include "CoreMinimal.h"

enum class EPsaImportLogLevel : uint8 { Error, Warning, Success };

// The full batch remains available when a level is hidden; filters only affect rendering.
class FPsaImportLog
{
public:
    static FText Label(EPsaImportLogLevel Level)
    {
        switch (Level)
        {
        case EPsaImportLogLevel::Error: return NSLOCTEXT("UnrealPSKPSA", "LogError", "Error");
        case EPsaImportLogLevel::Warning: return NSLOCTEXT("UnrealPSKPSA", "LogWarning", "Warning");
        default: return NSLOCTEXT("UnrealPSKPSA", "LogSuccess", "Success");
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
        if (Entries.IsEmpty()) return NSLOCTEXT("UnrealPSKPSA", "NoLog", "No import log yet.").ToString();
        TArray<FString> Lines;
        for (const auto& Entry : Entries)
            if (IsVisible(Entry.Level)) Lines.Add(FString::Printf(TEXT("[%s] %s"), *Label(Entry.Level).ToString(), *Entry.Message));
        return Lines.IsEmpty() ? FText::Format(NSLOCTEXT("UnrealPSKPSA", "NoVisibleLog", "No entries match the current filters ({0} total)."), Entries.Num()).ToString() : FString::Join(Lines, TEXT("\n\n"));
    }
private:
    struct FEntry { EPsaImportLogLevel Level; FString Message; };
    TArray<FEntry> Entries;
    TSet<EPsaImportLogLevel> HiddenLevels;
};
