#include "RmlUiWidget.h"
#include "SRmlWidget.h"
#include "RmlUi/Core.h"

static int32 GRmlContextCounter = 0;

TSharedRef<SWidget> URmlUiWidget::RebuildWidget()
{
	// Destroy any previous context
	if (!ContextName.IsEmpty())
	{
		Rml::RemoveContext(TCHAR_TO_UTF8(*ContextName));
		ContextName.Empty();
	}

	// Create a unique Rml::Context with a 1×1 placeholder — same pattern as SBluBrowser.
	// The actual context dimensions are set from OnPaint geometry on the first frame.
	ContextName = FString::Printf(TEXT("UERmlWidget_%d"), GRmlContextCounter++);
	Rml::Context* Context = Rml::CreateContext(
		TCHAR_TO_UTF8(*ContextName),
		Rml::Vector2i(1, 1));

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
	RmlSlateWidget.Reset();
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

void URmlUiWidget::SetElementInnerRml(const FString& ElementId, const FString& RmlContent)
{
	if (!RmlSlateWidget.IsValid()) return;
	Rml::Context* Ctx = RmlSlateWidget->Context();
	if (!Ctx) return;

	Rml::Element* El = Ctx->GetFocusElement();
	if (!El) return;

	El = El->GetOwnerDocument()->GetElementById(TCHAR_TO_UTF8(*ElementId));
	if (El)
	{
		El->SetInnerRML(TCHAR_TO_UTF8(*RmlContent));
	}
}

void URmlUiWidget::SetElementAttribute(const FString& ElementId, const FString& Attribute, const FString& Value)
{
	if (!RmlSlateWidget.IsValid()) return;
	Rml::Context* Ctx = RmlSlateWidget->Context();
	if (!Ctx) return;

	Rml::Element* El = Ctx->GetFocusElement();
	if (!El) return;

	El = El->GetOwnerDocument()->GetElementById(TCHAR_TO_UTF8(*ElementId));
	if (El)
	{
		El->SetAttribute(TCHAR_TO_UTF8(*Attribute), TCHAR_TO_UTF8(*Value));
	}
}

#if WITH_EDITOR
const FText URmlUiWidget::GetPaletteCategory()
{
	return FText::FromString(TEXT("RmlUi"));
}
#endif
