// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class RmlUI : ModuleRules
{
	public RmlUI(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.CPlusPlus;

		// Dependencies
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core" ,
			"UElibPNG" ,	// depended by freetype
			"zlib" ,		// depended by freetype
			"FreeType2" ,	// use free type lib
		});

		// RML ui need RTTI
		bUseRTTI = true;

		// Always optimize RmlUI library — even in DebugGame.
		// InNonDebugBuilds does NOT cover DebugGame for project plugins,
		// so we use Always to force /Ox in all configurations.
		// Without this, DebugGame compiles with /Od, making DOM operations 2-3× slower.
		OptimizeCode = CodeOptimization.Always;

		// Disable pch to fix header error
		PCHUsage = PCHUsageMode.NoPCHs;

		// Disable unity build
		bUseUnity = false;

		// Add include path
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

		// Add definition
		PrivateDefinitions.AddRange(new string[]
		{
			"RMLUI_CORE_EXPORTS" ,          // 6.2 export macro: RMLUICORE_API = __declspec(dllexport)
			"RMLUI_FONT_ENGINE_FREETYPE" ,  // 6.2: opt-in to compile the default FreeType font engine
			"_CRT_SECURE_NO_WARNINGS" ,     // Disable scanf warning
		});

		// Disable warning
		UnsafeTypeCastWarningLevel = WarningLevel.Off;
		ShadowVariableWarningLevel = WarningLevel.Off;
		
		// Enable lua 
		// PrivateDefinitions.Add("RMLUI_BUILD_LUA");
	}
}
