#pragma once

#include "CoreMinimal.h"
#include "RmlUiSettings.h"

namespace Rml { class Context; }
class FUERmlRenderInterface;

/**
 * Utility for pre-warming RmlUi font effect textures before widgets are shown.
 *
 * Typical usage in BeginPlay (before creating the Slate widget):
 *   FRmlWarmer::WarmContext(Context, URmlUiSettings::Get()->WarmupDocuments, &RmlRenderInterface);
 *
 * Capture workflow (non-shipping only):
 *   1. Set console variable: rmlui.CaptureWarmup 1
 *   2. Navigate all UI pages and tabs in PIE
 *   3. Stop PIE — document paths are written to DefaultGame.ini automatically
 *   4. Next session: WarmContext() pre-warms from the saved list
 */
class UERMLUI_API FRmlWarmer
{
public:
	/**
	 * Pre-warm all documents in the entries list against the given context.
	 * Loads each document (hidden), auto-detects any <tabset> and cycles all tabs,
	 * then runs convergence passes until no new textures are generated.
	 * Call in BeginPlay BEFORE creating SRmlWidget.
	 */
	static void WarmContext(Rml::Context* Context, const TArray<FRmlWarmupDocumentEntry>& Entries, FUERmlRenderInterface* RI);

	/**
	 * Warm ALL documents currently loaded in the context (regardless of settings).
	 * Iterates Context->GetNumDocuments(), shows each, settles, cycles tabsets,
	 * then restores the original visibility state.
	 * Designed for use after viewport dimensions change (first SRmlWidget::Tick)
	 * to catch font effect atlases dirtied by relayout.
	 */
	static void WarmAllDocuments(Rml::Context* Context, FUERmlRenderInterface* RI);

#if !UE_BUILD_SHIPPING
	/**
	 * Save the document URLs captured during this session to
	 * URmlUiSettings::WarmupDocuments (DefaultGame.ini).
	 * Call on EndPlay when rmlui.CaptureWarmup is active.
	 */
	static void SaveCapturedEntries(FUERmlRenderInterface* RI);

	/** Returns true if rmlui.CaptureWarmup console variable is set. */
	static bool IsCaptureEnabled();
#endif
};
