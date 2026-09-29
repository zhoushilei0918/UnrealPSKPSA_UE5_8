#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"
#include "ActorXImportSettings.generated.h"

class USkeletalMesh;

UENUM()
enum class EActorXForwardAxis : uint8
{
    PositiveX UMETA(DisplayName="+X"),
    PositiveY UMETA(DisplayName="+Y"),
    NegativeX UMETA(DisplayName="-X"),
    NegativeY UMETA(DisplayName="-Y")
};

USTRUCT()
struct UNREALPSKPSA_API FActorXOrientation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Orientation", meta=(DisplayName="Target Forward", ToolTip="Forward direction after importing into UE. Rotate around UE +Z, applying the same transform to the mesh and bones."))
    EActorXForwardAxis TargetForward = EActorXForwardAxis::PositiveY;

    UPROPERTY(EditAnywhere, Category="Orientation", meta=(DisplayName="Source Forward", ToolTip="Source forward direction after restoring UE coordinates. The girl023 sample uses +X. Use the same settings for every body part of a character."))
    EActorXForwardAxis SourceForward = EActorXForwardAxis::PositiveX;

    FQuat4f Rotation() const;
    FString Description() const;
    static FString AxisName(EActorXForwardAxis Axis);
    static FActorXOrientation Unchanged();
};

// Serialized with the mesh so animations imported in a later editor session use the same basis.
UCLASS()
class UNREALPSKPSA_API UActorXMeshImportData : public UAssetImportData
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, Category="ActorX", meta=(DisplayName="Import Orientation"))
    FActorXOrientation Orientation;
};

UCLASS()
class UNREALPSKPSA_API UActorXImportSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Orientation", meta=(ShowOnlyInnerProperties))
    FActorXOrientation Orientation;
};

UNREALPSKPSA_API FActorXOrientation GetActorXMeshOrientation(const USkeletalMesh* Mesh);
