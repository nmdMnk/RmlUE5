// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RmlUE5 : ModuleRules
{
	public RmlUE5(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UERmlUI", "RmlUI" });
	}
}
