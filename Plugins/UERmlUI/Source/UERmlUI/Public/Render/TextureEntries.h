#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class FRHITexture;
class FRHICommandListImmediate;
class UTexture;

class FRmlTextureEntry : public FGCObject, public TSharedFromThis<FRmlTextureEntry, ESPMode::ThreadSafe>
{
public:
	FRmlTextureEntry(UTexture* InTexture = nullptr, FString InTexturePath = FString());
	virtual ~FRmlTextureEntry();
	virtual FRHITexture* GetTextureRHI();
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FRmlTextureEntry"); }

	// Returns a non-sRGB SRV for asset textures imported with SRGB=true,
	// so the same UTexture2D works correctly in both Unreal materials (sRGB
	// hardware decode) and RmlUI (raw byte values).  Returns nullptr when
	// the texture is already non-sRGB or has no RHI resource yet.
	FRHIShaderResourceView* GetNonSRGBSRV(FRHICommandListImmediate& RHICmdList);

	TObjectPtr<UTexture>	BoundTexture;
	FString					TexturePath;
	bool					bPremultiplied = false;
	bool					bIsSavedLayer  = false;	// true for callback/cached textures (box-shadow, drop-shadow)
	bool					bWrapSampler   = false;	// true for file-loaded textures (repeat decorators need AM_Wrap)
	bool					bBoundTextureIsSRGB = false;

	// Direct RHI texture — set by render thread for SaveLayerAsTexture.
	// When valid, bypasses the BoundTexture→GetResource() path.
	FTextureRHIRef			OverrideRHI;

	// Cached SRV that bypasses sRGB hardware decode.  Created lazily on the
	// render thread for asset textures with SRGB=true.
	FShaderResourceViewRHIRef CachedNonSRGBSRV;
};
