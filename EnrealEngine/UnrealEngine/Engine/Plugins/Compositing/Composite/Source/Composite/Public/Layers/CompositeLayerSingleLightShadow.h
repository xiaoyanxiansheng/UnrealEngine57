// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EngineDefines.h"
#include "CompositeLayerBase.h"
#include "UObject/WeakObjectPtr.h"

#include "CompositeLayerSingleLightShadow.generated.h"

/**
* Note that much of the custom logic here could be removed given access to the engine's CSM/VSM built-in functionality.
* However, those techniques remain out-of-reach to plugins since they are renderer-private.
*/

#define UE_API COMPOSITE_API

class AActor;
class ALight;
class UCompositeSceneCapture2DComponent;

/**
* Layer for catching shadows from a single light.
* 
* Classic shadow map percentage-closer filtering technique (without cascades), defaulting to 5x5.
* Implemented with custom render passes for minimal performance cost.
*/
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "Single Light Shadow"))
class UCompositeLayerSingleLightShadow : public UCompositeLayerBase
{
	GENERATED_BODY()

public:
	/** Constructor. */
	UE_API UCompositeLayerSingleLightShadow(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor. */
	UE_API virtual ~UCompositeLayerSingleLightShadow() = default;

	UE_API virtual void OnRemoved(const UWorld* World) override;

	//~ Begin UObject Interface
	UE_API virtual void BeginDestroy() override;

#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual void PostEditUndo() override;
#endif
	//~ End UObject Interface

	/** Getter function to override, returning pass layer proxies to be passed to the render thread. (Proxy objects should be allocated from the provided allocator.) */
	UE_API virtual bool GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

public:
	/**
	* The reference light, whose exact transform will by used to render a shadow map.
	* Please make sure it is aligned to your shadow casting scene by piloting the light.
	* 
	* Only directional lights are currently supported.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta=(AllowedClasses="/Script/Engine.DirectionalLight"))
	TObjectPtr<ALight> Light;

	/** Get shadow caster actors. */
	UFUNCTION(BlueprintGetter)
	UE_API const TArray<TSoftObjectPtr<AActor>> GetShadowCastingActors() const;

	/** Set shadow caster actors. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetShadowCastingActors(TArray<TSoftObjectPtr<AActor>> InShadowCaster);

	/**
	* The desired width (in world units) of the shadow map view (ignored if light is not directional).
	* Reduce size to only contain the shadow casting geometry in view of the light.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	float OrthographicWidth = DEFAULT_ORTHOWIDTH;

	/**
	* Resolution of the shadow map texture. Lower resolutions will cause more blurry shadows, but require a higher shadow bias.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (ClampMin = "128", UIMin = "128"))
	int32 ShadowMapResolution = 2048;

	/** Basic shadow strength multiplier, from 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float ShadowStrength = 0.5f;

	/**
	* Shadow bias to avoid shadow acne. Increase until unwanted shadowing disappears (automatically scaled by r.Shadow.CSMDepthBias console variable).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ShadowBias = 0.5f;

private:
	/** Find or create scene depth capture component, correctly configured. */
	UCompositeSceneCapture2DComponent* FindOrCreateSceneDepthCapture(ACompositeActor& InOuter) const;

	/** Find or create shadow map capture component, correctly configured. */
	UCompositeSceneCapture2DComponent* FindOrCreateShadowMapCapture(ACompositeActor& InOuter) const;

	/** Convenience function to calculate shadow matrices. */
	void GetShadowMatrices(UCompositeSceneCapture2DComponent& ShadowMapCapture, FMatrix44f& OutShadowMatrix, FVector4f& OutShadowInvDeviceZToWorldZ) const;

private:
	/**
	* Optional: List of shadow casting actors to be removed from the main camera view's scene depth.
	* This helps create a cleaner shadow matte preventing aliased dark edges around CG objects.
	*/
	UPROPERTY(EditAnywhere, BlueprintGetter = GetShadowCastingActors, BlueprintSetter = SetShadowCastingActors, Category = "", meta = (AllowPrivateAccess = true))
	TArray<TSoftObjectPtr<AActor>> ShadowCastingActors;

	/** Cached pointer to the composite-managed shadow map scene capture. */
	mutable TWeakObjectPtr<UCompositeSceneCapture2DComponent> CachedShadowMapCapture;

	/** Cached pointer to the scene depth capture. */
	mutable TWeakObjectPtr<UCompositeSceneCapture2DComponent> CachedSceneDepthCapture;

	friend class FCompositeLayerSingleLightShadowCustomization;
};

#undef UE_API

