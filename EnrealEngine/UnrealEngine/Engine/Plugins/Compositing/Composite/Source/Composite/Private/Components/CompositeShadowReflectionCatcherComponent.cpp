// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/CompositeShadowReflectionCatcherComponent.h"

#include "CompositeRenderTargetPool.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "Composite"

UCompositeShadowReflectionCatcherComponent::UCompositeShadowReflectionCatcherComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

	ShowFlags.SetGrain(false);
	ShowFlags.SetScreenSpaceReflections(false);
	//Note: This can cause temporal ghosting artifacts on moving shadows, but remains preferable over the otherwise default shadow noise.
	ShowFlags.SetTemporalAA(true);
}

#undef LOCTEXT_NAMESPACE
