#pragma once
#include "CoreMinimal.h"

class USkeletalMesh;
class UAnimSequence;
class FPsaReader;

struct FPsaImportOptions
{
    bool bFModel = true;
    bool bReplaceExisting = false;
    bool bRepairInvalidKeys = false;
    bool bSaveAssets = true;
    float TranslationScale = 1.0f;
};

struct FPsaBoneMapping
{
    int32 SourceIndex;
    int32 TargetIndex;
};

class UNREALPSKPSA_API FPsaImporter
{
public:
    static bool MatchBones(const FPsaReader& Reader, const USkeletalMesh* Mesh, TArray<FPsaBoneMapping>& Mapping, FString& Error);
    static bool ImportFile(const FString& Filename, USkeletalMesh* Mesh, const FString& Destination,
        const FPsaImportOptions& Options, TArray<UAnimSequence*>& Imported, FString& Summary, FString& Error);
};
