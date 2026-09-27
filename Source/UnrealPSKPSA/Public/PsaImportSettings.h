#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PsaImportSettings.generated.h"

class USkeletalMesh;

UENUM()
enum class EPsaSource : uint8
{
    FModel UMETA(DisplayName="FModel / CUE4Parse"),
    ActorX UMETA(DisplayName="UEViewer / Legacy ActorX")
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

    UPROPERTY(EditAnywhere, Category="导入选项", meta=(DisplayName="PSA 导出来源", ToolTip="FModel 模式还原其根骨骼旋转和动画时长。"))
    EPsaSource Source = EPsaSource::FModel;

    UPROPERTY(EditAnywhere, Category="导入选项", meta=(DisplayName="覆盖同名动画", ToolTip="默认不覆盖，自动生成不同名称。勾选后仅覆盖使用相同 Skeleton 的动画序列。"))
    bool bReplaceExisting = false;

    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="导入选项", meta=(DisplayName="位置缩放", ClampMin="0.0001", ClampMax="10000.0", ToolTip="默认 1。仅当 FBX 模型采用不同单位时调整。"))
    float TranslationScale = 1.0f;
};
