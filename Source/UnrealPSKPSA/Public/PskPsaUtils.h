#pragma once
#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "ObjectTools.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"

class FPskPsaUtils
{
public:
	template <typename T, typename = UObject>
	static T* LocalFindOrCreate(UClass* StaticClass, UObject* FactoryParent, FString Filename, EObjectFlags Flags)
	{
		Filename = ObjectTools::SanitizeObjectName(Filename);
		const auto Package = CreatePackage(*(FPackageName::GetLongPackagePath(FactoryParent->GetOutermost()->GetName()) / Filename));
		Package->FullyLoad();

		auto Asset = FindObject<T>(Package, *Filename);
		if (!Asset && FPackageName::DoesPackageExist(Package->GetName()))
		{
			Asset = LoadObject<T>(nullptr, *(Package->GetName() + TEXT(".") + Filename));
		}
		if (!Asset)
		{
			Asset = NewObject<T>(Package, StaticClass, FName(Filename), Flags);
			Asset->PostEditChange();
			FAssetRegistryModule::AssetCreated(Asset);
			Asset->MarkPackageDirty();
		}
		
		return Asset;
	}

	template <typename T>
	static T* LocalCreate(UClass* StaticClass, UObject* FactoryParent, FString Filename, EObjectFlags Flags)
	{
		Filename = ObjectTools::SanitizeObjectName(Filename);
		const auto Package = CreatePackage(*(FPackageName::GetLongPackagePath(FactoryParent->GetOutermost()->GetName()) / Filename));
		// An existing package must be fully loaded before replacing and saving its asset.
		Package->FullyLoad();
		if (T* Existing = FindObject<T>(Package, *Filename))
		{
			FAssetCompilingManager::Get().FinishCompilationForObjects({Existing});
		}

		auto Asset = NewObject<T>(Package, StaticClass, FName(Filename), Flags);
		return Asset;
	}
};
