// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class MetaHumanCalibrationCore : ModuleRules
{
	protected bool BuildForDevelopment
	{
		get
		{
			// Check if source is available
			string SourceFilesPath = Path.Combine(ModuleDirectory, "Private");
			return Directory.Exists(SourceFilesPath) &&
				   Directory.GetFiles(SourceFilesPath).Length > 0;
		}
	}

	public MetaHumanCalibrationCore(ReadOnlyTargetRules Target) : base(Target)
	{
		bUsePrecompiled = !BuildForDevelopment;
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		ShortName = "MHCalibCore";

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"CaptureDataCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"MetaHumanCalibrationLib",
			"MetaHumanCaptureData",
			"CaptureDataEditor",
			"CaptureDataUtils",
			"SlateCore",
			"Slate",
			"ImageWrapper",
			"ImgMedia",
			"ImageCore",
			"CaptureUtils",
			"MetaHumanImageViewer",
			"SequencerWidgets",
			"Projects",
			"InputCore",
			"OutputLog",
			"Json",
			"JsonUtilities",
			"WorkspaceMenuStructure",
			"SettingsEditor",
			"OpenCVHelper",
			"OpenCV"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ToolMenus",
				"ToolWidgets",
				"ContentBrowser",
				"UnrealEd"
			});
		}
	}
}