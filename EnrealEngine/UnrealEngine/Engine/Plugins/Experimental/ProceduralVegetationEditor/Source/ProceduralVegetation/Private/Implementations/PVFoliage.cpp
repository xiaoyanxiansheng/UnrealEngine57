// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVFoliage.h"

#include "ProceduralVegetationModule.h"
#include "PVFloatRamp.h"

#include "Facades/PVAttributesNames.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVFoliageFacade.h"
#include "Facades/PVPointFacade.h"

void FPVFoliage::DistributeFoliage(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
                                   const FDistributionSettings& DistributionSettings, const FScaleSettings& ScaleSettings,
                                   const FVectorSettings& VectorSettings, const FPhyllotaxySettings& PhyllotaxySettings,
                                   const FMiscSettings& MiscSettings)
{
	PV::Facades::FFoliageFacade FoliageFacadeOutput(OutCollection);

	if (const int32 NumFoliageMeshes = FoliageFacadeOutput.NumFoliageNames(); NumFoliageMeshes == 0)
	{
		UE_LOG(LogProceduralVegetation, Warning, TEXT("There are no foliage meshes available in input for distribution"));
		return;
	}

	FRandomStream RandomStream;
	RandomStream.Initialize(MiscSettings.RandomSeed);

	PV::Facades::FBranchFacade BranchFacadeSource(SourceCollection);
	PV::Facades::FPointFacade PointFacadeSource(SourceCollection);

	TManagedArrayAccessor<TArray<FVector3f>> BudDirectionAttribute(OutCollection, PV::AttributeNames::BudDirection,
		PV::GroupNames::PointGroup);
	TManagedArrayAccessor<float> BranchGradientsAttribute(OutCollection, PV::AttributeNames::BranchGradient,
		PV::GroupNames::PointGroup);
	TManagedArrayAccessor<TArray<float>> LeafPhyllotaxyAttribute(OutCollection, PV::AttributeNames::LeafPhyllotaxy,
		PV::GroupNames::DetailsGroup);
	TManagedArrayAccessor<TArray<float>> BudHormoneLevels(OutCollection, PV::AttributeNames::BudHormoneLevels,
		PV::GroupNames::PointGroup);

	float PhyllotaxyFormationLeaf = LeafPhyllotaxyAttribute[0][1];
	float MinBudsLeaf = LeafPhyllotaxyAttribute[0][3];
	float MaxBudsLeaf = LeafPhyllotaxyAttribute[0][4];
	int32 ResetPhyllotaxyLeaf = static_cast<int>(LeafPhyllotaxyAttribute[0][6]);
	float PhyllotaxyOffsetLeaf = LeafPhyllotaxyAttribute[0][7];

	if (PhyllotaxySettings.OverridePhyllotaxy)
	{
		switch (PhyllotaxySettings.PhyllotaxyType)
		{
		case EPhyllotaxyType::Alternate: PhyllotaxyFormationLeaf = 180 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = MaxBudsLeaf = 1;
			break;
		case EPhyllotaxyType::Opposite: PhyllotaxyFormationLeaf = PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = MaxBudsLeaf = 2;
			break;
		case EPhyllotaxyType::Decussate: PhyllotaxyFormationLeaf = 90 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = MaxBudsLeaf = 2;
			break;
		case EPhyllotaxyType::Whorled: PhyllotaxyFormationLeaf = 90 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = PhyllotaxySettings.MinimumNodeBuds;
			MaxBudsLeaf = PhyllotaxySettings.MaximumNodeBuds;
			break;
		case EPhyllotaxyType::Spiral:
			{
				// These leaf arrangement configurations where Distichous is two leaves, Tristichous is three etc.
				switch (PhyllotaxySettings.PhyllotaxyFormation)
				{
				case EPhyllotaxyFormation::Distichous: PhyllotaxyFormationLeaf = 180 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Tristichous: PhyllotaxyFormationLeaf = 120 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Pentastichous: PhyllotaxyFormationLeaf = 144 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Octastichous: PhyllotaxyFormationLeaf = 135 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Parastichous: PhyllotaxyFormationLeaf = PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				}

				MinBudsLeaf = MaxBudsLeaf = 1;
				break;
			}
		}
	}

	const int32 NumBranches = BranchFacadeSource.GetElementCount();

	if (DistributionSettings.OverrideDistribution)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PVFoliageDistributorNode::OverrideDistribution);

		// Compute attachment points in terms of normalized distances
		OutCollection.EmptyGroup(PV::GroupNames::FoliageGroup);
		for (int32 BranchIndex = 0; BranchIndex < NumBranches; ++BranchIndex)
		{
			TArray<float> NormalizedAttachmentPoints;
			TArray<float> NormalizedAttachmentPointsFinal;
			TArray<FVector3f> AttachmentPointsPositions;
			TArray<float> AttachmentPointsLengthsFromRoot;
			TArray<FVector3f> AttachmentPointsUpVectors;
			TArray<FVector3f> AttachmentPointsNormalVectors;
			TArray<float> AttachmentPointsScales;
			TArray<int32> BranchPoints = BranchFacadeSource.GetPoints(BranchIndex);

			if (BranchPoints.Num() < 2)
			{
				continue;
			}
			
			const float FirstPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints[0]);
			const float LastPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints.Last());
			const float BranchLength = LastPointLengthFromRoot - FirstPointLengthFromRoot;
			float Increment = DistributionSettings.InstanceSpacing / BranchLength;
			const float AdjustedMaxPerBranch = DistributionSettings.MaxPerBranch == -1
				? BranchPoints.Num()
				: DistributionSettings.MaxPerBranch;
			float LookUp = 0.0f;

			for (int32 i = 0; i < AdjustedMaxPerBranch; ++i)
			{
				if (LookUp > 1.0f)
				{
					break;
				}

				float InstanceSpacingRampValue = DistributionSettings.InstanceSpacingRamp
					? DistributionSettings.InstanceSpacingRamp->GetRichCurveConst()->Eval(1.0f - LookUp)
					: Increment;
				// The increment is nudged slightly before blending with the weighted ramp value, the number is chosen based on observation
				float AdjustedIncrement = Increment + (InstanceSpacingRampValue * (Increment * 10.0f));
				AdjustedIncrement = FMath::Lerp(Increment, AdjustedIncrement, DistributionSettings.InstanceSpacingRampEffect);

				NormalizedAttachmentPoints.Add(1.0f - LookUp);
				LookUp += AdjustedIncrement;
			}

			// Compute Position, Angles, Scale and LengthFromRoot for attachment points

			// Compute based on first point attributes
			const FVector3f InitialApicalDirection(BudDirectionAttribute[BranchPoints[0]][0]);
			FVector3f InitialAxillaryDirection(BudDirectionAttribute[BranchPoints[0]][1]);
			const FVector3f InitialUpVector(BudDirectionAttribute[BranchPoints[0]][5]);

			if (ResetPhyllotaxyLeaf == 1)
			{
				InitialAxillaryDirection = FVector3f::CrossProduct(InitialApicalDirection, InitialUpVector);
			}

			if (PhyllotaxyOffsetLeaf > 0.01f)
			{
				const FQuat4f RotationQuat(InitialApicalDirection.GetSafeNormal(), FMath::DegreesToRadians(PhyllotaxyOffsetLeaf));
				InitialAxillaryDirection = RotationQuat.RotateVector(InitialAxillaryDirection);
			}

			int32 AxillaryRotationIteration = 0;
			for (int32 Idx = 0; Idx < NormalizedAttachmentPoints.Num(); ++Idx)
			{
				for (int32 BranchPointIndex = 0; BranchPointIndex < BranchPoints.Num() - 1; ++BranchPointIndex)
				{
					float AxillaryRotationDegree = FMath::DegreesToRadians(PhyllotaxyFormationLeaf * AxillaryRotationIteration);

					const int32 CurrentPointIndex = BranchPoints[BranchPointIndex];
					const int32 NextPointIndex = BranchPoints[BranchPointIndex + 1];
					const FVector3f CurrentPointPosition = PointFacadeSource.GetPosition(CurrentPointIndex);
					const FVector3f NextPointPosition = PointFacadeSource.GetPosition(NextPointIndex);
					float EthyleneLevelAtCurrentPoint = BudHormoneLevels[CurrentPointIndex][4];
					float EthyleneLevelAtNextPoint = BudHormoneLevels[NextPointIndex][4];
					const float CurrentPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(CurrentPointIndex);
					const float NextPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(NextPointIndex);
					const float CurrentPointLfrNormalized = FMath::GetMappedRangeValueClamped(
						FVector2f(FirstPointLengthFromRoot, LastPointLengthFromRoot),
						FVector2f(0.0f, 1.0f), CurrentPointLengthFromRoot);
					const float NextPointLfrNormalized = FMath::GetMappedRangeValueClamped(
						FVector2f(FirstPointLengthFromRoot, LastPointLengthFromRoot),
						FVector2f(0.0f, 1.0f),
						NextPointLengthFromRoot);

					if (NormalizedAttachmentPoints[Idx] >= CurrentPointLfrNormalized && NormalizedAttachmentPoints[Idx] <=
						NextPointLfrNormalized)
					{
						const float DistanceAlpha = (NormalizedAttachmentPoints[Idx] -
							CurrentPointLfrNormalized) / (NextPointLfrNormalized - CurrentPointLfrNormalized);

						const float EthyleneLevel = FMath::Lerp(EthyleneLevelAtCurrentPoint, EthyleneLevelAtNextPoint, DistanceAlpha);

						if ((EthyleneLevel - 0.001f) >= DistributionSettings.EthyleneThreshold)
						{
							continue;
						}

						const FVector3f Position = FMath::Lerp(CurrentPointPosition, NextPointPosition, DistanceAlpha);
						AttachmentPointsPositions.Add(Position);

						const float AttachmentPointLfr = FMath::GetMappedRangeValueClamped(
							FVector2f(0.0f, 1.0f),
							FVector2f(FirstPointLengthFromRoot, LastPointLengthFromRoot),
							NormalizedAttachmentPoints[Idx]);
						AttachmentPointsLengthsFromRoot.Add(AttachmentPointLfr);

						// Compute Scale
						const float CurrentPointBranchGradient = BranchGradientsAttribute[CurrentPointIndex];
						const float NextPointBranchGradient = BranchGradientsAttribute[NextPointIndex];

						const float BranchGradient = FMath::Clamp(
							FMath::Lerp(CurrentPointBranchGradient, NextPointBranchGradient, DistanceAlpha), 0.0f, 1.0f);
						float BranchGradientRampValue = ScaleSettings.ScaleRamp
							? ScaleSettings.ScaleRamp->GetRichCurveConst()->Eval(1.0f - BranchGradient)
							: 1.0f;

						const float CurrentPointScale = PointFacadeSource.GetPointScale(CurrentPointIndex);
						const float NextPointScale = PointFacadeSource.GetPointScale(NextPointIndex);
						const float BranchScale = FMath::Lerp(CurrentPointScale, NextPointScale, DistanceAlpha);

						const float BranchScaledValue = BranchGradientRampValue * (BranchScale + 0.1f);
						BranchGradientRampValue = FMath::Lerp(BranchGradientRampValue, BranchScaledValue, ScaleSettings.BranchScaleImpact);
						BranchGradientRampValue = FMath::Clamp(BranchGradientRampValue, 0.0f, 1.0f);

						float Scale = FMath::GetMappedRangeValueClamped(
							FVector2f(0.0f, 1.0f),
							FVector2f(ScaleSettings.MinScale, ScaleSettings.MaxScale),
							BranchGradientRampValue
						) * ScaleSettings.BaseScale;

						const float RandomValue = RandomStream.FRandRange(ScaleSettings.RandomScaleMin, ScaleSettings.RandomScaleMax);
						Scale = Scale * RandomValue;

						AttachmentPointsScales.Add(Scale);

						const FVector3f CurrentPointApicalDirectionVector(BudDirectionAttribute[CurrentPointIndex][0]);
						const FVector3f NextPointApicalDirectionVector(BudDirectionAttribute[NextPointIndex][0]);
						const FVector3f BudApicalDirectionVector = FMath::Lerp(CurrentPointApicalDirectionVector,
							NextPointApicalDirectionVector, DistanceAlpha);

						int32 BudsToAdd = 1;

						// Compute Up Vector
						// Last point (order of the array is last to first)
						if (Idx == 0)
						{
							AttachmentPointsUpVectors.Add(BudApicalDirectionVector);
						}
						else // Points other than last
						[[likely]]
						{
							const FQuat4f RotationQuat(InitialApicalDirection.GetSafeNormal(), AxillaryRotationDegree);
							FVector3f NewAxillaryDirection = RotationQuat.RotateVector(InitialAxillaryDirection.GetSafeNormal());
							FQuat4f ApicalCorrection = FQuat4f::FindBetweenNormals(
								InitialApicalDirection.GetSafeNormal(), BudApicalDirectionVector.GetSafeNormal());
							NewAxillaryDirection = ApicalCorrection.RotateVector(NewAxillaryDirection).GetSafeNormal();

							int32 NumBuds = RandomStream.RandRange(MinBudsLeaf, MaxBudsLeaf);
							float RotationPerBud = 360.0f / static_cast<float>(NumBuds);

							for (int32 i = 0; i < NumBuds; ++i)
							{
								float RotationAngle = FMath::DegreesToRadians(RotationPerBud * i);
								const FQuat4f AxillaryRotation(InitialApicalDirection.GetSafeNormal(), RotationAngle);
								FVector3f AxillaryDirectionModified = AxillaryRotation.RotateVector(NewAxillaryDirection.GetSafeNormal());

								if (VectorSettings.OverrideAxilAngle)
								{
									float AxilAngleRampValue = VectorSettings.AxilAngleRamp
										? FMath::Clamp(
											VectorSettings.AxilAngleRamp->GetRichCurveConst()->Eval(1.0f - BranchGradient),
											0.0f,
											1.0f)
										: 1.0f;

									AxilAngleRampValue = AxilAngleRampValue * VectorSettings.AxilAngleRampUpperValue;
									const float AxilAngleBlended = FMath::Lerp(VectorSettings.AxilAngle, AxilAngleRampValue,
										VectorSettings.AxilAngleRampEffect);

									const FVector3f RotationAxis = FVector3f::CrossProduct(
										BudApicalDirectionVector.GetSafeNormal(), AxillaryDirectionModified.GetSafeNormal());

									const FQuat4f Rotation(RotationAxis.GetSafeNormal(), FMath::DegreesToRadians(AxilAngleBlended));
									const FVector3f AxilVector = Rotation.RotateVector(BudApicalDirectionVector.GetSafeNormal());

									AttachmentPointsUpVectors.Add(AxilVector);
								}
								else
								{
									AttachmentPointsUpVectors.Add(AxillaryDirectionModified);
								}

								if (i > 0)
								{
									FVector3f LastPositionAdded = AttachmentPointsPositions.Last();
									float LastScaleAdded = AttachmentPointsScales.Last();
									float LastLengthFromRootAdded = AttachmentPointsLengthsFromRoot.Last();
									AttachmentPointsPositions.Add(LastPositionAdded);
									AttachmentPointsScales.Add(LastScaleAdded);
									AttachmentPointsLengthsFromRoot.Add(LastLengthFromRootAdded);
								}
							}

							BudsToAdd = NumBuds;
							AxillaryRotationIteration++;
						}

						// Compute Normal vectors based on light optimal direction
						const FVector3f CurrentPointLightOptimalVector(BudDirectionAttribute[CurrentPointIndex][2]);
						const FVector3f NextPointLightOptimalVector(BudDirectionAttribute[NextPointIndex][2]);
						const FVector3f BudLightOptimalVector = FMath::Lerp(CurrentPointLightOptimalVector, NextPointLightOptimalVector,
							DistanceAlpha);

						FQuat4f QuatRotation = FQuat4f::FindBetweenNormals(BudApicalDirectionVector.GetSafeNormal(),
							BudLightOptimalVector.GetSafeNormal());
						const FVector3f ApicalDirectionRotated = QuatRotation.RotateVector(BudApicalDirectionVector);

						for (int32 i = 0; i < BudsToAdd; ++i)
						{
							AttachmentPointsNormalVectors.Add(ApicalDirectionRotated);
						}
					}
				}
			}

			// Set foliage entries
			TManagedArrayAccessor<FVector3f> PivotPointsAttribute(OutCollection, PV::AttributeNames::FoliagePivotPoint,
				PV::GroupNames::FoliageGroup);
			const int32 NumFoliageMeshes = FoliageFacadeOutput.NumFoliageNames();
			int32 CurrentFoliageMeshIndex = BranchIndex % NumFoliageMeshes;
			for (int32 Idx = 0; Idx < AttachmentPointsLengthsFromRoot.Num(); ++Idx)
			{
				const int32 NewIndex = PivotPointsAttribute.AddElements(1);
				FoliageFacadeOutput.SetFoliageEntry(NewIndex, {
					.NameId = CurrentFoliageMeshIndex,
					.BranchId = BranchIndex,
					.PivotPoint = AttachmentPointsPositions[Idx],
					.UpVector = AttachmentPointsUpVectors[Idx],
					.NormalVector = AttachmentPointsNormalVectors[Idx],
					.Scale = AttachmentPointsScales[Idx],
					.LengthFromRoot = AttachmentPointsLengthsFromRoot[Idx]
				});
				CurrentFoliageMeshIndex = (CurrentFoliageMeshIndex + 1) % NumFoliageMeshes;
			}
		}

		// Re-calculate Foliage IDs for each branch
		TMap<int32, TArray<int32>> BranchIndexToFoliageIDs;
		for (int32 i = 0; i < FoliageFacadeOutput.NumFoliageEntries(); ++i)
		{
			const int32 BranchIndex = FoliageFacadeOutput.GetFoliageEntry(i).BranchId;
			TArray<int32>& IDs = BranchIndexToFoliageIDs.FindOrAdd(BranchIndex);
			IDs.Add(i);
		}
		for (const TPair<int32, TArray<int32>>& Pair : BranchIndexToFoliageIDs)
		{
			int32 BranchId = Pair.Key;
			const TArray<int32>& FoliageIDs = Pair.Value;
			FoliageFacadeOutput.SetFoliageIdsArray(BranchId, FoliageIDs);
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PVFoliageDistributorNode::EthyleneRemove);

		// No need to apply Ethylene threshold if the distribution was overriden since that's already been accounted for
		if (DistributionSettings.OverrideDistribution)
		{
			return;
		}

		// Compute Foliage instances to remove based on Ethylene threshold
		TManagedArrayAccessor<FVector3f> PivotPointsAttribute(
			OutCollection,
			PV::AttributeNames::FoliagePivotPoint,
			PV::GroupNames::FoliageGroup);
		TSet<int32> FoliageEntriesToRemove;

		for (int32 BranchIndex = 0; BranchIndex < NumBranches; ++BranchIndex)
		{
			TArray<int32> BranchPoints = BranchFacadeSource.GetPoints(BranchIndex);
			TArray<int32> FoliageIDs = FoliageFacadeOutput.GetFoliageEntryIdsForBranch(BranchIndex);
			TSet<int32> FoliageIDsToRemove;

			for (int32 BranchPointIndex = 0; BranchPointIndex < BranchPoints.Num() - 1; ++BranchPointIndex)
			{
				for (int32 FoliageIDsIndex = 0; FoliageIDsIndex < FoliageIDs.Num(); ++FoliageIDsIndex)
				{
					const float CurrentPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints[BranchPointIndex]);
					const float NextPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints[BranchPointIndex + 1]);
					const float FoliageLengthFromRoot = FoliageFacadeOutput.GetFoliageEntry(FoliageIDs[FoliageIDsIndex]).LengthFromRoot;

					if (FoliageLengthFromRoot >= CurrentPointLengthFromRoot && FoliageLengthFromRoot <= NextPointLengthFromRoot)
					{
						float EthyleneLevelAtCurrentPoint = BudHormoneLevels[BranchPoints[BranchPointIndex]][4];
						float EthyleneLevelAtNextPoint = BudHormoneLevels[BranchPoints[BranchPointIndex + 1]][4];

						float EthyleneLevel = FMath::GetMappedRangeValueClamped(
							FVector2f(CurrentPointLengthFromRoot, NextPointLengthFromRoot),
							FVector2f(EthyleneLevelAtNextPoint, EthyleneLevelAtCurrentPoint),
							FoliageLengthFromRoot);

						if ((EthyleneLevel - 0.001f) >= DistributionSettings.EthyleneThreshold)
						{
							FoliageIDsToRemove.Add(FoliageIDs[FoliageIDsIndex]);
						}
					}
				}
			}

			for (int32 Fid : FoliageIDsToRemove)
			{
				FoliageEntriesToRemove.Add(Fid);
			}
		}

		// Remove foliage instances
		for (int32 i = PivotPointsAttribute.Num() - 1; i >= 0; --i)
		{
			if (FoliageEntriesToRemove.Contains(i))
			{
				PivotPointsAttribute.RemoveElements(1, i);
			}
		}

		// Re-calculate Foliage IDs for each branch
		TMap<int32, TArray<int32>> BranchIndexToFoliageIDs;
		for (int32 i = 0; i < FoliageFacadeOutput.NumFoliageEntries(); ++i)
		{
			const int32 BranchIndex = FoliageFacadeOutput.GetFoliageEntry(i).BranchId;
			TArray<int32>& IDs = BranchIndexToFoliageIDs.FindOrAdd(BranchIndex);
			IDs.Add(i);
		}
		for (const TPair<int32, TArray<int32>>& Pair : BranchIndexToFoliageIDs)
		{
			int32 BranchId = Pair.Key;
			const TArray<int32>& FoliageIDs = Pair.Value;
			FoliageFacadeOutput.SetFoliageIdsArray(BranchId, FoliageIDs);
		}
	}
}
