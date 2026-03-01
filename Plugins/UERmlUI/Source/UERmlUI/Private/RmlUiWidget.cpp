#include "RmlUiWidget.h"
#include "SRmlWidget.h"
#include "RmlWarmer.h"
#include "RmlUiSettings.h"
#include "RmlInterface/UERmlRenderInterface.h"
#include "RmlUi/Core.h"

static int32 GRmlContextCounter = 0;

TSharedRef<SWidget> URmlUiWidget::RebuildWidget()
{
	// Destroy any previous context, unless we have a pre-warmed one ready.
	if (!ContextName.IsEmpty() && !PrewarmedContext)
	{
		// Null the context on the old SRmlWidget BEFORE removing it.
		// Slate may tick the old widget one more time after RebuildWidget returns;
		// without this, it would access the freed context (use-after-free on root_element).
		if (RmlSlateWidget.IsValid())
			RmlSlateWidget->Context(nullptr);
		Rml::RemoveContext(TCHAR_TO_UTF8(*ContextName));
		ContextName.Empty();
	}

	// Use pre-warmed context if available, otherwise create fresh at 1×1.
	// The actual dimensions are set from OnPaint geometry on the first frame.
	if (!PrewarmedContext)
	{
		ContextName = FString::Printf(TEXT("UERmlWidget_%d"), GRmlContextCounter++);
		PrewarmedContext = Rml::CreateContext(
			TCHAR_TO_UTF8(*ContextName),
			Rml::Vector2i(1, 1));
	}
	// else: ContextName + PrewarmedContext already set by PrewarmFromSettings()

	Rml::Context* Context = PrewarmedContext;
	PrewarmedContext = nullptr;

	RmlSlateWidget = SNew(SRmlWidget)
		.InitContext(Context)
		.InitEnableRml(true)
		.InitHandleCursor(bUseRmlCursor);

	// Load font faces before the document so text renders on the first frame.
	// Fonts are global in RmlUi — duplicates are silently ignored.
	for (const FRmlFontEntry& Font : Fonts)
	{
		if (Font.FontPath.IsEmpty())
			continue;
		Rml::LoadFontFace(TCHAR_TO_UTF8(*Font.FontPath));
	}

	// Fire callback so external code can register data models before the document loads.
	if (OnContextReady)
		OnContextReady(Context);

	// Auto-load DocumentPath if set
	if (Context && !DocumentPath.IsEmpty())
	{
		Rml::ElementDocument* Doc = Context->LoadDocument(TCHAR_TO_UTF8(*DocumentPath));
		if (Doc)
			Doc->Show();
	}

	return RmlSlateWidget.ToSharedRef();
}

void URmlUiWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (RmlSlateWidget.IsValid())
		RmlSlateWidget->Context(nullptr);
	if (!ContextName.IsEmpty())
	{
		Rml::RemoveContext(TCHAR_TO_UTF8(*ContextName));
		ContextName.Empty();
	}
	PrewarmedContext = nullptr;
	RmlSlateWidget.Reset();
}

void URmlUiWidget::PrewarmFromSettings()
{
	const TArray<FRmlWarmupDocumentEntry>& Entries = URmlUiSettings::Get()->WarmupDocuments;
	if (Entries.IsEmpty()) return;

	// Abandon any stale pre-warmed context from a previous call.
	if (PrewarmedContext)
	{
		Rml::RemoveContext(TCHAR_TO_UTF8(*ContextName));
		PrewarmedContext = nullptr;
	}

	// Determine viewport dimensions for correct layout during warmup.
	Rml::Vector2i Dims(1920, 1080);
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Vps;
		GEngine->GameViewport->GetViewportSize(Vps);
		if (!Vps.IsNearlyZero())
			Dims = Rml::Vector2i(static_cast<int>(Vps.X), static_cast<int>(Vps.Y));
	}

	ContextName = FString::Printf(TEXT("UERmlWidget_%d"), GRmlContextCounter++);
	PrewarmedContext = Rml::CreateContext(TCHAR_TO_UTF8(*ContextName), Dims);

	FUERmlRenderInterface* RI = static_cast<FUERmlRenderInterface*>(Rml::GetRenderInterface());
	FRmlWarmer::WarmContext(PrewarmedContext, Entries, RI);
}

int32 URmlUiWidget::ReloadDocuments()
{
	Rml::Context* Ctx = GetContext();
	if (!Ctx) return 0;

	// Collect source URLs and visibility of all documents.
	struct FDocInfo { Rml::String SourceURL; bool bVisible; };
	Rml::Vector<FDocInfo> Docs;
	for (int32 i = 0; i < Ctx->GetNumDocuments(); ++i)
	{
		Rml::ElementDocument* Doc = Ctx->GetDocument(i);
		if (!Doc) continue;
		// Skip internal documents (debugger, etc.) that have no source URL.
		Rml::String URL = Doc->GetSourceURL();
		if (URL.empty()) continue;
		Docs.push_back({std::move(URL), Doc->IsVisible()});
	}

	// Close all collected documents (deferred — cleaned up on next Context::Update).
	for (int32 i = Ctx->GetNumDocuments() - 1; i >= 0; --i)
	{
		Rml::ElementDocument* Doc = Ctx->GetDocument(i);
		if (Doc && !Doc->GetSourceURL().empty())
			Doc->Close();
	}

	// Re-load from disk.
	int32 Count = 0;
	for (const FDocInfo& Info : Docs)
	{
		Rml::ElementDocument* Doc = Ctx->LoadDocument(Info.SourceURL);
		if (Doc)
		{
			if (Info.bVisible)
				Doc->Show();
			++Count;
		}
	}

	OnDocumentsReloaded(Ctx);

	return Count;
}

Rml::Context* URmlUiWidget::GetContext() const
{
	if (RmlSlateWidget.IsValid())
		return RmlSlateWidget->Context();
	return nullptr;
}

void URmlUiWidget::LoadDocument(const FString& InDocumentPath)
{
	if (!RmlSlateWidget.IsValid()) return;
	Rml::Context* Ctx = RmlSlateWidget->Context();
	if (!Ctx) return;

	Rml::ElementDocument* Doc = Ctx->LoadDocument(TCHAR_TO_UTF8(*InDocumentPath));
	if (Doc)
		Doc->Show();
}

Rml::Element* URmlUiWidget::FindElementById(const FString& ElementId) const
{
	if (!RmlSlateWidget.IsValid()) return nullptr;
	Rml::Context* Ctx = RmlSlateWidget->Context();
	if (!Ctx) return nullptr;

	Rml::Element* Focus = Ctx->GetFocusElement();
	if (!Focus) return nullptr;

	return Focus->GetOwnerDocument()->GetElementById(TCHAR_TO_UTF8(*ElementId));
}

void URmlUiWidget::SetElementInnerRml(const FString& ElementId, const FString& RmlContent)
{
	if (Rml::Element* El = FindElementById(ElementId))
		El->SetInnerRML(TCHAR_TO_UTF8(*RmlContent));
}

void URmlUiWidget::SetElementAttribute(const FString& ElementId, const FString& Attribute, const FString& Value)
{
	if (Rml::Element* El = FindElementById(ElementId))
		El->SetAttribute(TCHAR_TO_UTF8(*Attribute), TCHAR_TO_UTF8(*Value));
}

void URmlUiWidget::EnableEmptyClickCapture()
{
	if (RmlSlateWidget.IsValid())
		RmlSlateWidget->SetOnEmptyClick(FSimpleDelegate::CreateUObject(this, &URmlUiWidget::OnEmptyClick));
}

#if WITH_EDITOR
const FText URmlUiWidget::GetPaletteCategory()
{
	return FText::FromString(TEXT("RmlUi"));
}
#endif
