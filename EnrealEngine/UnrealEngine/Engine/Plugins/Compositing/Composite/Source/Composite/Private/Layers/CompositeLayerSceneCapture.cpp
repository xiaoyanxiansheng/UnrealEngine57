// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerSceneCapture.h"

#include "CompositeActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CompositeSceneCapture2DComponent.h"
#include "CompositeRenderTargetPool.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"

UCompositeLayerSceneCapture::UCompositeLayerSceneCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bVisibleInSceneCaptureOnly{true} // By default, registered meshes will no longer be visible in the main render
	, bCustomRenderPass{false}
{
	Operation = ECompositeCoreMergeOp::Over;
}

UCompositeLayerSceneCapture::~UCompositeLayerSceneCapture() = default;

void UCompositeLayerSceneCapture::OnRemoved(const UWorld* World)
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	// Note: We don't use IsValid(..) since destruction should proceed even if the actor is pending kill.
	if (CompositeActor != nullptr)
	{
		CompositeActor->DestroySceneCaptures(this);
	}

	RestorePrimitiveVisibilityState();
}

void UCompositeLayerSceneCapture::BeginDestroy()
{
	Super::BeginDestroy();

	OnRemoved(GetWorld()); // Redundant remove call for safety
}

#if WITH_EDITOR
void UCompositeLayerSceneCapture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Actors))
	{
		SetActors(MoveTemp(Actors));
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bCustomRenderPass))
	{
		SetCustomRenderPass(bCustomRenderPass);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bVisibleInSceneCaptureOnly))
	{
		SetVisibleInSceneCaptureOnly(bVisibleInSceneCaptureOnly);
	}
}

void UCompositeLayerSceneCapture::PreEditUndo()
{
	Super::PreEditUndo();
}

void UCompositeLayerSceneCapture::PostEditUndo()
{
	Super::PostEditUndo();

	SetActors(MoveTemp(Actors));

	SetCustomRenderPass(bCustomRenderPass);
}
#endif

bool UCompositeLayerSceneCapture::GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return false;
	}

	USceneCaptureComponent2D* CaptureComponent = GetSceneCaptureComponent();
	if (!IsValid(CaptureComponent))
	{
		return false;
	}

	FCompositeRenderTargetPool::Get().ConditionalAcquireTarget(CaptureComponent, CaptureComponent->TextureTarget, GetRenderResolution());

	FResourceMetadata Metadata = {};
	Metadata.bInvertedAlpha = true;
	Metadata.bDistorted = CaptureComponent->ShowFlags.LensDistortion;

	const ResourceId TexId = InContext.FindOrCreateExternalTexture(CaptureComponent->TextureTarget, Metadata);

	FPassInputDecl PassInput;
	PassInput.Set<FPassExternalResourceDesc>({ TexId });

	AddChildPasses(PassInput, InContext, InFrameAllocator, LayerPasses);

	FPassInputDeclArray Inputs;
	Inputs.SetNum(FixedNumLayerInputs);
	Inputs[0] = PassInput;
	Inputs[1] = GetDefaultSecondInput(InContext);

	OutProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), GetMergeOperation(InContext), TEXT("SceneCapture"), ELensDistortionHandling::Disabled);
	return true;
}

const TArray<TSoftObjectPtr<AActor>> UCompositeLayerSceneCapture::GetActors() const
{
	return Actors;
}

void UCompositeLayerSceneCapture::SetActors(TArray<TSoftObjectPtr<AActor>> InActors)
{
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ShowOnlyComponents;
	ShowOnlyComponents.Reserve(InActors.Num());

	for (const TSoftObjectPtr<AActor>& SoftActor : InActors)
	{
		if (const AActor* Actor = SoftActor.Get())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					ShowOnlyComponents.Add(PrimitiveComponent);
				}
			}
		}
	}

	USceneCaptureComponent2D* SceneCaptureComponent = GetSceneCaptureComponent();
	if (IsValid(SceneCaptureComponent))
	{
		// First, we empty show-only actors since our previous logic used them
		SceneCaptureComponent->ShowOnlyActors.Empty();
		SceneCaptureComponent->ShowOnlyComponents = ShowOnlyComponents;
	}

	RestorePrimitiveVisibilityState();

	Actors = MoveTemp(InActors);

	CachePrimitiveVisibilityState(ShowOnlyComponents);

	UpdatePrimitiveVisibilityState();
}

bool UCompositeLayerSceneCapture::IsCustomRenderPass() const
{
	return bCustomRenderPass;
}

void UCompositeLayerSceneCapture::SetCustomRenderPass(bool bInIsFastRenderPass)
{
	bCustomRenderPass = bInIsFastRenderPass;

	UCompositeSceneCapture2DComponent* SceneCaptureComponent = GetSceneCaptureComponent();
	if (IsValid(SceneCaptureComponent))
	{
		if (bCustomRenderPass)
		{
			SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
			SceneCaptureComponent->bRenderInMainRenderer = true;
		}
		else
		{
			SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
			SceneCaptureComponent->bRenderInMainRenderer = false;
		}
	}
}

bool UCompositeLayerSceneCapture::IsVisibleInSceneCaptureOnly() const
{
	return bVisibleInSceneCaptureOnly;
}

void UCompositeLayerSceneCapture::SetVisibleInSceneCaptureOnly(bool bInVisible)
{
	bVisibleInSceneCaptureOnly = bInVisible;

	UpdatePrimitiveVisibilityState();
}

UCompositeSceneCapture2DComponent* UCompositeLayerSceneCapture::GetSceneCaptureComponent() const
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (IsValid(CompositeActor))
	{
		return CompositeActor->FindOrCreateSceneCapture<UCompositeSceneCapture2DComponent>(this);
	}

	return nullptr;
}

void UCompositeLayerSceneCapture::CachePrimitiveVisibilityState(TArrayView<TWeakObjectPtr<UPrimitiveComponent>> InPrimitives)
{
	CachedVisibilityInSceneCapture.Reset();

	for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : InPrimitives)
	{
		TStrongObjectPtr<UPrimitiveComponent> Primitive = PrimitiveComponent.Pin();
		if (Primitive.IsValid())
		{
			CachedVisibilityInSceneCapture.Add(Primitive.Get(), Primitive->bVisibleInSceneCaptureOnly);
		}
	}
}

void UCompositeLayerSceneCapture::UpdatePrimitiveVisibilityState()
{
	for (TSoftObjectPtr<AActor>& SoftActor : Actors)
	{
		if (AActor* Actor = SoftActor.Get())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					PrimitiveComponent->Modify();
					PrimitiveComponent->SetVisibleInSceneCaptureOnly(bVisibleInSceneCaptureOnly);
				}
			}
		}
	}
}

void UCompositeLayerSceneCapture::RestorePrimitiveVisibilityState()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, bool>& Pair : CachedVisibilityInSceneCapture)
	{
		TStrongObjectPtr< UPrimitiveComponent> Primitive = Pair.Key.Pin();
		if (Primitive.IsValid())
		{
			Primitive->Modify();
			Primitive->SetVisibleInSceneCaptureOnly(Pair.Value);
		}
	}

	CachedVisibilityInSceneCapture.Reset();
}

