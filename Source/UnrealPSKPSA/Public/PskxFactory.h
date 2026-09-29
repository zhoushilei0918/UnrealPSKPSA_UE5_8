#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"
#include "ActorXImportSettings.h"
#include "PskxFactory.generated.h"

UCLASS()
class UNREALPSKPSA_API UPskxFactory : public UFactory
{
	GENERATED_BODY()
public:
	UPskxFactory()
	{
		bEditorImport = true;
		bText = false;

		Formats.Add(FactoryExtension + ";" + FactoryDescription);

        SupportedClass = UStaticMesh::StaticClass();
	}
	
	static UObject* Import(const FString& Filename, UObject* Parent, const FName Name, const EObjectFlags Flags, TMap<FString, FString>
						   MaterialNameToPathMap, const FActorXOrientation& Orientation = FActorXOrientation());

    UPROPERTY(EditAnywhere, Category="Import")
    FActorXOrientation Orientation;

    virtual bool FactoryCanImport(const FString& Filename) override;

protected:
	FString FactoryExtension = "pskx";
	FString FactoryDescription = "ActorX Static or Skeletal Mesh (Auto Detect)";
	
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Params, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};
