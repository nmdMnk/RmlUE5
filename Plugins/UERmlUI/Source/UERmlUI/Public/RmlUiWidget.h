#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "RmlUiWidget.generated.h"

class SRmlWidget;
namespace Rml { class Context; class Element; }

/** One font face to load when the widget initializes. */
USTRUCT(BlueprintType)
struct UERMLUI_API FRmlFontEntry
{
	GENERATED_BODY()

	/** Font file path, relative to project root (e.g. Content/RmlAssets/assets/LatoLatin-Regular.ttf). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RmlUi")
	FString FontPath;
};

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
	FString DocumentPath;

	/** When true, RmlUi controls the mouse cursor (e.g. move arrow on draggable items). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RmlUi")
	bool bUseRmlCursor = true;

	/**
	 * Font faces to load before the document is shown.
	 * Paths are relative to the project root (e.g. Content/RmlAssets/assets/LatoLatin-Regular.ttf).
	 * Fonts are global in RmlUi — loading them here makes them available to all widgets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RmlUi")
	TArray<FRmlFontEntry> Fonts;

	/**
	 * Pre-warm font effect textures from URmlUiSettings::WarmupDocuments.
	 * Call BEFORE AddToViewport (e.g. in BeginPlay) to avoid CPU spikes on first render.
	 * No-op if WarmupDocuments is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "RmlUi|Performance")
	void PrewarmFromSettings();

	/**
	 * Close all documents in this widget's context and reload them from disk.
	 * Calls OnDocumentsReloaded() after all documents are re-loaded so subclasses
	 * can re-bind event listeners. Data models on the context are preserved.
	 * Call Rml::Factory::ClearStyleSheetCache() before if RCSS changes are involved.
	 * @return Number of documents reloaded.
	 */
	int32 ReloadDocuments();

	/** Load an RML document into the RmlUi context */
	UFUNCTION(BlueprintCallable, Category = "RmlUi")
	void LoadDocument(const FString& InDocumentPath);

	/** Set the inner RML of an element by ID */
	UFUNCTION(BlueprintCallable, Category = "RmlUi")
	void SetElementInnerRml(const FString& ElementId, const FString& Rml);

	/** Set an attribute on an element by ID */
	UFUNCTION(BlueprintCallable, Category = "RmlUi")
	void SetElementAttribute(const FString& ElementId, const FString& Attribute, const FString& Value);

	/** Get the RmlUi context owned by this widget. Null before the widget is built. */
	Rml::Context* GetContext() const;

	/**
	 * Optional C++ callback fired after context creation + font loading,
	 * but BEFORE DocumentPath is loaded. Set this to register data models.
	 *
	 * Example:
	 *   RmlWidget->OnContextReady = [this](Rml::Context* Ctx) {
	 *       MyInventory.CreateDataModel(Ctx);
	 *   };
	 */
	TFunction<void(Rml::Context*)> OnContextReady;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	/** Called after ReloadDocuments() re-loads all documents. Override to re-bind
	 *  event listeners or other per-document state (data models are preserved). */
	virtual void OnDocumentsReloaded(Rml::Context* Ctx) {}

	/** Called when the user clicks on empty space (no RmlUi element under cursor).
	 *  Override to clear selection or perform other deselection logic.
	 *  Must call EnableEmptyClickCapture() in RebuildWidget to activate. */
	virtual void OnEmptyClick() {}

	/** Enable OnEmptyClick — call after Super::RebuildWidget() in subclasses. */
	void EnableEmptyClickCapture();

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	Rml::Element* FindElementById(const FString& ElementId) const;

	TSharedPtr<SRmlWidget>	RmlSlateWidget;
	FString					ContextName;
	Rml::Context*			PrewarmedContext = nullptr;
};
