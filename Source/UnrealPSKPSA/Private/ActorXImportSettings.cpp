#include "ActorXImportSettings.h"
#include "Engine/SkeletalMesh.h"

FQuat4f FActorXOrientation::Rotation() const
{
    const float Degrees = (int32(TargetForward) - int32(SourceForward)) * 90.0f;
    return FQuat4f(FVector3f::UpVector, FMath::DegreesToRadians(Degrees));
}

FString FActorXOrientation::AxisName(EActorXForwardAxis Axis)
{
    switch (Axis)
    {
    case EActorXForwardAxis::PositiveX: return TEXT("+X");
    case EActorXForwardAxis::PositiveY: return TEXT("+Y");
    case EActorXForwardAxis::NegativeX: return TEXT("-X");
    case EActorXForwardAxis::NegativeY: return TEXT("-Y");
    default: return NSLOCTEXT("UnrealPSKPSA", "UnknownAxis", "Unknown").ToString();
    }
}

FString FActorXOrientation::Description() const
{
    return FText::Format(NSLOCTEXT("UnrealPSKPSA", "OrientationDescription", "Source {0} → UE {1}"), FText::FromString(AxisName(SourceForward)), FText::FromString(AxisName(TargetForward))).ToString();
}

FActorXOrientation FActorXOrientation::Unchanged()
{
    FActorXOrientation Result;
    Result.TargetForward = Result.SourceForward;
    return Result;
}

FActorXOrientation GetActorXMeshOrientation(const USkeletalMesh* Mesh)
{
    if (Mesh)
        if (const auto* Data = Cast<UActorXMeshImportData>(Mesh->GetAssetImportData())) return Data->Orientation;
    // Old plugin meshes and external FBX meshes have no recorded ActorX rotation.
    return FActorXOrientation::Unchanged();
}
