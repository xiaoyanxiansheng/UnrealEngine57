// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrownConradyDULensDistortionModelHandler.h"

#include "CameraCalibrationSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Models/BrownConradyDULensModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BrownConradyDULensDistortionModelHandler)

void UBrownConradyDULensDistortionModelHandler::InitializeHandler()
{
	// Initialize with DU model instead of UD
	LensModelClass = UBrownConradyDULensModel::StaticClass();
}

FVector2D UBrownConradyDULensDistortionModelHandler::ComputeDistortedUV(const FVector2D& InScreenUV) const
{
	// For DU model, ComputeDistortedUV actually performs undistortion
	// So we call the parent's ComputeUndistortedUV
	return Super::ComputeUndistortedUV(InScreenUV);
}

FVector2D UBrownConradyDULensDistortionModelHandler::ComputeUndistortedUV(const FVector2D& InScreenUV) const
{
	// For DU model, ComputeUndistortedUV actually performs distortion
	// So we call the parent's ComputeDistortedUV
	return Super::ComputeDistortedUV(InScreenUV);
}

void UBrownConradyDULensDistortionModelHandler::InitDistortionMaterials()
{
	// Initialize materials using the parent class since DU uses the same materials as UD
	if (DistortionPostProcessMID == nullptr)
	{
		UMaterialInterface* DistortionMaterialParent = GetDefault<UCameraCalibrationSettings>()->GetDefaultDistortionMaterial(UBrownConradyUDLensDistortionModelHandler::StaticClass());
		DistortionPostProcessMID = UMaterialInstanceDynamic::Create(DistortionMaterialParent, this);
	}

	if (UndistortionDisplacementMapMID == nullptr)
	{
		UMaterialInterface* MaterialParent = GetDefault<UCameraCalibrationSettings>()->GetDefaultUndistortionDisplacementMaterial(UBrownConradyUDLensDistortionModelHandler::StaticClass());
		UndistortionDisplacementMapMID = UMaterialInstanceDynamic::Create(MaterialParent, this);
	}

	if (DistortionDisplacementMapMID == nullptr)
	{
		UMaterialInterface* MaterialParent = GetDefault<UCameraCalibrationSettings>()->GetDefaultDistortionDisplacementMaterial(UBrownConradyUDLensDistortionModelHandler::StaticClass());
		DistortionDisplacementMapMID = UMaterialInstanceDynamic::Create(MaterialParent, this);
	}

	// For DU model, swap the texture assignments since our direction is reversed
	DistortionPostProcessMID->SetTextureParameterValue("UndistortionDisplacementMap", DistortionDisplacementMapRT);
	DistortionPostProcessMID->SetTextureParameterValue("DistortionDisplacementMap", UndistortionDisplacementMapRT);

	SetDistortionState(CurrentState);
}