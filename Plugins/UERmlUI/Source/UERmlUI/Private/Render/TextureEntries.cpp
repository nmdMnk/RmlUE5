#include "Render/TextureEntries.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

FRmlTextureEntry::FRmlTextureEntry(UTexture* InTexture, FString InTexturePath)
	: BoundTexture(InTexture)
	, TexturePath(InTexturePath)
{
}

FRmlTextureEntry::~FRmlTextureEntry()
{
}

FRHITexture* FRmlTextureEntry::GetTextureRHI()
{
	if (!BoundTexture) return nullptr;
	FTextureResource* Res = BoundTexture->GetResource();
	if (!Res) return nullptr;
	return Res->TextureRHI.GetReference();
}

void FRmlTextureEntry::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (BoundTexture)
	{
		Collector.AddReferencedObject(BoundTexture);
	}
}
