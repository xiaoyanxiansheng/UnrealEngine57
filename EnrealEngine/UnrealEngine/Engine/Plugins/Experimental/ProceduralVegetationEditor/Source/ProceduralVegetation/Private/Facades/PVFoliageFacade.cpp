// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facades/PVFoliageFacade.h"

#include "ProceduralVegetationModule.h"
#include "VisualizeTexture.h"
#include "Chaos/Deformable/MuscleActivationConstraints.h"
#include "Facades/PVAttributesNames.h"

namespace PV::Facades
{
	FFoliageFacade::FFoliageFacade(FManagedArrayCollection& InCollection, const int32 InitialSize)
		: ConstCollection(InCollection)
		, Collection(&InCollection)
		, NamesAttribute(InCollection, AttributeNames::FoliageName, GroupNames::FoliageNamesGroup)
		, NameIdsAttribute(InCollection, AttributeNames::FoliageNameID, GroupNames::FoliageGroup)
		, BranchIdsAttribute(InCollection, AttributeNames::FoliageBranchID, GroupNames::FoliageGroup)
		, PivotPointsAttribute(InCollection, AttributeNames::FoliagePivotPoint, GroupNames::FoliageGroup)
		, UpVectorsAttribute(InCollection, AttributeNames::FoliageUPVector, GroupNames::FoliageGroup)
		, NormalVectorsAttribute(InCollection, AttributeNames::FoliageNormalVector, GroupNames::FoliageGroup)
		, ScalesAttribute(InCollection, AttributeNames::FoliageScale, GroupNames::FoliageGroup)
		, LengthFromRootAttribute(InCollection, AttributeNames::FoliageLengthFromRoot, GroupNames::FoliageGroup)
		, ParentBoneIdsAttribute(InCollection, AttributeNames::FoliageParentBoneID, GroupNames::FoliageGroup)
		, FoliageIdsAttribute(InCollection, AttributeNames::BranchFoliageIDs, GroupNames::BranchGroup)
		, FoliagePathAttribute(InCollection, AttributeNames::FoliagePath, GroupNames::DetailsGroup)
	{
		DefineSchema(InitialSize);
	}

	FFoliageFacade::FFoliageFacade(const FManagedArrayCollection& InCollection)
		: ConstCollection(InCollection)
		, Collection(nullptr)
		, NamesAttribute(InCollection, AttributeNames::FoliageName, GroupNames::FoliageNamesGroup)
		, NameIdsAttribute(InCollection, AttributeNames::FoliageNameID, GroupNames::FoliageGroup)
		, BranchIdsAttribute(InCollection, AttributeNames::FoliageBranchID, GroupNames::FoliageGroup)
		, PivotPointsAttribute(InCollection, AttributeNames::FoliagePivotPoint, GroupNames::FoliageGroup)
		, UpVectorsAttribute(InCollection, AttributeNames::FoliageUPVector, GroupNames::FoliageGroup)
		, NormalVectorsAttribute(InCollection, AttributeNames::FoliageNormalVector, GroupNames::FoliageGroup)
		, ScalesAttribute(InCollection, AttributeNames::FoliageScale, GroupNames::FoliageGroup)
		, LengthFromRootAttribute(InCollection, AttributeNames::FoliageLengthFromRoot, GroupNames::FoliageGroup)
		, ParentBoneIdsAttribute(InCollection, AttributeNames::FoliageParentBoneID, GroupNames::FoliageGroup)
		, FoliageIdsAttribute(InCollection, AttributeNames::BranchFoliageIDs, GroupNames::BranchGroup)
		, FoliagePathAttribute(InCollection, AttributeNames::FoliagePath, GroupNames::DetailsGroup)
	{
	}

	void FFoliageFacade::DefineSchema(const int32 InitialSize)
	{
		check(!IsConst());

		if (!Collection->HasGroup(GroupNames::FoliageGroup))
		{
			Collection->AddGroup(GroupNames::FoliageGroup);
			if (InitialSize > 0)
			{
				Collection->AddElements(InitialSize, GroupNames::FoliageGroup);
			}
		}

		if (!Collection->HasGroup(GroupNames::FoliageNamesGroup))
		{
			Collection->AddGroup(GroupNames::FoliageNamesGroup);
		}

		if (!Collection->HasGroup(GroupNames::DetailsGroup))
		{
			Collection->AddGroup(GroupNames::DetailsGroup);
		}

		NamesAttribute.Add();
		NameIdsAttribute.Add();
		BranchIdsAttribute.Add();
		PivotPointsAttribute.Add();
		UpVectorsAttribute.Add();
		NormalVectorsAttribute.Add();
		ScalesAttribute.Add();
		LengthFromRootAttribute.Add();
		ParentBoneIdsAttribute.Add();
		FoliageIdsAttribute.Add();
		FoliagePathAttribute.Add();
	}

	bool FFoliageFacade::IsValid() const
	{
		return NamesAttribute.IsValid() && NameIdsAttribute.IsValid() && BranchIdsAttribute.IsValid() && PivotPointsAttribute.IsValid() &&
			UpVectorsAttribute.IsValid() && NormalVectorsAttribute.IsValid() && ScalesAttribute.IsValid() && LengthFromRootAttribute.IsValid() &&
			FoliageIdsAttribute.IsValid() && ParentBoneIdsAttribute.IsValid();
	}

	FFoliageEntryData FFoliageFacade::GetFoliageEntry(const int32 Index) const
	{
		FFoliageEntryData ReturnData = {};
		if (IsValid() && Index > -1)
		{
			if (NameIdsAttribute.IsValidIndex(Index))
			{
				ReturnData.NameId = NameIdsAttribute.Get()[Index];
			}
			if (BranchIdsAttribute.IsValidIndex(Index))
			{
				ReturnData.BranchId = BranchIdsAttribute.Get()[Index];
			}
			if (PivotPointsAttribute.IsValidIndex(Index))
			{
				ReturnData.PivotPoint = PivotPointsAttribute.Get()[Index];
			}
			if (UpVectorsAttribute.IsValidIndex(Index))
			{
				ReturnData.UpVector = UpVectorsAttribute.Get()[Index];
			}
			if (NormalVectorsAttribute.IsValidIndex(Index))
			{
				ReturnData.NormalVector = NormalVectorsAttribute.Get()[Index];
			}
			if (ScalesAttribute.IsValidIndex(Index))
			{
				ReturnData.Scale = ScalesAttribute.Get()[Index];
			}
			if (LengthFromRootAttribute.IsValidIndex(Index))
			{
				ReturnData.LengthFromRoot = LengthFromRootAttribute.Get()[Index];
			}
			if (ParentBoneIdsAttribute.IsValidIndex(Index))
			{
				ReturnData.ParentBoneID = ParentBoneIdsAttribute.Get()[Index];
			}
		}
		return ReturnData;
	}

	const float& FFoliageFacade::GetLengthFromRoot(const int32 Index) const
	{
		if (LengthFromRootAttribute.IsValid() && LengthFromRootAttribute.IsValidIndex(Index))
		{
			return LengthFromRootAttribute[Index];
		}

		static constexpr float DefaultLengthFromRoot = 0.0;
		return DefaultLengthFromRoot;
	}

	void FFoliageFacade::SetLengthFromRoot(const int32 Index, const float Input)
	{
		check(!IsConst());
		if (IsValid() && LengthFromRootAttribute.IsValidIndex(Index))
		{
			LengthFromRootAttribute.ModifyAt(Index, Input);
		}
	}

	int32 FFoliageFacade::GetParentBoneID(const int32 Index) const
	{
		if (ParentBoneIdsAttribute.IsValid() && ParentBoneIdsAttribute.IsValidIndex(Index))
		{
			return ParentBoneIdsAttribute[Index];
		}

		return INDEX_NONE;
	}

	void FFoliageFacade::SetParentBoneID(const int32 Index, const int32 Input)
	{
		check(!IsConst());
		if (IsValid() && ParentBoneIdsAttribute.IsValidIndex(Index))
		{
			ParentBoneIdsAttribute.ModifyAt(Index, Input);
		}
	}

	const TArray<int32>& FFoliageFacade::GetFoliageEntryIdsForBranch(const int32 Index) const
	{
		if (FoliageIdsAttribute.IsValid() && FoliageIdsAttribute.IsValidIndex(Index))
		{
			return FoliageIdsAttribute[Index];
		}

		static const TArray<int32> EmptyArray;
		return EmptyArray;
	}

	int32 FFoliageFacade::AddFoliageEntry(const FFoliageEntryData& InputData)
	{
		check(!IsConst());
		if (IsValid())
		{
			const int32 NewIndex = NameIdsAttribute.AddElements(1);
			NameIdsAttribute.Modify()[NewIndex] = InputData.NameId;
			BranchIdsAttribute.Modify()[NewIndex] = InputData.BranchId;
			PivotPointsAttribute.Modify()[NewIndex] = InputData.PivotPoint;
			UpVectorsAttribute.Modify()[NewIndex] = InputData.UpVector;
			NormalVectorsAttribute.Modify()[NewIndex] = InputData.NormalVector;
			ScalesAttribute.Modify()[NewIndex] = InputData.Scale;
			LengthFromRootAttribute.Modify()[NewIndex] = InputData.LengthFromRoot;
			ParentBoneIdsAttribute.Modify()[NewIndex] = InputData.ParentBoneID;

			return NewIndex;
		}
		return INDEX_NONE;
	}

	void FFoliageFacade::SetFoliageEntry(const int32 Index, const FFoliageEntryData& InputData)
	{
		check(!IsConst());
		if (IsValid() && Index >= 0)
		{
			NameIdsAttribute.ModifyAt(Index, InputData.NameId);
			BranchIdsAttribute.ModifyAt(Index, InputData.BranchId);
			PivotPointsAttribute.ModifyAt(Index, InputData.PivotPoint);
			UpVectorsAttribute.ModifyAt(Index, InputData.UpVector);
			NormalVectorsAttribute.ModifyAt(Index, InputData.NormalVector);
			ScalesAttribute.ModifyAt(Index, InputData.Scale);
			LengthFromRootAttribute.ModifyAt(Index, InputData.LengthFromRoot);
			LengthFromRootAttribute.ModifyAt(Index, InputData.LengthFromRoot);
			LengthFromRootAttribute.ModifyAt(Index, InputData.LengthFromRoot);
			ParentBoneIdsAttribute.ModifyAt(Index, InputData.ParentBoneID);
		}
	}

	void FFoliageFacade::SetFoliageIdsArray(const int32 Index, const TArray<int32>& InputIds)
	{
		check(!IsConst());
		if (IsValid() && FoliageIdsAttribute.IsValidIndex(Index))
		{
			FoliageIdsAttribute.Modify()[Index] = InputIds;
		}
	}

	void FFoliageFacade::SetFoliageBranchId(const int32 Index, const int32 InputId)
	{
		check(!IsConst());
		if (IsValid() && BranchIdsAttribute.IsValidIndex(Index))
		{
			BranchIdsAttribute.ModifyAt(Index, InputId);
		}
	}

	FString FFoliageFacade::GetFoliageName(const int32 Index) const
	{
		if (NamesAttribute.IsValid() && NamesAttribute.IsValidIndex(Index))
		{
			return NamesAttribute[Index];
		}

		return FString();
	}

	TArray<FString> FFoliageFacade::GetFoliageNames() const
	{
		TArray<FString> FoliageNames;
		if (NamesAttribute.IsValid())
		{
			for (int32 i = 0; i < NamesAttribute.Num(); i++)
			{
				FoliageNames.AddUnique(NamesAttribute[i]);
			}
		}

		return FoliageNames;
	}

	const FVector3f& FFoliageFacade::GetPivotPoint(const int32 Index) const
	{
		if (PivotPointsAttribute.IsValid() && PivotPointsAttribute.IsValidIndex(Index))
		{
			return PivotPointsAttribute[Index];
		}

		static const FVector3f DefaultPivotPoint = FVector3f::Zero();
		return DefaultPivotPoint;
	}

	const FVector3f& FFoliageFacade::GetUpVector(const int32 Index) const
	{
		if (UpVectorsAttribute.IsValid() && UpVectorsAttribute.IsValidIndex(Index))
		{
			return UpVectorsAttribute[Index];
		}

		static const FVector3f DefaultUpVector = FVector3f::Zero();
		return DefaultUpVector;
	}

	const FVector3f& FFoliageFacade::GetNormalVector(const int32 Index) const
	{
		if (NormalVectorsAttribute.IsValid() && NormalVectorsAttribute.IsValidIndex(Index))
		{
			return NormalVectorsAttribute[Index];
		}

		static const FVector3f DefaultNormalVector = FVector3f::Zero();
		return DefaultNormalVector;
	}


	void FFoliageFacade::SetPivotPoint(const int32 Index, const FVector3f& Input)
	{
		check(!IsConst());
		if (IsValid() && PivotPointsAttribute.IsValidIndex(Index))
		{
			PivotPointsAttribute.ModifyAt(Index, Input);
		}
	}

	void FFoliageFacade::SetUpVector(const int32 Index, const FVector3f& Input)
	{
		check(!IsConst());
		if (IsValid() && UpVectorsAttribute.IsValidIndex(Index))
		{
			UpVectorsAttribute.ModifyAt(Index, Input);
		}
	}

	void FFoliageFacade::SetNormalVector(const int32 Index, const FVector3f& Input)
	{
		check(!IsConst());
		if (IsValid() && NormalVectorsAttribute.IsValidIndex(Index))
		{
			NormalVectorsAttribute.ModifyAt(Index, Input);
		}
	}

	void FFoliageFacade::SetScale(const int32 Index, const float& Input)
	{
		check(!IsConst());
		if (IsValid() && ScalesAttribute.IsValidIndex(Index))
		{
			ScalesAttribute.ModifyAt(Index, Input);
		}
	}

	void FFoliageFacade::SetFoliageName(const int32 Index, const FString& Input)
	{
		check(!IsConst());
		if (IsValid() && NamesAttribute.IsValidIndex(Index))
		{
			NamesAttribute.ModifyAt(Index, Input);
		}
	}

	void FFoliageFacade::SetFoliageNames(const TArray<FString>& InputNames)
	{
		check(!IsConst());
		Collection->EmptyGroup(GroupNames::FoliageNamesGroup);
		Collection->AddElements(InputNames.Num(), GroupNames::FoliageNamesGroup);
		for (int32 Index = 0; Index < InputNames.Num(); ++Index)
		{
			NamesAttribute.Modify()[Index] = InputNames[Index];
		}
	}

	void FFoliageFacade::SetFoliagePath(const FString InPath)
	{
		check(!IsConst());

		int32 NumElements = Collection->NumElements(GroupNames::DetailsGroup);

		if (NumElements == 0)
		{
			Collection->AddElements(1, GroupNames::DetailsGroup);
		}

		FoliagePathAttribute.Modify()[0] = InPath;
	}

	FString FFoliageFacade::GetFoliagePath() const
	{
		if (FoliagePathAttribute.IsValid() && FoliagePathAttribute.IsValidIndex(0))
		{
			return FoliagePathAttribute[0];
		}

		return FString();
	}

	int32 FFoliageFacade::GetElementCount() const
	{
		return NameIdsAttribute.Num();
	}

	void FFoliageFacade::CopyEntry(const int32 FromIndex, const int32 ToIndex)
	{
		if (IsValid() && NameIdsAttribute.IsValidIndex(FromIndex) && NameIdsAttribute.IsValidIndex(ToIndex))
		{
			NameIdsAttribute.ModifyAt(ToIndex, NameIdsAttribute[FromIndex]);
			BranchIdsAttribute.ModifyAt(ToIndex, BranchIdsAttribute[FromIndex]);
			PivotPointsAttribute.ModifyAt(ToIndex, PivotPointsAttribute[FromIndex]);
			UpVectorsAttribute.ModifyAt(ToIndex, UpVectorsAttribute[FromIndex]);
			NormalVectorsAttribute.ModifyAt(ToIndex, NormalVectorsAttribute[FromIndex]);
			ScalesAttribute.ModifyAt(ToIndex, ScalesAttribute[FromIndex]);
			LengthFromRootAttribute.ModifyAt(ToIndex, LengthFromRootAttribute[FromIndex]);
			ParentBoneIdsAttribute.ModifyAt(ToIndex, ParentBoneIdsAttribute[FromIndex]);
		}
	}

	void FFoliageFacade::RemoveEntries(const int32 NumEntries, const int32 StartIndex)
	{
		if (IsValid() && NameIdsAttribute.IsValidIndex(StartIndex) && StartIndex + NumEntries <= NameIdsAttribute.Num())
		{
			NameIdsAttribute.RemoveElements(NumEntries, StartIndex);
		}
	}

	FTransform FFoliageFacade::GetFoliageTransform(int32 Id) const
	{
		const FFoliageEntryData Data = GetFoliageEntry(Id);

		const FQuat UpVectorRotationQuat = FQuat::FindBetweenNormals(FVector::UpVector, FVector(Data.UpVector).GetSafeNormal());
		const FVector RotatedVectorFace = UpVectorRotationQuat.RotateVector(FVector::RightVector);
		float Dot = FVector::DotProduct(RotatedVectorFace, FVector(Data.NormalVector));
		float Angle = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
		const FQuat FaceRotationQuat = FQuat(FVector(Data.UpVector).GetSafeNormal(), Angle);

		const FTransform ScaleTransform(FQuat::Identity, FVector::ZeroVector, FVector(Data.Scale));
		const FTransform FaceRotationTransform(FaceRotationQuat);
		const FTransform UpVectorRotationTransform(UpVectorRotationQuat);
		const FTransform TranslationTransform(FRotator::ZeroRotator, FVector(Data.PivotPoint));
		const FTransform FinalTransform = ScaleTransform * UpVectorRotationTransform * FaceRotationTransform * TranslationTransform;

		return FinalTransform;
	}

	const TManagedArray<FVector3f>& FFoliageFacade::GetPivotPositions() const
	{
		return PivotPointsAttribute.Get();
	}
}
