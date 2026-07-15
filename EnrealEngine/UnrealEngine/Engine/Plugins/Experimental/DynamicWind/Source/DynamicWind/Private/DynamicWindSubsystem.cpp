// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicWindSubsystem.h"
#include "Animation/Skeleton.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/SkinnedAsset.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "SceneInterface.h"
#include "ScenePrivate.h"
#include "NaniteSceneProxy.h"
#include "DynamicWindSkeletalData.h"

namespace
{

TAutoConsoleVariable<bool> CVarDynamicWindEnable(
	TEXT("DynamicWind.Enable"),
	true,
	TEXT("Whether or not to enable the dynamic wind subsystem"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

}

UDynamicWindSubsystem::UDynamicWindSubsystem()
: bInitialized(false)
, TransformProvider(nullptr)
, TransformProviderId(DYNAMIC_WIND_TRANSFORM_PROVIDER_GUID)
{
}

bool UDynamicWindSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return CVarDynamicWindEnable.GetValueOnAnyThread();
}

void UDynamicWindSubsystem::PostInitialize()
{
	Super::PostInitialize();

	if (UWorld* World = GetWorld())
	{
		if (FSceneInterface* Scene = World->Scene)
		{
			if (FScene* RenderScene = Scene->GetRenderScene())
			{
				TransformProvider = MakeUnique<FDynamicWindTransformProvider>(*RenderScene);

				OnCreateRenderThreadResources =
					Nanite::FSkinnedSceneProxyDelegates::OnCreateRenderThreadResources.AddLambda(
						[this](const Nanite::FSkinnedSceneProxy* InSkinnedSceneProxy)
						{
							RegisterSceneProxy(InSkinnedSceneProxy);
						}
					);

				OnDestroyRenderThreadResources =
					Nanite::FSkinnedSceneProxyDelegates::OnDestroyRenderThreadResources.AddLambda(
						[this](const Nanite::FSkinnedSceneProxy* InSkinnedSceneProxy)
						{
							UnregisterSceneProxy(InSkinnedSceneProxy);
						}
					);


			}
		}
	}

	bInitialized =
		TransformProvider.IsValid() &&
		OnCreateRenderThreadResources.IsValid() &&
		OnDestroyRenderThreadResources.IsValid();
}

void UDynamicWindSubsystem::PreDeinitialize()
{
	bInitialized = false;

	if (OnCreateRenderThreadResources.IsValid())
	{
		Nanite::FSkinnedSceneProxyDelegates::OnCreateRenderThreadResources.Remove(OnCreateRenderThreadResources);
		OnCreateRenderThreadResources.Reset();
	}

	if (OnDestroyRenderThreadResources.IsValid())
	{
		Nanite::FSkinnedSceneProxyDelegates::OnDestroyRenderThreadResources.Remove(OnDestroyRenderThreadResources);
		OnDestroyRenderThreadResources.Reset();
	}

	TransformProvider.Reset();

	Super::PreDeinitialize();
}

void UDynamicWindSubsystem::RegisterSceneProxy(const Nanite::FSkinnedSceneProxy* InSkinnedProxy)
{
	if (&InSkinnedProxy->GetScene() != GetWorld()->Scene)
	{
		return;
	}

	FSkinningSceneExtensionProxy* Proxy = InSkinnedProxy->GetSkinningSceneExtensionProxy();

	if (!Proxy || Proxy->GetTransformProviderId() != TransformProviderId)
	{
		return;
	}

	if (const USkinnedAsset* SkinnedAsset = Proxy->GetSkinnedAsset())
	{
		if (!bInitialized)
		{
			UE_LOG(LogDynamicWind, Verbose, TEXT("Trying to register SkinnedSceneProxy (SkinnedAsset=%s), when the subsystem is not initialized."), *SkinnedAsset->GetName());
			return;
		}

		TransformProvider->RegisterSceneProxy(InSkinnedProxy);
	}
}

void UDynamicWindSubsystem::UnregisterSceneProxy(const Nanite::FSkinnedSceneProxy* InSkinnedProxy)
{
	if (&InSkinnedProxy->GetScene() != GetWorld()->Scene)
	{
		return;
	}

	FSkinningSceneExtensionProxy* Proxy = InSkinnedProxy->GetSkinningSceneExtensionProxy();

	if (!Proxy || Proxy->GetTransformProviderId() != TransformProviderId)
	{
		return;
	}

	if (const USkinnedAsset* SkinnedAsset = Proxy->GetSkinnedAsset())
	{
		if (!bInitialized)
		{
			UE_LOG(LogDynamicWind, Verbose, TEXT("Trying to unregister SkinnedSceneProxy (SkinnedAsset=%s), when the subsystem is not initialized."), *SkinnedAsset->GetName());
			return;
		}

		TransformProvider->UnregisterSceneProxy(InSkinnedProxy);
	}
}

float UDynamicWindSubsystem::GetBlendedWindAmplitude() const
{
	if (TransformProvider)
	{
		return TransformProvider->GetBlendedWindAmplitude();
	}

	return -1.0f;
}

void UDynamicWindSubsystem::UpdateWindParameters(const FDynamicWindParameters& Parameters)
{
	if (bInitialized)
	{
		ENQUEUE_RENDER_COMMAND(UpdateFoliageWindSimulationInputs)
		(
			[WindProvider = TransformProvider.Get(), WindParameters = Parameters](FRHICommandListImmediate&) mutable
			{
				WindProvider->UpdateParameters(WindParameters);
			}
		);
	}
}
