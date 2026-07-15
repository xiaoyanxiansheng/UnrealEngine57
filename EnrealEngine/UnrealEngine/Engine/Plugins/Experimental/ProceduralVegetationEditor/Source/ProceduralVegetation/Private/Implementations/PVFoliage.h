// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Curves/CurveFloat.h"

#include "GeometryCollection/ManagedArrayCollection.h"

#include "PVFoliage.generated.h"

struct FPVFloatRamp;

UENUM(BlueprintType)
enum class EPhyllotaxyType : uint8
{
	Alternate UMETA(DisplayName = "Alternate"),
	Opposite UMETA(DisplayName = "Opposite"),
	Decussate UMETA(DisplayName = "Decussate"),
	Whorled UMETA(DisplayName = "Whorled"),
	Spiral UMETA(DisplayName = "Spiral")
};

UENUM(BlueprintType)
enum class EPhyllotaxyFormation : uint8
{
	Distichous UMETA(DisplayName = "Distichous"),
	Tristichous UMETA(DisplayName = "Tristichous"),
	Pentastichous UMETA(DisplayName = "Pentastichous"),
	Octastichous UMETA(DisplayName = "Octastichous"),
	Parastichous UMETA(DisplayName = "Parastichous")
};

struct FDistributionSettings
{
	const float EthyleneThreshold;
	const bool OverrideDistribution;
	const float InstanceSpacing;
	const FPVFloatRamp* InstanceSpacingRamp;
	const float InstanceSpacingRampEffect;
	const int32 MaxPerBranch;
};

struct FScaleSettings
{
	const float BaseScale;
	const float BranchScaleImpact;
	const float MinScale;
	const float MaxScale;
	const float RandomScaleMin;
	const float RandomScaleMax;
	const FPVFloatRamp* ScaleRamp;
};

struct FVectorSettings
{
	const bool OverrideAxilAngle;
	const float AxilAngle;
	const FPVFloatRamp* AxilAngleRamp;
	const float AxilAngleRampUpperValue;
	const float AxilAngleRampEffect;
};

struct FPhyllotaxySettings
{
	const bool OverridePhyllotaxy;
	const EPhyllotaxyType PhyllotaxyType;
	const EPhyllotaxyFormation PhyllotaxyFormation;
	const int32 MinimumNodeBuds;
	const int32 MaximumNodeBuds;
	const float PhyllotaxyAdditionalAngle;
};

struct FMiscSettings
{
	const int32 RandomSeed;
};

struct FPVFoliage
{
	static void DistributeFoliage(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
	                              const FDistributionSettings& DistributionSettings, const FScaleSettings& ScaleSettings,
	                              const FVectorSettings& VectorSettings, const FPhyllotaxySettings& PhyllotaxySettings,
	                              const FMiscSettings& MiscSettings);
};
