// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "SpanAllocator.h"
#include "Matrix3x4.h"
#include "Containers/Map.h"
#include "DynamicWindLog.h"
#include "DynamicWindParameters.h"
#include "DynamicWindProvider.h"
#include "DynamicWindSubsystem.generated.h"

class USkeleton;
class UInstancedSkinnedMeshComponent;

UCLASS(BlueprintType, ClassGroup = (Rendering, Common))
class DYNAMICWIND_API UDynamicWindSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDynamicWindSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual void PostInitialize() override;
	virtual void PreDeinitialize() override;

	void RegisterSceneProxy(const Nanite::FSkinnedSceneProxy* InSkinnedProxy);
	void UnregisterSceneProxy(const Nanite::FSkinnedSceneProxy* InSkinnedProxy);

public:
	UFUNCTION(BlueprintCallable, Category = "DynamicWind")
	float GetBlendedWindAmplitude() const;

	UFUNCTION(BlueprintCallable, Category = "DynamicWind")
	void UpdateWindParameters(const FDynamicWindParameters& Parameters);

private:
	bool bInitialized = false;

	TUniquePtr<FDynamicWindTransformProvider> TransformProvider;
	FGuid TransformProviderId;

	FDelegateHandle OnCreateRenderThreadResources;
	FDelegateHandle OnDestroyRenderThreadResources;
};
