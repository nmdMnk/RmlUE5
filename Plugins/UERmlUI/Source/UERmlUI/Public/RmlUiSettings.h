#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RmlUiSettings.generated.h"

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

	// Enable 4x MSAA for smooth rounded borders and curved edges.
	// Disable if you experience rendering artifacts or want lower GPU overhead.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering")
	bool bEnableMSAA = true;

	virtual FName GetCategoryName() const override
	{
		return FName(TEXT("Plugins"));
	}
};
