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
		.InitEnableRml(true);

	// Auto-load DefaultDocument if set
	if (Context && !DefaultDocument.IsEmpty())
	{
		Rml::ElementDocument* Doc = Context->LoadDocument(TCHAR_TO_UTF8(*DefaultDocument));
		if (Doc)
			Doc->Show();
	}

	return RmlSlateWidget.ToSharedRef();
}

void URmlUiWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
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
			Dims = Rml::Vector2i((int)Vps.X, (int)Vps.Y);
	}

	ContextName = FString::Printf(TEXT("UERmlWidget_%d"), GRmlContextCounter++);
	PrewarmedContext = Rml::CreateContext(TCHAR_TO_UTF8(*ContextName), Dims);

	FUERmlRenderInterface* RI = static_cast<FUERmlRenderInterface*>(Rml::GetRenderInterface());
	FRmlWarmer::WarmContext(PrewarmedContext, Entries, RI);
}

void URmlUiWidget::LoadDocument(const FString& DocumentPath)
{
	if (!RmlSlateWidget.IsValid()) return;
	Rml::Context* Ctx = RmlSlateWidget->Context();
	if (!Ctx) return;

	Rml::ElementDocument* Doc = Ctx->LoadDocument(TCHAR_TO_UTF8(*DocumentPath));
	if (Doc)
	{
		Doc->Show();
	}
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

#if WITH_EDITOR
const FText URmlUiWidget::GetPaletteCategory()
{
	return FText::FromString(TEXT("RmlUi"));
}
#endif
