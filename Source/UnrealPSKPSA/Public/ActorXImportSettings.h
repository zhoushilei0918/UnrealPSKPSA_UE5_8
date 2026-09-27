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

    UPROPERTY(EditAnywhere, Category="朝向", meta=(DisplayName="目标朝向", ToolTip="模型导入 UE 后的正面方向。绕 UE 的 +Z 轴旋转，网格和骨骼使用同一变换。"))
    EActorXForwardAxis TargetForward = EActorXForwardAxis::PositiveY;

    UPROPERTY(EditAnywhere, Category="朝向", meta=(DisplayName="源模型正面", ToolTip="源模型在还原为 UE 坐标后的正面方向。当前 girl023 样本为 +X；同一角色所有身体部件必须使用相同设置。"))
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
    UPROPERTY(VisibleAnywhere, Category="ActorX", meta=(DisplayName="导入朝向"))
    FActorXOrientation Orientation;
};

UCLASS()
class UNREALPSKPSA_API UActorXImportSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="朝向", meta=(ShowOnlyInnerProperties))
    FActorXOrientation Orientation;
};

UNREALPSKPSA_API FActorXOrientation GetActorXMeshOrientation(const USkeletalMesh* Mesh);
