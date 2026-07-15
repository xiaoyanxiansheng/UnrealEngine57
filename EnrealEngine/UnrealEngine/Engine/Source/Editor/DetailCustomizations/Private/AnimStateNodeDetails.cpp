// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimStateNodeDetails.h"

#include "AnimGraphNode_StateResult.h"
#include "AnimStateNode.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Fonts/SlateFontInfo.h"
#include "HAL/Platform.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SToolTip.h"

class IDetailCustomization;

#define LOCTEXT_NAMESPACE "FAnimStateNodeDetails"

/////////////////////////////////////////////////////////////////////////


TSharedRef<IDetailCustomization> FAnimStateNodeDetails::MakeInstance()
{
	return MakeShareable( new FAnimStateNodeDetails );
}

void FAnimStateNodeDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	TArray<TWeakObjectPtr<UAnimStateNode>> StateNodes = DetailBuilder.GetObjectsOfTypeBeingCustomized<UAnimStateNode>();
	TArray<UObject*> StateResultNodes;
	for (TWeakObjectPtr<UAnimStateNode> WeakStateNode : StateNodes)
	{
		UAnimStateNode* StateNode = WeakStateNode.Get();
		if (StateNode == nullptr)
		{
			continue;
		}

		UAnimGraphNode_StateResult* StateResultNode = StateNode->GetResultNodeInsideState();
		if (StateResultNode == nullptr)
		{
			continue;
		}

		StateResultNodes.Add(StateResultNode);
	}

	IDetailCategoryBuilder& AnimationStateCategory = DetailBuilder.EditCategory("Animation State", LOCTEXT("AnimationState", "Animation State"));

	IDetailPropertyRow* Row = AnimationStateCategory.AddExternalObjects(StateResultNodes, EPropertyLocation::Default, FAddPropertyParams().HideRootObjectNode(true));
	if (Row != nullptr)
	{
		Row->ShouldAutoExpand(true);
	}

	IDetailCategoryBuilder& DeprecatedCategory = DetailBuilder.EditCategory("Deprecated", LOCTEXT("DeprecatedCategory", "Deprecated"));
	DeprecatedCategory.InitiallyCollapsed(true);
	
	GenerateAnimationStateEventRow(DeprecatedCategory, LOCTEXT("EnteredAnimationStateEventLabel", "Entered State Event"), TEXT("StateEntered"));
	GenerateAnimationStateEventRow(DeprecatedCategory, LOCTEXT("ExitedAnimationStateEventLabel", "Left State Event"), TEXT("StateLeft"));
	GenerateAnimationStateEventRow(DeprecatedCategory, LOCTEXT("FullyBlendedAnimationStateEventLabel", "Fully Blended State Event"), TEXT("StateFullyBlended"));

	DetailBuilder.HideProperty("StateEntered");
	DetailBuilder.HideProperty("StateLeft");
	DetailBuilder.HideProperty("StateFullyBlended");
}

void FAnimStateNodeDetails::GenerateAnimationStateEventRow(IDetailCategoryBuilder& AnimationStateCategory,const FText& StateEventLabel, const FString& TransitionName)
{
	CreateTransitionEventPropertyWidgets(AnimationStateCategory, TransitionName, true);
}

#undef LOCTEXT_NAMESPACE
