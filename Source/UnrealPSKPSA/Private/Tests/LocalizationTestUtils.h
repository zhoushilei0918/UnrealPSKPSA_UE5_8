#pragma once
#include "CoreMinimal.h"

// Assertions should inspect the same localized diagnostic in either editor language.
inline FText ActorXTestText(const TCHAR* Key)
{
    FText Text;
    if (!FText::FindTextInLiveTable_Advanced(TEXT("UnrealPSKPSA"), Key, Text))
        return FText::FromString(FString(TEXT("MISSING_LOCALIZATION_KEY: ")) + Key);
    return Text;
}
