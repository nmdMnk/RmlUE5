#pragma once
#include "CoreMinimal.h"
#include "RmlUi/Core/Input.h"

class UTexture2D;

class UERMLUI_API FRmlHelper
{
public:
	static Rml::Input::KeyIdentifier ConvertKey(FKey InKey);
	static int GetKeyModifierState(const FModifierKeysState& InState);
	static int GetMouseKey(const FKey& InMouseEvent);
	static UTexture2D* LoadTextureFromRaw(const uint8* InSource, FIntPoint InSize);
	static UTexture2D* LoadTextureFromFile(const FString& InFilePath);
	static UTexture2D* LoadTextureFromAsset(const FString& InAssetPath, UObject* InOuter = GetTransientPackage());

	/**
	 * Pre-warm font effect caches by rendering a hidden RML document once.
	 * Font face layers (glow, shadow, outline) are generated lazily on first use,
	 * causing a CPU spike. Call this during a loading screen with an RML string
	 * that uses your game's font effects to move the cost to load time.
	 *
	 * @param RmlContent  Full <rml> document string with font-effect CSS and sample text.
	 */
	static void PrecacheFontEffects(const FString& RmlContent);
};
