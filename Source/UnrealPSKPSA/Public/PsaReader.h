#pragma once

#include "CoreMinimal.h"
#include "ActorXModels.h"

struct FPsaSequenceInfo
{
    FString Name;
    int32 TotalBones = 0;
    int32 FirstRawFrame = 0;
    int32 NumRawFrames = 0;
    float AnimRate = 0;
};

struct FPsaKey
{
    FVector3f Position;
    FQuat4f Rotation;
    float Time = 0;
};

struct FPsaScaleKey
{
    FVector3f Scale;
    float Time = 0;
};

class UNREALPSKPSA_API FPsaReader
{
public:
    explicit FPsaReader(const FString& Filename);
    bool bIsValid = false;
    FString Error;
    TArray<VNamedBoneBinary> Bones;
    TArray<FPsaSequenceInfo> Sequences;
    TArray<FPsaKey> Keys;
    TArray<FPsaScaleKey> ScaleKeys;
};
