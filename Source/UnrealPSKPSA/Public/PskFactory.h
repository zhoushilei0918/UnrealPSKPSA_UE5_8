#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"
#include "ActorXImportSettings.h"
#include "PskFactory.generated.h"

UCLASS()
class UNREALPSKPSA_API UPskFactory : public UFactory
{
	GENERATED_BODY()
public:
	UPskFactory()
	{
		bEditorImport = true;
		bText = false;

		Formats.Add(FactoryExtension + ";" + FactoryDescription);

		SupportedClass = FactoryClass;
	}
	
	static UObject* Import(const FString& Filename, UObject* Parent, const FName Name, const EObjectFlags Flags, TMap<FString, FString>
	                       MaterialNameToPathMap, const FActorXOrientation& Orientation = FActorXOrientation());

    UPROPERTY(EditAnywhere, Category="Import")
    FActorXOrientation Orientation;
	static void ProcessSkeleton(const FSkeletalMeshImportData&    ImportData,
								const USkeleton*                  Skeleton,
								FReferenceSkeleton&               OutRefSkeleton,
								int32&                            OutSkeletalDepth);

protected:
	UClass* FactoryClass = USkeletalMesh::StaticClass();
	FString FactoryExtension = "psk";
	FString FactoryDescription = "Unreal Skeletal Mesh";

	virtual bool FactoryCanImport(const FString& Filename) override
	{
		const auto Extension = FPaths::GetExtension(Filename);
		return Extension.Equals(FactoryExtension, ESearchCase::IgnoreCase);
	}
	
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	
};
