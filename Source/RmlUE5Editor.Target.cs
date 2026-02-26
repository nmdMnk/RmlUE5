// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RmlUE5EditorTarget : TargetRules
{
    public RmlUE5EditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.AddRange(new string[] { "RmlUE5" });
    }
}
