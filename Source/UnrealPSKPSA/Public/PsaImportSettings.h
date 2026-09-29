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
    Auto UMETA(DisplayName="自动识别（推荐）")
};

UCLASS()
class UNREALPSKPSA_API UPsaImportSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="目标", meta=(DisplayName="目标骨骼网格", ToolTip="选择由 PSK 或 FBX 导入的 Skeletal Mesh。动画使用它的 Skeleton。"))
    TObjectPtr<USkeletalMesh> TargetMesh;

    UPROPERTY(EditAnywhere, Category="目标", meta=(DisplayName="保存路径", ToolTip="UE 内容路径，例如 /Game/Characters/Animations。"))
    FString Destination = TEXT("/Game/Animations");

    UPROPERTY(EditAnywhere, Category="导入选项", meta=(DisplayName="PSA 导出来源", ToolTip="默认按每个文件识别 UEViewer 特征，否则沿用 FModel；也可手动指定来源。UEViewer 未提供真实层级时，按骨骼名匹配并沿用目标网格层级。"))
    EPsaSource Source = EPsaSource::Auto;

    UPROPERTY(EditAnywhere, Category="导入选项", meta=(DisplayName="覆盖同名动画", ToolTip="默认不覆盖，自动生成不同名称。勾选后仅覆盖使用相同 Skeleton 的动画序列。"))
    bool bReplaceExisting = false;

    UPROPERTY(EditAnywhere, Category="导入选项", meta=(DisplayName="修复无效关键帧", ToolTip="默认关闭。按同一动画、同一骨骼补帧：中间缺帧使用前后有效帧插值，首尾缺帧全部复制最近有效帧；整条轨道均无效则失败。无效姿态的位置、旋转和缩放一起修复。补帧是估算，不会恢复丢失的原始动作。"))
    bool bRepairInvalidKeys = false;

    UPROPERTY(EditAnywhere, Category="导入选项", meta=(DisplayName="使用模型参考缩放", ToolTip="默认关闭。开启后忽略 PSA 的动画缩放，改用目标网格各骨骼的参考缩放；位置和旋转不变。适用于异常拉伸的动画，也会忽略原本有意制作的缩放效果。"))
    bool bUseReferenceScale = false;

    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="导入选项", meta=(DisplayName="位置缩放", ClampMin="0.0001", ClampMax="10000.0", ToolTip="默认 1。仅当 FBX 模型采用不同单位时调整。"))
    float TranslationScale = 1.0f;
};
