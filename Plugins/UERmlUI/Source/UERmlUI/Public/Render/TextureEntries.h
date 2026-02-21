#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class FRHITexture;
class UTexture;

class FRmlTextureEntry : public FGCObject, public TSharedFromThis<FRmlTextureEntry, ESPMode::ThreadSafe>
{
public:
	FRmlTextureEntry(UTexture* InTexture = nullptr, FString InTexturePath = FString());
	virtual ~FRmlTextureEntry();
	virtual FRHITexture* GetTextureRHI();
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FRmlTextureEntry"); }
public:
	TObjectPtr<UTexture>	BoundTexture;
	FString					TexturePath;
	bool					bPremultiplied = false;
};
