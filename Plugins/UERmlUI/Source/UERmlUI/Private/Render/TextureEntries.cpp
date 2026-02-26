#include "Render/TextureEntries.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "RHICommandList.h"

FRmlTextureEntry::FRmlTextureEntry(UTexture* InTexture, FString InTexturePath)
	: BoundTexture(InTexture)
	, TexturePath(InTexturePath)
{
	if (UTexture2D* Tex2D = Cast<UTexture2D>(InTexture))
		bBoundTextureIsSRGB = Tex2D->SRGB;
}

FRmlTextureEntry::~FRmlTextureEntry()
{
}

FRHITexture* FRmlTextureEntry::GetTextureRHI()
{
	if (OverrideRHI.IsValid())
		return OverrideRHI.GetReference();
	if (!BoundTexture) return nullptr;
	FTextureResource* Res = BoundTexture->GetResource();
	if (!Res) return nullptr;
	return Res->TextureRHI.GetReference();
}

FRHIShaderResourceView* FRmlTextureEntry::GetNonSRGBSRV(FRHICommandListImmediate& RHICmdList)
{
	if (!bBoundTextureIsSRGB)
		return nullptr;

	if (CachedNonSRGBSRV.IsValid())
		return CachedNonSRGBSRV.GetReference();

	FRHITexture* TexRHI = GetTextureRHI();
	if (!TexRHI)
		return nullptr;

	FRHITextureSRVCreateInfo CreateInfo;
	CreateInfo.SRGBOverride = SRGBO_ForceDisable;
	CachedNonSRGBSRV = RHICmdList.CreateShaderResourceView(TexRHI, CreateInfo);
	return CachedNonSRGBSRV.GetReference();
}

void FRmlTextureEntry::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(BoundTexture);
}
