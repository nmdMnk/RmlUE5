// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RmlUE5Target : TargetRules
{
    public RmlUE5Target(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.AddRange(new string[] { "RmlUE5" });
    }
}
