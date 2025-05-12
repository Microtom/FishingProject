// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FishingProject : ModuleRules
{
	public FishingProject(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] { "CableComponent", "EnhancedInput", "MessageLog", "ProceduralMeshComponent" });
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
	}
}
