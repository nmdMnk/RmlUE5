#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "RmlUiWidget.generated.h"

class SRmlWidget;
namespace Rml { class Context; class Element; }

/**
 * UMG widget that wraps SRmlWidget for Blueprint use.
 * Drop into a Widget Blueprint to display RmlUi HTML/RCSS content in-game.
 */
UCLASS(meta = (DisplayName = "RmlUI Widget"))
class UERMLUI_API URmlUiWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** RML/HTML document path to load (file path or asset path) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RmlUi")
	FString DefaultDocument;

	/**
	 * Pre-warm font effect textures from URmlUiSettings::WarmupDocuments.
	 * Call BEFORE AddToViewport (e.g. in BeginPlay) to avoid CPU spikes on first render.
	 * No-op if WarmupDocuments is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "RmlUi|Performance")
	void PrewarmFromSettings();

	/** Load an RML document into the RmlUi context */
	UFUNCTION(BlueprintCallable, Category = "RmlUi")
	void LoadDocument(const FString& DocumentPath);

	/** Set the inner RML of an element by ID */
	UFUNCTION(BlueprintCallable, Category = "RmlUi")
	void SetElementInnerRml(const FString& ElementId, const FString& Rml);

	/** Set an attribute on an element by ID */
	UFUNCTION(BlueprintCallable, Category = "RmlUi")
	void SetElementAttribute(const FString& ElementId, const FString& Attribute, const FString& Value);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	Rml::Element* FindElementById(const FString& ElementId) const;

	TSharedPtr<SRmlWidget>	RmlSlateWidget;
	FString					ContextName;
	Rml::Context*			PrewarmedContext = nullptr;
};
