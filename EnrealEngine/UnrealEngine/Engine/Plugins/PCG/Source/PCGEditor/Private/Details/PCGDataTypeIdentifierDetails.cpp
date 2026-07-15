// Copyright Epic Games, Inc. All Rights Reserved.

#include "Details/PCGDataTypeIdentifierDetails.h"

#include "PCGModule.h"
#include "Helpers/PCGHelpers.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"

namespace PCGDataTypeIdentifierDetails
{
	/** Walks property parent hierarchy until a property is found that belongs to a UObject. */
	TSharedPtr<IPropertyHandle> GetFirstUObjectParentProperty(TSharedPtr<IPropertyHandle> InPropertyHandle)
	{
		while (InPropertyHandle.IsValid())
		{
			if (const FProperty* Property = InPropertyHandle->GetProperty())
			{
				const UStruct* OwnerStruct = Property->GetOwnerStruct();

				// OwnerStruct will be a UClass if this property is declared on a UObject.
				if (OwnerStruct && OwnerStruct->IsA<UClass>())
				{
					return InPropertyHandle;
				}
			}

			InPropertyHandle = InPropertyHandle->GetParentHandle();
		}

		return nullptr;
	}

	// Check if the identifier is a superset of other.
	bool IsSuperSet(const FPCGDataTypeIdentifier& Identifier, const FPCGDataTypeBaseId& Other)
	{
		return (Identifier & Other) == Other;
	}

	/**
	 * Look for a function on the outer object of the property or a flag in the property metadata to check if the widget
	 * should support composition (have its bitflag-like behavior enabled) or not. 
	 */
	bool SupportComposition(TSharedRef<IPropertyHandle> InPropertyHandle)
	{
		const FString& SupportCompositionFunctionName = InPropertyHandle->GetMetaData(PCGObjectMetadata::DataTypeIdentifierSupportsComposition);
		if (!SupportCompositionFunctionName.IsEmpty())
		{
			TArray<UObject*> OutObjects;
			InPropertyHandle->GetOuterObjects(OutObjects);

			if (OutObjects.IsEmpty() || !OutObjects[0])
			{
				return false;
			}

			UObject* Object = OutObjects[0];
			// Strip `()` that could be at the end of the function name
			const FString FunctionName = SupportCompositionFunctionName.Replace(TEXT("()"), TEXT(""));
			
			if (!Object->GetClass() || !Object->GetClass()->FindFunctionByName(*FunctionName))
			{
				return false;
			}

			DECLARE_DELEGATE_RetVal(bool, FSupportComposition);
			auto Func = FSupportComposition::CreateUFunction(Object, *FunctionName);
			return Func.IsBound() ? Func.Execute() : false;
		}
		else if (InPropertyHandle->HasMetaData(PCGObjectMetadata::DataTypeIdentifierSupportsComposition))
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	/**
	 * Look for a function on the outer object of the property (or its parent properties) to check if the widget should filter out any types.
	 */
	TOptional<TArray<FPCGDataTypeIdentifier>> GetFilter(TSharedRef<IPropertyHandle> InPropertyHandle)
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = GetFirstUObjectParentProperty(InPropertyHandle.ToSharedPtr());

		if (!PropertyHandle)
		{
			return {};
		}

		const FString& GetFilterFunctionName = PropertyHandle->GetMetaData(PCGObjectMetadata::DataTypeIdentifierFilter);

		if (!GetFilterFunctionName.IsEmpty())
		{
			TArray<UObject*> OutObjects;
			PropertyHandle->GetOuterObjects(OutObjects);

			if (OutObjects.IsEmpty() || !OutObjects[0])
			{
				return {};
			}

			UObject* Object = OutObjects[0];
			// Strip `()` that could be at the end of the function name
			const FString FunctionName = GetFilterFunctionName.Replace(TEXT("()"), TEXT(""));
			
			if (!Object->GetClass() || !Object->GetClass()->FindFunctionByName(*FunctionName))
			{
				return {};
			}

			DECLARE_DELEGATE_RetVal(TArray<FPCGDataTypeIdentifier>, FSupportComposition);
			auto Func = FSupportComposition::CreateUFunction(Object, *FunctionName);
			
			if (Func.IsBound())
			{
				return Func.Execute();
			}
		}

		return {};
	}
}

void FPCGDataTypeIdentifierDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	check(PropertyHandle.IsValid());

	TSharedRef<SWidget> PropertyNameWidget = PropertyHandle->CreatePropertyNameWidget();
	
	const TOptional<TArray<FPCGDataTypeIdentifier>> Filter = PCGDataTypeIdentifierDetails::GetFilter(InPropertyHandle);
	bSupportComposition = PCGDataTypeIdentifierDetails::SupportComposition(InPropertyHandle);

	const FPCGDataTypeRegistry& Registry = FPCGModule::GetConstDataTypeRegistry();
	
	HiddenTypes.Empty();
	VisibleTypes.Empty();

	// Do a depth search so types are ordered by category.
	Registry.HierarchyDepthSearch([this, &Filter, &Registry](const FPCGDataTypeBaseId& DataType, int32 Depth) -> FPCGDataTypeRegistry::ESearchCommand
	{
		const FPCGDataTypeInfo* TypeInfo = Registry.GetTypeInfo(DataType);
		check(TypeInfo);

		const bool bInFilter = !Filter.IsSet() || Filter.GetValue().Contains(DataType);

		// Remove hidden or filtered types
		if (TypeInfo->Hidden() || !bInFilter)
		{
			HiddenTypes.Add(DataType);
		}
		else
		{
			VisibleTypes.Emplace(DataType, Depth);
		}

		return FPCGDataTypeRegistry::ESearchCommand::ExpandAndContinue;
	});

	HeaderRow
		.NameContent()
		[
			PropertyNameWidget
		]
		.ValueContent()
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &FPCGDataTypeIdentifierDetails::OnGetMenuContent)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &FPCGDataTypeIdentifierDetails::GetText)
				.ToolTipText(this, &FPCGDataTypeIdentifierDetails::GetTooltip)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

TSharedRef<SWidget> FPCGDataTypeIdentifierDetails::OnGetMenuContent()
{
	const bool bCloseAfterSelection = !bSupportComposition;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, nullptr, nullptr, true);

	const FPCGDataTypeRegistry& Registry = FPCGModule::GetConstDataTypeRegistry();

	for (const auto& [DataType, Depth] : VisibleTypes)
	{
		const FPCGDataTypeInfo* TypeInfo = Registry.GetTypeInfo(DataType);
		check(TypeInfo);
		
		MenuBuilder.AddMenuEntry(
			FText::FromString(DataType.ToString()),
			FText{},
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([this, ThisDataType = DataType]()
				{
					TArray<UObject*> OuterObjects;
					PropertyHandle->GetOuterObjects(OuterObjects);
					if (OuterObjects.IsEmpty())
					{
						return;
					}

					FPCGDataTypeIdentifier* Identifier = GetStruct();
					if (!ensure(Identifier))
					{
						return;
					}

					FPCGDataTypeIdentifier TempIdentifier{};

					if (bSupportComposition && FSlateApplication::Get().GetModifierKeys().IsControlDown())
					{
						if (!PCGDataTypeIdentifierDetails::IsSuperSet(*Identifier, ThisDataType))
						{
							TempIdentifier = Identifier->Compose(ThisDataType);
						}
						else
						{
							const FPCGDataTypeIdentifier DataTypeIdentifier{ ThisDataType };
							FPCGDataTypeRegistry::FGetIdentifiersDifferenceParams Params = {
								.SourceIdentifier = Identifier,
								.DifferenceIdentifiers = MakeConstArrayView<FPCGDataTypeIdentifier>(&DataTypeIdentifier, 1),
								.Filter = FPCGDataTypeRegistry::FGetIdentifiersDifferenceParams::ExcludeFilteredTypes,
								.FilteredTypes = HiddenTypes
							};
							
							TempIdentifier = FPCGModule::GetConstDataTypeRegistry().GetIdentifiersDifference(Params);
						}
					}
					else
					{
						TempIdentifier = ThisDataType;
					}

					if (TempIdentifier.IsValid())
					{
						// Need to export text and import text to go through the normal flow (like update Blueprint instances)
						UObject* Outer = OuterObjects[0];
						FString IdentifierAsText{};
						FPCGDataTypeIdentifier::StaticStruct()->ExportText(IdentifierAsText, &TempIdentifier, /*Defaults=*/nullptr, Outer, PPF_None, /*ExportRootScope=*/nullptr);
						if (!IdentifierAsText.IsEmpty())
						{
							FScopedTransaction Transaction(NSLOCTEXT("PCGDataTypeIdentifierDetails", "ModifyIdentifier", "Modify PCG Data Type Identifier."));
							PropertyHandle->SetPerObjectValues({MoveTemp(IdentifierAsText)});
						}
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, ThisDataType = DataType]() -> bool
				{
					FPCGDataTypeIdentifier* Identifier = GetStruct();
					return ensure(Identifier) ? PCGDataTypeIdentifierDetails::IsSuperSet(*Identifier, ThisDataType) : false;
				})
			),
			NAME_None,
			bSupportComposition ? EUserInterfaceActionType::Check : EUserInterfaceActionType::None);
	}

	return MenuBuilder.MakeWidget();
}

FText FPCGDataTypeIdentifierDetails::GetText() const
{
	static const FPCGDataTypeIdentifier DefaultInvalidDataType{};

	const FPCGDataTypeIdentifier* DataType = GetStruct();
	return DataType ? DataType->ToDisplayText() : DefaultInvalidDataType.ToDisplayText();
}

FText FPCGDataTypeIdentifierDetails::GetTooltip() const
{
	static const FText CompositionTooltip = NSLOCTEXT("PCGDataTypeIdentifierDetails", "CompositionTooltip", "\n---\nTypes can be composed like bitflags.\n"
	"By default, selecting a type will remove all the others, no composition.\n"
	"A broader type will select all its subtypes (like BaseTexture selects Texture and RenderTarget, or Any selects everything)\n"
	"Use 'Ctrl + Click' to add/remove another type to the composition.");

	return FText::Format(INVTEXT("{0}{1}"), GetText(), bSupportComposition ? CompositionTooltip : FText{});
}

FPCGDataTypeIdentifier* FPCGDataTypeIdentifierDetails::GetStruct()
{
	void* Data = nullptr;
	FPropertyAccess::Result Result = PropertyHandle->GetValueData(Data);

	return (Result == FPropertyAccess::Success) ? static_cast<FPCGDataTypeIdentifier*>(Data) : nullptr;
}

const FPCGDataTypeIdentifier* FPCGDataTypeIdentifierDetails::GetStruct() const
{
	return const_cast<FPCGDataTypeIdentifierDetails&>(*this).GetStruct();
}
