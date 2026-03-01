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
#include "RmlUiWidget.h"
#include "RmlUi/Core.h"
#include "FontEngineDefault/FontEngineInterfaceDefault.h"
#include <algorithm>
#include <cctype>
#include <set>

#define LOCTEXT_NAMESPACE "FUERmlUI"

// Family-level font fallback: if the requested font-family is not loaded,
// fall back to FallbackFamily instead of returning null (which renders nothing).
class FUERmlFontEngineInterface : public Rml::FontEngineInterfaceDefault
{
public:
	// Family name MUST be lowercase: FontProvider stores families as lowercase keys.
	void SetFallbackFamily(Rml::String InFamily)
	{
		std::transform(InFamily.begin(), InFamily.end(), InFamily.begin(), [](unsigned char c){ return std::tolower(c); });
		FallbackFamily = std::move(InFamily);
	}

	Rml::FontFaceHandle GetFontFaceHandle(
		const Rml::String& family,
		Rml::Style::FontStyle style,
		Rml::Style::FontWeight weight,
		int size) override
	{
		Rml::FontFaceHandle Handle = FontEngineInterfaceDefault::GetFontFaceHandle(family, style, weight, size);
		if (Handle == 0 && !FallbackFamily.empty() && family != FallbackFamily)
		{
			if (WarnedFamilies.insert(family).second)
			{
				UE_LOG(LogUERmlUI, Warning, TEXT("Font family '%s' not found, falling back to '%s'"),
					UTF8_TO_TCHAR(family.c_str()), UTF8_TO_TCHAR(FallbackFamily.c_str()));
			}
			Handle = FontEngineInterfaceDefault::GetFontFaceHandle(FallbackFamily, style, weight, size);
		}
		return Handle;
	}

private:
	Rml::String FallbackFamily;
	std::set<Rml::String> WarnedFamilies;
};

static FUERmlFontEngineInterface GFontEngineInterface;
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
		Rml::SetFontEngineInterface(&GFontEngineInterface);
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

		// Load a default font — try UE Engine's bundled Roboto first, fall back to Windows Arial.
		// This font also serves as the family-level fallback: if CSS requests a family that has
		// not been loaded (e.g. "LatoLatin" when running in the editor without the game mode),
		// GFontEngineInterface.GetFontFaceHandle() retries with FallbackFamily so text still renders.
		FString FontPath = FPaths::ConvertRelativePathToFull(
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"));
		const bool bUseRoboto = FPlatformFileManager::Get().GetPlatformFile().FileExists(*FontPath);
		if (!bUseRoboto)
		{
			FontPath = TEXT("C:/Windows/Fonts/arial.ttf");
		}
		if (Rml::LoadFontFace(TCHAR_TO_UTF8(*FontPath)))
		{
			// Tell the font engine which family to use as the family-level fallback.
			GFontEngineInterface.SetFallbackFamily(bUseRoboto ? "Roboto" : "Arial");
		}
		else
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
	TEXT("Handles both URmlDocument (game mode path) and URmlUiWidget (UMG widget path)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GRmlInitialized)
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("rmlui.ReloadDocuments: RmlUI not initialized"));
			return;
		}
		Rml::Factory::ClearStyleSheetCache();
		Rml::Factory::ClearTemplateCache();

		// 1) Reload documents managed by URmlDocument subclasses (game mode path).
		int32 Count = 0;
		for (TObjectIterator<URmlDocument> It; It; ++It)
		{
			if (It->Reload()) ++Count;
		}

		// 2) Reload documents inside URmlUiWidget contexts (UMG widget path).
		for (TObjectIterator<URmlUiWidget> It; It; ++It)
		{
			Count += It->ReloadDocuments();
		}

		UE_LOG(LogUERmlUI, Log, TEXT("rmlui.ReloadDocuments: reloaded %d document(s)"), Count);
	}));
#endif