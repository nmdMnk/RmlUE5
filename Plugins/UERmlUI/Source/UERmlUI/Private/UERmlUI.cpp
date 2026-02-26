#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/Engine.h"
#include "RmlInterface/UERmlFileInterface.h"
#include "RmlInterface/UERmlSystemInterface.h"
#include "RmlInterface/UERmlRenderInterface.h"
#include "Logging.h"
#include "RmlDocument.h"
#include "RmlUi/Core.h"

#define LOCTEXT_NAMESPACE "FUERmlUI"

static FUERmlFileInterface GFileInterface;
static FUERmlSystemInterface GSystemInterface;
static FUERmlRenderInterface GRenderInterface;
static bool GRmlInitialized = false;

class FUERmlUI : public IModuleInterface
{
public:
	void StartupModule() override
	{
		// register shader dictionary 
		FString ShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("UERmlUI"))->GetBaseDir(),TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/UERmlUI"), ShaderDir);

		// Set interfaces (must be called before Rml::Initialise)
		Rml::SetFileInterface(&GFileInterface);
		Rml::SetSystemInterface(&GSystemInterface);
		Rml::SetRenderInterface(&GRenderInterface);

		// Initialize RmlUi core
		if (!Rml::Initialise())
		{
			UE_LOG(LogUERmlUI, Error, TEXT("Rml::Initialise() failed"));
			return;
		}
		GRmlInitialized = true;

		// Load a default font — try UE Engine's bundled Roboto first, fall back to Windows Arial
		FString FontPath = FPaths::ConvertRelativePathToFull(
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"));
		if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FontPath))
		{
			FontPath = TEXT("C:/Windows/Fonts/arial.ttf");
		}
		if (!Rml::LoadFontFace(TCHAR_TO_UTF8(*FontPath)))
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("Failed to load font '%s' — text may not render"), *FontPath);
		}

#if STATS
		// Register "stat RmlUI" after engine is ready
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FUERmlUI::RegisterStatCommands);
#endif
	}

	void ShutdownModule() override
	{
#if STATS
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);
		if (GEngine)
		{
			GEngine->RemoveEngineStat(TEXT("STAT_RmlUI"));
		}
#endif
		GRmlInitialized = false;
	}

#if STATS
private:
	void RegisterStatCommands()
	{
		if (!GEngine) return;

		GEngine->AddEngineStat(
			TEXT("STAT_RmlUI"),
			TEXT("STATCAT_Advanced"),
			LOCTEXT("RmlUI_Desc", "Toggle all RmlUI stat groups (RT, Interface, Game)"),
			UEngine::FEngineStatRender(),
			UEngine::FEngineStatToggle::CreateRaw(this, &FUERmlUI::ToggleStatRmlUI));
	}

	bool ToggleStatRmlUI(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		if (!GEngine) return false;

		GEngine->Exec(World, TEXT("stat RmlUI_RT"));
		GEngine->Exec(World, TEXT("stat RmlUI_Interface"));
		GEngine->Exec(World, TEXT("stat RmlUI_Game"));

		return true;
	}
#endif
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUERmlUI, UERmlUI);

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand GRmlReloadStyleSheets(
	TEXT("rmlui.ReloadStyleSheets"),
	TEXT("Reload all RmlUI RCSS stylesheets from disk across all active contexts.\n")
	TEXT("Use during PIE to hot-reload CSS/RCSS changes without restarting."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GRmlInitialized)
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("rmlui.ReloadStyleSheets: RmlUI not initialized"));
			return;
		}
		Rml::Factory::ClearStyleSheetCache();
		Rml::Factory::ClearTemplateCache();
		int32 NumContexts = Rml::GetNumContexts();
		int32 TotalDocs = 0;
		for (int32 i = 0; i < NumContexts; ++i)
		{
			Rml::Context* Ctx = Rml::GetContext(i);
			if (!Ctx) continue;
			int32 NumDocs = Ctx->GetNumDocuments();
			for (int32 j = 0; j < NumDocs; ++j)
			{
				Rml::ElementDocument* Doc = Ctx->GetDocument(j);
				if (Doc)
				{
					Doc->ReloadStyleSheet();
					++TotalDocs;
				}
			}
		}
		UE_LOG(LogUERmlUI, Log, TEXT("rmlui.ReloadStyleSheets: reloaded %d doc(s) in %d context(s)"),
			TotalDocs, NumContexts);
	}));

static FAutoConsoleCommand GRmlReloadDocuments(
	TEXT("rmlui.ReloadDocuments"),
	TEXT("Fully reload all RmlUI documents from disk (hot-reload RML structure + RCSS).\n")
	TEXT("Closes and reopens every document, calling OnInit() again to re-cache element pointers."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GRmlInitialized)
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("rmlui.ReloadDocuments: RmlUI not initialized"));
			return;
		}
		Rml::Factory::ClearStyleSheetCache();
		Rml::Factory::ClearTemplateCache();
		int32 Count = 0;
		for (TObjectIterator<URmlDocument> It; It; ++It)
		{
			if (It->Reload()) ++Count;
		}
		UE_LOG(LogUERmlUI, Log, TEXT("rmlui.ReloadDocuments: reloaded %d document(s)"), Count);
	}));
#endif