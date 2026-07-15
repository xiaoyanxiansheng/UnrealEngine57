// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEvaluationScopeInstanceContainer.h"

#include "UObject/Class.h"
#include "UObject/Package.h"
#include <type_traits>

namespace UE::StateTree::InstanceData
{
namespace Private
{
	static constexpr uint32 DebugTableEndTag = 0x99AABBCC;
	static constexpr uint32 DebugStructEndTag = 0xFFEEDDCC;
	void Add(FEvaluationScopeInstanceContainer::FMemoryRequirement& MemoryRequirement, TNotNull<const UScriptStruct*> Struct)
	{
		const int32 StructSize = Struct->GetStructureSize();
		const int32 StructAlignment = Struct->GetMinAlignment();
		MemoryRequirement.Alignment = FMath::Max(StructAlignment, MemoryRequirement.Alignment);
		MemoryRequirement.Size = Align(MemoryRequirement.Size, StructAlignment);
		MemoryRequirement.Size += StructSize;
		MemoryRequirement.Size += sizeof(DebugStructEndTag); // for struct end tag
	}
}

void FEvaluationScopeInstanceContainer::FMemoryRequirementBuilder::Add(TNotNull<const UScriptStruct*> Struct)
{
	if (!Struct->IsChildOf(FStateTreeInstanceObjectWrapper::StaticStruct()))
	{
		Private::Add(MemoryRequirement, Struct);
		if (FirstStructAlignment <= 0)
		{
			FirstStructAlignment = Struct->GetMinAlignment();
		}
	}
	++MemoryRequirement.NumberOfElements;
}

FEvaluationScopeInstanceContainer::FMemoryRequirement FEvaluationScopeInstanceContainer::FMemoryRequirementBuilder::Build()
{
	int32 ContainerSize = sizeof(FItem) * MemoryRequirement.NumberOfElements;
	ContainerSize += sizeof(Private::DebugTableEndTag); // table for end tag
	ContainerSize = Align(ContainerSize, FirstStructAlignment);
	FMemoryRequirement Result = MemoryRequirement;
	Result.Size += ContainerSize;
	return Result;
}

FEvaluationScopeInstanceContainer::FEvaluationScopeInstanceContainer(TNotNull<void*> InMemory, const FMemoryRequirement& InRequirement)
	: Memory(InMemory)
	, MemoryRequirement(InRequirement)
{
	AddTableDebugTag();
}

FEvaluationScopeInstanceContainer::~FEvaluationScopeInstanceContainer()
{
	Reset();
}

void FEvaluationScopeInstanceContainer::Add(FStateTreeDataHandle DataHandle, FConstStructView DefaultInstance)
{
	check(NumberOfElements < MemoryRequirement.NumberOfElements);

	FItem* ItemContainer = static_cast<FItem*>(Memory);
	const UScriptStruct* Struct = DefaultInstance.GetScriptStruct();
	if (!Struct->IsChildOf(FStateTreeInstanceObjectWrapper::StaticStruct()))
	{
		void* MemoryForNewItem = (NumberOfElements > 0)
			? (uint8*)ItemContainer[NumberOfElements - 1].Instance.GetMutableMemory()
				+ ItemContainer[NumberOfElements - 1].Instance.GetStruct()->GetStructureSize()
				+ sizeof(Private::DebugStructEndTag)
			: (uint8*)Memory + (MemoryRequirement.NumberOfElements * sizeof(FItem))
				+ sizeof(Private::DebugTableEndTag);
		MemoryForNewItem = Align(MemoryForNewItem, Struct->GetMinAlignment());

		// Initialize the instance data
		constexpr int32 ArrayDim = 1;
		Struct->InitializeStruct(MemoryForNewItem);
		Struct->CopyScriptStruct(MemoryForNewItem, DefaultInstance.GetMemory(), ArrayDim);

		// Initialize the item in the table
		new (ItemContainer + NumberOfElements)FItem();
		ItemContainer[NumberOfElements].Instance = FStateTreeDataView(Struct, (uint8*)MemoryForNewItem);
		ItemContainer[NumberOfElements].DataHandle = DataHandle;

		AddStructDebugTag(NumberOfElements);

		if ((Struct->StructFlags & (STRUCT_IsPlainOldData | STRUCT_NoDestructor)) == 0)
		{
			bStructsHaveDestructor = true;
		}
	}
	else
	{
		const FStateTreeInstanceObjectWrapper& Wrapper = DefaultInstance.Get<const FStateTreeInstanceObjectWrapper>();
		if (Wrapper.InstanceObject)
		{
			UObject* DuplicatedObject = ::DuplicateObject(Wrapper.InstanceObject, GetTransientPackage());

			new (ItemContainer + NumberOfElements)FItem();
			ItemContainer[NumberOfElements].Instance = FStateTreeDataView(DuplicatedObject);
			ItemContainer[NumberOfElements].DataHandle = DataHandle;
		}
	}
	++NumberOfElements;
}

void FEvaluationScopeInstanceContainer::Reset()
{
	TestDebugTags();

	if (bStructsHaveDestructor)
	{
		for (; NumberOfElements > 0; --NumberOfElements)
		{
			FItem* ItemContainer = static_cast<FItem*>(Memory);
			if (!ItemContainer[NumberOfElements - 1].DataHandle.IsObjectSource())
			{
				FStateTreeDataView& Instance = ItemContainer[NumberOfElements - 1].Instance;
				Instance.GetStruct()->DestroyStruct(Instance.GetMutableMemory());
			}
			if constexpr (!std::is_trivially_destructible_v<FItem>)
			{
				ItemContainer->~FItem();
			}
		}
		check(NumberOfElements == 0);
	}
	else if constexpr (!std::is_trivially_destructible_v<FItem>)
	{
		for (; NumberOfElements > 0; --NumberOfElements)
		{
			FItem* ItemContainer = static_cast<FItem*>(Memory);
			ItemContainer->~FItem();
		}
		check(NumberOfElements == 0);
	}
	NumberOfElements = 0;
}

FEvaluationScopeInstanceContainer::FItem* FEvaluationScopeInstanceContainer::Get(const FStateTreeDataHandle& DataHandle) const
{
	FItem* ItemContainer = static_cast<FItem*>(Memory);
	for (int32 Index = 0; Index < NumberOfElements; ++Index)
	{
		if (ItemContainer[Index].DataHandle == DataHandle)
		{
			return &ItemContainer[Index];
		}
	}
	return nullptr;
}

void FEvaluationScopeInstanceContainer::AddTableDebugTag()
{
#if WITH_STATETREE_DEBUG
	void* MemoryForDebugTag = (uint8*)Memory + (sizeof(FItem) * MemoryRequirement.NumberOfElements);
	*reinterpret_cast<uint32*>(MemoryForDebugTag) = Private::DebugTableEndTag;
#endif
}

void FEvaluationScopeInstanceContainer::AddStructDebugTag(int32 Index)
{
#if WITH_STATETREE_DEBUG
	FItem* ItemContainer = static_cast<FItem*>(Memory);
	void* MemoryForDebugTag = (uint8*)ItemContainer[Index].Instance.GetMutableMemory()
		+ ItemContainer[Index].Instance.GetStruct()->GetStructureSize();
	*reinterpret_cast<uint32*>(MemoryForDebugTag) = Private::DebugStructEndTag;
#endif
}

void FEvaluationScopeInstanceContainer::TestDebugTags() const
{
#if WITH_STATETREE_DEBUG && DO_ENSURE
	CA_SUPPRESS(6269); // warning C6269: Possibly incorrect order of operations.
	if (Memory)
	{
		const void* MemoryForTableDebugTag = (const uint8*)Memory + (sizeof(FItem) * MemoryRequirement.NumberOfElements);
		const int32 TableEndTag = *reinterpret_cast<const uint32*>(MemoryForTableDebugTag);
		ensure(TableEndTag == Private::DebugTableEndTag);

		FItem* ItemContainer = static_cast<FItem*>(Memory);
		for (int32 Index = 0; Index < NumberOfElements; ++Index)
		{
			const void* MemoryForStructDebugTag = (const uint8*)ItemContainer[Index].Instance.GetMutableMemory()
				+ ItemContainer[Index].Instance.GetStruct()->GetStructureSize();
			const int32 StructEndTag = *reinterpret_cast<const uint32*>(MemoryForStructDebugTag);
			ensure(StructEndTag == Private::DebugStructEndTag);
		}
	}
#endif
}

} // namespace UE::StateTree::InstanceData
