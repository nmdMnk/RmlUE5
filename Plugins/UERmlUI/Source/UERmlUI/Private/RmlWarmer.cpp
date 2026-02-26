#include "RmlWarmer.h"
#include "RmlInterface/UERmlRenderInterface.h"
#include "RmlUiSettings.h"
#include "Logging.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Elements/ElementTabSet.h"

/**
 * Run convergence passes until no new textures are generated.
 * Each pass does Update+Render and checks for new warmup textures.
 */
static void WarmSettle(Rml::Context* Context, FUERmlRenderInterface* RI, int32 MaxPasses = 20)
{
	for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
	{
		RI->ResetWarmupCounter();
		Context->Update();
		Context->Render();
		if (RI->GetWarmupTextureCount() == 0)
			break;
	}
}

/**
 * Cycle all tabs in a tabset, running Update+Render for each tab until
 * no new textures are generated. Returns total new textures generated.
 */
static int32 WarmTabset(Rml::Context* Context, FUERmlRenderInterface* RI,
	Rml::ElementTabSet* Tabset, int32 RestoreTab = -1)
{
	const int32 NumTabs = Tabset->GetNumTabs();
	int32 TotalNew = 0;

	for (int32 Round = 0; Round < 20; ++Round)
	{
		int32 NewThisRound = 0;
		for (int32 Tab = 0; Tab < NumTabs; ++Tab)
		{
			Tabset->SetActiveTab(Tab);
			RI->ResetWarmupCounter();
			Context->Update();
			Context->Render();
			NewThisRound += RI->GetWarmupTextureCount();
		}
		TotalNew += NewThisRound;
		if (NewThisRound == 0)
			break;
	}

	if (RestoreTab >= 0)
		Tabset->SetActiveTab(RestoreTab);

	WarmSettle(Context, RI);
	return TotalNew;
}

#if !UE_BUILD_SHIPPING

static TAutoConsoleVariable<int32> CVarRmlCaptureWarmup(
	TEXT("rmlui.CaptureWarmup"),
	0,
	TEXT("Enable RmlUi warmup document capture.\n")
	TEXT("Navigate all UI pages and tabs in PIE, then stop PIE.\n")
	TEXT("Document paths are saved to DefaultGame.ini for use as warmup entries."),
	ECVF_Default);

bool FRmlWarmer::IsCaptureEnabled()
{
	return CVarRmlCaptureWarmup.GetValueOnGameThread() > 0;
}

void FRmlWarmer::SaveCapturedEntries(FUERmlRenderInterface* RI)
{
	if (!RI) return;

	const TSet<FString>& Urls = RI->GetCapturedDocumentUrls();
	if (Urls.IsEmpty())
	{
		UE_LOG(LogUERmlUI, Log, TEXT("RmlWarmer: no documents captured — nothing saved"));
		return;
	}

	URmlUiSettings* Settings = GetMutableDefault<URmlUiSettings>();
	Settings->WarmupDocuments.Empty();
	for (const FString& Url : Urls)
	{
		FRmlWarmupDocumentEntry Entry;
		Entry.DocumentPath = Url;
		Settings->WarmupDocuments.Add(Entry);
	}
	Settings->UpdateDefaultConfigFile();

	UE_LOG(LogUERmlUI, Log, TEXT("RmlWarmer: saved %d warmup entries to DefaultGame.ini"),
		Settings->WarmupDocuments.Num());
}

#endif // !UE_BUILD_SHIPPING

// ============================================================================

void FRmlWarmer::WarmContext(
	Rml::Context* Context,
	const TArray<FRmlWarmupDocumentEntry>& Entries,
	FUERmlRenderInterface* RI)
{
	if (!Context || !RI || Entries.IsEmpty()) return;

#if !UE_BUILD_SHIPPING
	// In capture mode, skip warmup so live rendering triggers GenerateTexture
	// calls where document URLs can be captured for the settings list.
	if (IsCaptureEnabled()) return;
#endif

	for (const FRmlWarmupDocumentEntry& Entry : Entries)
	{
		const std::string UrlUtf8 = TCHAR_TO_UTF8(*Entry.DocumentPath);

		// Prefer an already-loaded document with this URL so we warm the actual
		// instance (and its CallbackTexture handles) rather than a parallel copy.
		// Warming a copy (Document B) leaves Document A's CallbackTextureSources
		// with no handles, causing the CPU ConvolutionFilter to run on first live render.
		Rml::ElementDocument* Doc = nullptr;
		bool bLoadedForWarmup = false;

		for (int32 i = 0, N = Context->GetNumDocuments(); i < N; ++i)
		{
			Rml::ElementDocument* Existing = Context->GetDocument(i);
			if (Existing && Existing->GetSourceURL() == UrlUtf8)
			{
				Doc = Existing;
				break;
			}
		}

		if (!Doc)
		{
			Doc = Context->LoadDocument(UrlUtf8);
			bLoadedForWarmup = true;
		}

		if (!Doc)
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("RmlWarmer: failed to load '%s'"), *Entry.DocumentPath);
			continue;
		}

		const bool bWasVisible = Doc->IsVisible();
		if (!bWasVisible)
			Doc->Show();

		WarmSettle(Context, RI);

		// Auto-detect tabset and cycle all tabs
		Rml::ElementList Tabsets;
		Doc->GetElementsByTagName(Tabsets, "tabset");
		if (!Tabsets.empty())
			WarmTabset(Context, RI, static_cast<Rml::ElementTabSet*>(Tabsets[0]), 0);

		if (bLoadedForWarmup)
			Doc->Close(); // loaded only for warmup — remove from context
		else if (!bWasVisible)
			Doc->Hide(); // restore original hidden state
	}

	RI->PreallocateTextureReserves();
	UE_LOG(LogUERmlUI, Log, TEXT("RmlWarmer: warmup complete (%d documents)"), Entries.Num());
}

// ============================================================================

void FRmlWarmer::WarmAllDocuments(Rml::Context* Context, FUERmlRenderInterface* RI)
{
	if (!Context || !RI) return;

#if !UE_BUILD_SHIPPING
	if (IsCaptureEnabled()) return;
#endif

	const int32 NumDocs = Context->GetNumDocuments();
	if (NumDocs == 0) return;

	int32 TotalNewTextures = 0;

	for (int32 DocIdx = 0; DocIdx < NumDocs; ++DocIdx)
	{
		Rml::ElementDocument* Doc = Context->GetDocument(DocIdx);
		if (!Doc) continue;

		const bool bWasVisible = Doc->IsVisible();
		if (!bWasVisible)
			Doc->Show();

		RI->ResetWarmupCounter();
		Context->Update();
		Context->Render();
		TotalNewTextures += RI->GetWarmupTextureCount();

		// Cycle tabsets to warm hidden tab content
		Rml::ElementList Tabsets;
		Doc->GetElementsByTagName(Tabsets, "tabset");
		if (!Tabsets.empty())
		{
			auto* Tabset = static_cast<Rml::ElementTabSet*>(Tabsets[0]);
			TotalNewTextures += WarmTabset(Context, RI, Tabset, Tabset->GetActiveTab());
		}

		if (!bWasVisible)
			Doc->Hide();
	}

	if (TotalNewTextures > 0)
	{
		RI->PreallocateTextureReserves();
		UE_LOG(LogUERmlUI, Log, TEXT("RmlWarmer: post-dim settle caught %d new textures across %d documents"),
			TotalNewTextures, NumDocs);
	}
}
