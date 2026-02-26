#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RmlUiSettings.generated.h"

UENUM(BlueprintType)
enum class ERmlMSAASamples : uint8
{
	Disabled  UMETA(DisplayName = "Disabled"),
	x2        UMETA(DisplayName = "2x"),
	x4        UMETA(DisplayName = "4x"),
	x8        UMETA(DisplayName = "8x"),
};

/** One entry in the warmup document list. */
USTRUCT()
struct UERMLUI_API FRmlWarmupDocumentEntry
{
	GENERATED_BODY()

	/** Full path to the RML document to pre-warm (e.g. C:/Project/Content/RmlAssets/demo.rml). */
	UPROPERTY(config, EditAnywhere, Category = "RmlUi")
	FString DocumentPath;
};

/**
 * Settings for the RmlUi rendering plugin.
 * Configure these in Project Settings -> Plugins -> RmlUI.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "RmlUI"))
class UERMLUI_API URmlUiSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "RmlUI", meta = (DisplayName = "Get RmlUI Settings"))
	static const URmlUiSettings* Get()
	{
		return GetDefault<URmlUiSettings>();
	}

	// MSAA sample count for smooth rounded borders and curved edges.
	// Higher values give smoother edges but cost more GPU.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering")
	ERmlMSAASamples MSAASamples = ERmlMSAASamples::x4;

	int32 GetMSAASampleCount() const
	{
		switch (MSAASamples)
		{
			case ERmlMSAASamples::x2: return 2;
			case ERmlMSAASamples::x4: return 4;
			case ERmlMSAASamples::x8: return 8;
			default:                  return 1;
		}
	}

	bool IsMSAAEnabled() const { return MSAASamples != ERmlMSAASamples::Disabled; }

	// Documents to pre-warm before showing the first RmlUi widget.
	// Use rmlui.CaptureWarmup 1 in PIE to auto-populate this list, then stop PIE.
	// Call FRmlWarmer::WarmContext() in BeginPlay to apply.
	UPROPERTY(config, EditAnywhere, Category = "Performance",
		meta = (DisplayName = "Warmup Documents"))
	TArray<FRmlWarmupDocumentEntry> WarmupDocuments;

	virtual FName GetCategoryName() const override
	{
		return FName(TEXT("Plugins"));
	}
};
