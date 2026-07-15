// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerShadowReflection.h"

#include "CompositeActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CompositeShadowReflectionCatcherComponent.h"
#include "CompositeRenderTargetPool.h"
#include "Containers/StaticArray.h"

UCompositeLayerShadowReflection::UCompositeLayerShadowReflection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Operation = ECompositeCoreMergeOp::Multiply;
	AutoConfigureActors = ECompositeHiddenInSceneCaptureConfiguration::None;
}

UCompositeLayerShadowReflection::~UCompositeLayerShadowReflection() = default;

void UCompositeLayerShadowReflection::OnRemoved(const UWorld* World)
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	// Note: We don't use IsValid(..) since destruction should proceed even if the actor is pending kill.
	if (CompositeActor != nullptr)
	{
		CompositeActor->DestroySceneCaptures(this);
	}

	RestorePrimitiveVisibilityState();
}

void UCompositeLayerShadowReflection::BeginDestroy()
{
	Super::BeginDestroy();

	OnRemoved(GetWorld()); // Redundant remove call for safety
}

#if WITH_EDITOR
void UCompositeLayerShadowReflection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Actors))
	{
		SetActors(MoveTemp(Actors));
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, AutoConfigureActors))
	{
		UpdatePrimitiveVisibilityState();
	}
}

void UCompositeLayerShadowReflection::PreEditUndo()
{
	Super::PreEditUndo();
}

void UCompositeLayerShadowReflection::PostEditUndo()
{
	Super::PostEditUndo();

	SetActors(MoveTemp(Actors));
}
#endif

bool UCompositeLayerShadowReflection::GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return false;
	}

	CachedSceneCaptures = FindOrCreateSceneCapturePair(*CompositeActor);

	// Cached scene captures are now expected to be valid, early out if not.
	if (!ensure(CachedSceneCaptures[0].IsValid() && CachedSceneCaptures[1].IsValid()))
	{
		return false;
	}

	FMergePassProxy* DivisionProxy = nullptr;

	{
		FPassInputDeclArray Inputs;
		Inputs.SetNum(2);

		for (int32 Index = 0; Index < 2; ++Index)
		{
			UCompositeShadowReflectionCatcherComponent* Component = CachedSceneCaptures[Index].Get();
			FCompositeRenderTargetPool::Get().ConditionalAcquireTarget(Component, Component->TextureTarget, GetRenderResolution());

			FResourceMetadata Metadata = {};
			Metadata.bInvertedAlpha = true;
			Metadata.bDistorted = Component->ShowFlags.LensDistortion;
			const ResourceId TexId = InContext.FindOrCreateExternalTexture(Component->TextureTarget, Metadata);

			Inputs[Index].Set<FPassExternalResourceDesc>({ TexId });
		}

		DivisionProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), ECompositeCoreMergeOp::Divide, TEXT("ShadowRefl Div"), ELensDistortionHandling::Disabled);
	}

	FPassInputDeclArray Inputs;
	Inputs.SetNum(FixedNumLayerInputs);
	Inputs[0].Set<const FCompositeCorePassProxy*>(DivisionProxy);
	Inputs[1] = GetDefaultSecondInput(InContext);

	OutProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), GetMergeOperation(InContext), TEXT("ShadowRefl Mul"));
	return true;
}

const TArray<TSoftObjectPtr<AActor>> UCompositeLayerShadowReflection::GetActors() const
{
	return Actors;
}

void UCompositeLayerShadowReflection::SetActors(TArray<TSoftObjectPtr<AActor>> InActors)
{
	RestorePrimitiveVisibilityState();

	Actors = MoveTemp(InActors);

	// Update hidden components
	TArray<TWeakObjectPtr<UPrimitiveComponent> > HiddenComponents;
	HiddenComponents.Reserve(Actors.Num());

	for (const TSoftObjectPtr<AActor>& SoftActor : Actors)
	{
		if (const AActor* Actor = SoftActor.Get())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					HiddenComponents.Add(PrimitiveComponent);
				}
			}
		}
	}

	CachePrimitiveVisibilityState(HiddenComponents);

	UpdatePrimitiveVisibilityState();

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return;
	}

	if (!CachedSceneCaptures[0].IsValid() || !CachedSceneCaptures[1].IsValid())
	{
		CachedSceneCaptures = FindOrCreateSceneCapturePair(*CompositeActor);
	}

	// Early exit if the cached scene captures are not valid
	if (!ensure(CachedSceneCaptures[0].IsValid() && CachedSceneCaptures[1].IsValid()))
	{
		return;
	}

	// First, we empty hidden actors since our previous logic used them
	CachedSceneCaptures[1]->HiddenActors.Empty();
	CachedSceneCaptures[1]->HiddenComponents = HiddenComponents;
}

void UCompositeLayerShadowReflection::CachePrimitiveVisibilityState(TArrayView<TWeakObjectPtr<UPrimitiveComponent>> InPrimitives)
{
	CachedVisibilityInSceneCapture.Reset();

	for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : InPrimitives)
	{
		TStrongObjectPtr<UPrimitiveComponent> Primitive = PrimitiveComponent.Pin();
		if (Primitive.IsValid())
		{
			FCompositeShadowReflectionPrimitiveState& State = CachedVisibilityInSceneCapture.Add(Primitive.Get());
			State.bHiddenInSceneCapture = Primitive->bHiddenInSceneCapture;
			State.bAffectIndirectLightingWhileHidden = Primitive->bAffectIndirectLightingWhileHidden;
			State.bCastHiddenShadow = Primitive->bCastHiddenShadow;
		}
	}
}

void UCompositeLayerShadowReflection::UpdatePrimitiveVisibilityState()
{
	bool bHideActorsInSceneCapture = false;

	switch (AutoConfigureActors)
	{
	case ECompositeHiddenInSceneCaptureConfiguration::None:
		return;
	case ECompositeHiddenInSceneCaptureConfiguration::Hidden:
		bHideActorsInSceneCapture = true;
		break;
	case ECompositeHiddenInSceneCaptureConfiguration::Visible:
	default:
		break;
	}

	for (TSoftObjectPtr<AActor>& Actor : Actors)
	{
		if (Actor.IsValid())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					PrimitiveComponent->Modify();
					PrimitiveComponent->SetHiddenInSceneCapture(bHideActorsInSceneCapture);
					PrimitiveComponent->SetAffectIndirectLightingWhileHidden(bHideActorsInSceneCapture);
					PrimitiveComponent->SetCastHiddenShadow(bHideActorsInSceneCapture);
				}
			}
		}
	}
}

void UCompositeLayerShadowReflection::RestorePrimitiveVisibilityState()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FCompositeShadowReflectionPrimitiveState>& Pair : CachedVisibilityInSceneCapture)
	{
		TStrongObjectPtr< UPrimitiveComponent> Primitive = Pair.Key.Pin();
		if (Primitive.IsValid())
		{
			const FCompositeShadowReflectionPrimitiveState& State = Pair.Value;

			Primitive->Modify();
			Primitive->SetHiddenInSceneCapture(State.bHiddenInSceneCapture);
			Primitive->SetAffectIndirectLightingWhileHidden(State.bAffectIndirectLightingWhileHidden);
			Primitive->SetCastHiddenShadow(State.bCastHiddenShadow);
		}
	}

	CachedVisibilityInSceneCapture.Reset();
}

TStaticArray<TWeakObjectPtr<UCompositeShadowReflectionCatcherComponent>, 2> UCompositeLayerShadowReflection::FindOrCreateSceneCapturePair(ACompositeActor& InOuter) const
{
	return {
		InOuter.FindOrCreateSceneCapture<UCompositeShadowReflectionCatcherComponent>(this, 0, FName("ShadowReflectionCatcher_CG")),
		InOuter.FindOrCreateSceneCapture<UCompositeShadowReflectionCatcherComponent>(this, 1, FName("ShadowReflectionCatcher_NoCG"))
	};
}

