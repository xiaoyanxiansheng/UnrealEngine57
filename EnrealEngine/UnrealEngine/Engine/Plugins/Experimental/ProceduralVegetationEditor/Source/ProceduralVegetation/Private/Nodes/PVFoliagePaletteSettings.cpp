// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVFoliagePaletteSettings.h"

#include "ProceduralVegetationModule.h"
#include "DataTypes/PVData.h"
#include "DataTypes/PVMeshData.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGStaticMeshResourceData.h"
#include "Facades/PVFoliageFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Helpers/PVAnalyticsHelper.h"

#define LOCTEXT_NAMESPACE "PVFoliagePaletteSettings"

#if WITH_EDITOR
FText UPVFoliagePaletteSettings::GetDefaultNodeTitle() const 
{ 
	return LOCTEXT("NodeTitle", "Foliage Palette");
}

FText UPVFoliagePaletteSettings::GetNodeTooltipText() const 
{ 
	return LOCTEXT("NodeTooltip", 
		"Allows the user to visualize and optionally change the foliage meshes. "
		"Foliage mesh data is loaded from the Procedrual Vegetation preset. "
		"The user can replace or remove foliage meshes. The placement of folliage meshes is driven by the procedural vegetaiton preset and cannot be modifed"
		"\n\nPress Ctrl + L to lock/unlock node output"
	);
}

void UPVFoliagePaletteSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPVFoliagePaletteSettings, bOverrideFoliage))
	{
		DirtyCache();
	}
}

void UPVFoliagePaletteSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;

	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (MemberProperty && Property)
	{
		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPVFoliagePaletteSettings, FoliageMeshes))
		{
			FName Name = Property->GetFName();
			
			int Index = PropertyChangedEvent.GetArrayIndex(Name.ToString());

			if (Index >= 0 && FoliageMeshes.Num() > Index)
			{
				if (!FoliageMeshes[Index].IsNull())
				{
					FString MeshPath = FoliageMeshes[Index].ToString();

					PV::Analytics::SendFoliageMeshChangeEvent(MeshPath);	
				}
			}
		}
	}
}

#endif

FPCGDataTypeIdentifier UPVFoliagePaletteSettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoMesh::AsId() };
}

FPCGDataTypeIdentifier UPVFoliagePaletteSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoMesh::AsId() };
}

FPCGElementPtr UPVFoliagePaletteSettings::CreateElement() const
{
	return MakeShared<FPVFoliageElement>();
}

bool FPVFoliageElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVFoliageElement::Execute);

	check(InContext);

	const UPVFoliagePaletteSettings* Settings = InContext->GetInputSettings<UPVFoliagePaletteSettings>();
	check(Settings);

	UPVFoliagePaletteSettings* const MutableSettings = const_cast<UPVFoliagePaletteSettings*>(Settings);
	check(MutableSettings);

	const TArray<FPCGTaggedData>& Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (!Inputs.IsEmpty())
	{
		if (const UPVMeshData* InputData = Cast<UPVMeshData>(Inputs[0].Data))
		{
			FManagedArrayCollection Collection = InputData->GetCollection();

			UPVMeshData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVMeshData>(InContext);

			PV::Facades::FFoliageFacade Facade(Collection);

			if (!Settings->bOverrideFoliage)
			{
				MutableSettings->FoliageMeshes.Empty();
				MutableSettings->bOverrideFoliage = false;
				for (int32 FoliageIndex = 0; FoliageIndex < Facade.NumFoliageNames(); ++FoliageIndex)
				{
					MutableSettings->FoliageMeshes.Emplace(FSoftObjectPath(Facade.GetFoliageName(FoliageIndex)));
				}
#if WITH_EDITOR
				MutableSettings->PostEditChange();
#endif
			}

			const int32 IterationLimit = FMath::Min(Settings->FoliageMeshes.Num(), Facade.NumFoliageNames());
			TArray<FString> FoliageNames;
			for (int32 i = 0; i < IterationLimit; ++i)
			{
				FoliageNames.Emplace(Settings->FoliageMeshes[i].ToString());
			}

			if (Settings->FoliageMeshes.Num() > Facade.NumFoliageNames())
			{
				UE_LOG(LogProceduralVegetation, Warning,
				       TEXT("Num of Foliage meshes specified is greater than the number of available entries, ignoring the last %d entries"),
				       Settings->FoliageMeshes.Num() - Facade.NumFoliageNames());
			}
			else if (Settings->FoliageMeshes.Num() < Facade.NumFoliageNames())
			{
				if (Settings->FoliageMeshes.Num() > 0)
				{
					UE_LOG(LogProceduralVegetation, Warning,
					       TEXT("Num of Foliage meshes specified is less than the number of available entries, repeating the last entry"));

					for (int32 i = Settings->FoliageMeshes.Num(); i < Facade.NumFoliageNames(); ++i)
					{
						FoliageNames.Emplace(Settings->FoliageMeshes.Last().ToString());
					}
				}
			}

			Facade.SetFoliageNames(FoliageNames);

			OutManagedArrayCollectionData->Initialize(MoveTemp(Collection));
			InContext->OutputData.TaggedData.Emplace(OutManagedArrayCollectionData);
		}
		else
		{
			PCGLog::InputOutput::LogInvalidInputDataError(InContext);
			return true;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE