#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PsaImportSettings.generated.h"

class USkeletalMesh;

UENUM()
enum class EPsaSource : uint8
{
    FModel UMETA(DisplayName="FModel / CUE4Parse"),
    ActorX UMETA(DisplayName="UEViewer / Legacy ActorX"),
    Auto UMETA(DisplayName="Auto Detect (Recommended)")
};

UCLASS()
class UNREALPSKPSA_API UPsaImportSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Target", meta=(DisplayName="Target Skeletal Mesh", ToolTip="Select a Skeletal Mesh imported from PSK or FBX. Animations will use its Skeleton."))
    TObjectPtr<USkeletalMesh> TargetMesh;

    UPROPERTY(EditAnywhere, Category="Target", meta=(DisplayName="Destination", ToolTip="UE content path, such as /Game/Characters/Animations."))
    FString Destination = TEXT("/Game/Animations");

    UPROPERTY(EditAnywhere, Category="Import Options", meta=(DisplayName="PSA Source", ToolTip="Detect UEViewer metadata per file, otherwise use FModel. You can also choose the source manually. When UEViewer provides no real hierarchy, match bones by name and use the target mesh hierarchy."))
    EPsaSource Source = EPsaSource::Auto;

    UPROPERTY(EditAnywhere, Category="Import Options", meta=(DisplayName="Replace Existing Animations", ToolTip="Off by default: generate a unique name. When enabled, overwrite only animation sequences using the same Skeleton."))
    bool bReplaceExisting = false;

    UPROPERTY(EditAnywhere, Category="Import Options", meta=(DisplayName="Repair Invalid Keys", ToolTip="Off by default. Repair keys per animation and bone: interpolate interior gaps; copy the nearest valid frame at either end; fail if the entire track is invalid. Position, rotation, and scale are repaired together. Repairs estimate poses and do not recover the original motion."))
    bool bRepairInvalidKeys = false;

    UPROPERTY(EditAnywhere, Category="Import Options", meta=(DisplayName="Use Mesh Reference Scale", ToolTip="Off by default. Ignore PSA animation scale and use each target mesh bone's reference scale, keeping position and rotation. Useful for unusually stretched animations; intentional scale effects are also ignored."))
    bool bUseReferenceScale = false;

    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="Import Options", meta=(DisplayName="Translation Scale", ClampMin="0.0001", ClampMax="10000.0", ToolTip="Default: 1. Adjust only if the FBX mesh uses different units."))
    float TranslationScale = 1.0f;
};
