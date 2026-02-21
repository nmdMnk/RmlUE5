#include "CoreMinimal.h"
#include "Logging.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "RmlInterface/UERmlFileInterface.h"
#include "RmlInterface/UERmlSystemInterface.h"
#include "RmlInterface/UERmlRenderInterface.h"
#include "RmlUi/Core.h"

#define LOCTEXT_NAMESPACE "FUERmlUI"

static FUERmlFileInterface GFileInterface;
static FUERmlSystemInterface GSystemInterface;
static FUERmlRenderInterface GRenderInterface;

class UERMLUI_API FUERmlUI : public IModuleInterface
{
	bool bRmlInitialized = false;
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
		bRmlInitialized = true;

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
	}

	void ShutdownModule() override
	{
		bRmlInitialized = false;
	}
};

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUERmlUI, UERmlUI);