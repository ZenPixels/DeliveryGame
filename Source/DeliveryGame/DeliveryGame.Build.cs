// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DeliveryGame : ModuleRules
{
	public DeliveryGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ChaosVehicles",
			"PhysicsCore",
			"NavigationSystem",
			"AIModule",
			"GameplayTasks",
			"UMG",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"AudioExtensions",
		});
	}
}
