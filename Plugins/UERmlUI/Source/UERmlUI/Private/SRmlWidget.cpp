#include "SRmlWidget.h"
#include "RmlHelper.h"
#include "RmlWarmer.h"
#include "RmlUiSettings.h"
#include "Logging.h"
#include "RmlInterface/UERmlSystemInterface.h"
#include "RmlInterface/UERmlRenderInterface.h"
#include "RmlUi/Core.h"

DECLARE_STATS_GROUP(TEXT("RmlUI_Game"), STATGROUP_RmlUI_Game, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("RmlUI Context Update"),  STAT_RmlUI_ContextUpdate, STATGROUP_RmlUI_Game);
DECLARE_CYCLE_STAT(TEXT("RmlUI Context Render"),  STAT_RmlUI_ContextRender, STATGROUP_RmlUI_Game);

void SRmlWidget::Construct(const FArguments& InArgs)
{
	BoundContext = InArgs._InitContext;
	CachedSystemInterface = InArgs._InitSystemInterface;
	bEnableRml = InArgs._InitEnableRml;
	CachedRenderInterface = static_cast<FUERmlRenderInterface*>(Rml::GetRenderInterface());
}

bool SRmlWidget::AddToViewport(UWorld* InWorld, int32 ZOrder)
{
	UGameViewportClient* ViewportClient = InWorld->GetGameViewport();
	if (!ViewportClient) return false;

	ViewportClient->AddViewportWidgetContent(AsShared(), ZOrder + 10);
	return true;
}

bool SRmlWidget::RemoveFromParent(UWorld* InWorld)
{
	UGameViewportClient* ViewportClient = InWorld->GetGameViewport();
	if (!ViewportClient) return false;

	ViewportClient->RemoveViewportWidgetContent(AsShared());
	return true;
}

void SRmlWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!bEnableRml || !BoundContext) return;

	// Compute physical viewport size from the widget geometry.
	FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateRenderTransform& TickTransform = AllottedGeometry.GetAccumulatedRenderTransform();
	FVector2D PhysSize = TickTransform.TransformPoint(FVector2D(Size.X, Size.Y)) - TickTransform.GetTranslation();
	if (PhysSize.X <= 0.0 || PhysSize.Y <= 0.0) return; // minimized or degenerate geometry
	float DPIScale = (Size.X > 0.0f) ? (float)(PhysSize.X / Size.X) : 1.0f;
	// Avoid downscaling dp units at low resolutions (UE DPI curve can yield < 1),
	// otherwise text appears disproportionately small after the 16:9 fit scale.
	const float EffectiveDPIScale = FMath::Max(1.0f, DPIScale);
	ViewportPhysSize = Rml::Vector2i(FMath::RoundToInt(PhysSize.X), FMath::RoundToInt(PhysSize.Y));

	// Scan visible documents for ui_base_width/ui_base_height.
	// When found, the context is extended to cover the full viewport in logical
	// coordinates (ViewportPhysSize / UiScale). The scaled document is CSS-positioned
	// centered within the extended context. Non-scaled documents (selectbar etc.)
	// fill the full context naturally, matching the physical viewport 1:1 (or scaled
	// uniformly by UiScale when viewport < base resolution). This puts all documents
	// — including cursor_proxy / drag clones — in the same coordinate space.
	float FoundBaseW = 0.f, FoundBaseH = 0.f;
	{
		const int32 NumDocuments = BoundContext->GetNumDocuments();
		for (int32 DocIndex = 0; DocIndex < NumDocuments; ++DocIndex)
		{
			Rml::ElementDocument* Doc = BoundContext->GetDocument(DocIndex);
			if (!Doc || !Doc->IsVisible())
				continue;
			const float BaseW = Doc->GetAttribute<float>("ui_base_width", 0.f);
			const float BaseH = Doc->GetAttribute<float>("ui_base_height", 0.f);
			if (BaseW > 0.f && BaseH > 0.f)
			{
				FoundBaseW = BaseW;
				FoundBaseH = BaseH;
				break;
			}
		}
	}

	Rml::Vector2i DesiredContextSize;
	float DesiredDpRatio;

	if (FoundBaseW > 0.f)
	{
		// Scaled mode: context covers the entire viewport in logical coordinates.
		// UiScale handles the remaining sub-1.0 visual scale via Slate render transform.
		const float FitScale = FMath::Min(
			static_cast<float>(ViewportPhysSize.x) / FoundBaseW,
			static_cast<float>(ViewportPhysSize.y) / FoundBaseH);
		const float RasterScale = FMath::Max(1.0f, FitScale);
		const float UiScale = FitScale / RasterScale;
		const int32 ContextW = FMath::RoundToInt(static_cast<float>(ViewportPhysSize.x) / UiScale);
		const int32 ContextH = FMath::RoundToInt(static_cast<float>(ViewportPhysSize.y) / UiScale);

		DesiredContextSize = Rml::Vector2i(ContextW, ContextH);
		DesiredDpRatio = RasterScale;
		ActiveUiScale = UiScale;

		// Position each scaled document centered within the extended context.
		// No CSS transform — the Slate render transform handles scaling uniformly.
		const float LogicalW = FoundBaseW * RasterScale;
		const float LogicalH = FoundBaseH * RasterScale;
		const float CenterX = (static_cast<float>(ContextW) - LogicalW) * 0.5f;
		const float CenterY = (static_cast<float>(ContextH) - LogicalH) * 0.5f;
		{
			const int32 NumDocuments = BoundContext->GetNumDocuments();
			for (int32 DocIndex = 0; DocIndex < NumDocuments; ++DocIndex)
			{
				Rml::ElementDocument* Doc = BoundContext->GetDocument(DocIndex);
				if (!Doc)
					continue;
				const float BW = Doc->GetAttribute<float>("ui_base_width", 0.f);
				const float BH = Doc->GetAttribute<float>("ui_base_height", 0.f);
				if (BW <= 0.f || BH <= 0.f)
					continue;
				Doc->SetProperty(Rml::PropertyId::Position, Rml::Property(Rml::Style::Position::Absolute));
				Doc->SetProperty(Rml::PropertyId::MarginTop, Rml::Property(0.f, Rml::Unit::PX));
				Doc->SetProperty(Rml::PropertyId::MarginRight, Rml::Property(0.f, Rml::Unit::PX));
				Doc->SetProperty(Rml::PropertyId::MarginBottom, Rml::Property(0.f, Rml::Unit::PX));
				Doc->SetProperty(Rml::PropertyId::MarginLeft, Rml::Property(0.f, Rml::Unit::PX));
				Doc->SetProperty(Rml::PropertyId::Left, Rml::Property(CenterX, Rml::Unit::PX));
				Doc->SetProperty(Rml::PropertyId::Top, Rml::Property(CenterY, Rml::Unit::PX));
				Doc->SetProperty(Rml::PropertyId::Width, Rml::Property(LogicalW, Rml::Unit::PX));
				Doc->SetProperty(Rml::PropertyId::Height, Rml::Property(LogicalH, Rml::Unit::PX));
			}
		}
	}
	else
	{
		// Normal mode: context = physical viewport, no scaling.
		DesiredContextSize = ViewportPhysSize;
		DesiredDpRatio = EffectiveDPIScale;
		ActiveUiScale = 1.0f;
	}

	if (DesiredContextSize != BoundContext->GetDimensions())
	{
		BoundContext->SetDimensions(DesiredContextSize);
		// NOTE: Do NOT call Rml::ReleaseTextures here. Callback textures (font effects,
		// box-shadow) depend on CSS properties (dp values), not viewport dimensions.
		// ReleaseTextures clears texture handles but leaves CallbackTexture wrappers
		// "valid", so the callbacks are never re-invoked — causing spikes when pages
		// are first opened after resize. RmlUI handles relayout internally via
		// SetDimensions; element-specific textures regenerate as needed.
	}

	// dp_ratio tracked independently of context size: warmup may set the exact
	// same context dimensions as the first physical size, causing size == last_size
	// above and leaving dp_ratio at its default — fonts would rasterize at the wrong
	// density. CachedDPIScale = 0 forces the call on the very first Tick.
	if (!FMath::IsNearlyEqual(DesiredDpRatio, CachedDPIScale, 1e-4f))
	{
		CachedDPIScale = DesiredDpRatio;
		BoundContext->SetDensityIndependentPixelRatio(DesiredDpRatio);
	}

	{ SCOPE_CYCLE_COUNTER(STAT_RmlUI_ContextUpdate); BoundContext->Update(); }

	// One-shot settle after the first real Tick — only if warmup was configured.
	// If WarmContext() ran pre-widget at estimated dimensions, the actual allotted
	// geometry may differ slightly, causing RmlUI to relayout and dirty font effect
	// atlas textures. WarmAllDocuments re-settles every loaded document so the CPU
	// ConvolutionFilter work happens now — not on first user click.
	// With no WarmupDocuments configured, the legacy path is used (no warmup at all).
	if (bNeedsWarmupSettle)
	{
		bNeedsWarmupSettle = false;

		const bool bShouldSettle = !URmlUiSettings::Get()->WarmupDocuments.IsEmpty()
			&& CachedRenderInterface
#if !UE_BUILD_SHIPPING
			&& !FRmlWarmer::IsCaptureEnabled()
#endif
			;

		if (bShouldSettle)
		{
			FRmlWarmer::WarmAllDocuments(BoundContext, CachedRenderInterface);
		}
	}
}

int32 SRmlWidget::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	if (!bEnableRml || !BoundContext) return LayerId;

	FUERmlRenderInterface* RenderInterface = CachedRenderInterface;
	if (!RenderInterface) return LayerId;
	const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
	FVector2D RenderTranslation = RenderTransform.GetTranslation();

	// Context space -> NDC.  When ActiveUiScale != 1, the context is in logical
	// resolution (larger than viewport) and the render matrix scales to physical pixels.
	// No offset: the context covers the full viewport, letterboxing is CSS-internal.
	FVector2D ScreenSize = OutDrawElements.GetPaintWindow()->GetSizeInScreen();
	if (ScreenSize.X <= 0.0 || ScreenSize.Y <= 0.0) return LayerId; // degenerate window, avoid divide by zero
	const float TX = (float)RenderTranslation.X;
	const float TY = (float)RenderTranslation.Y;
	FMatrix44f RenderMatrix(
		FPlane4f( 2.0f * ActiveUiScale / (float)ScreenSize.X,  0.0f,                                              0.0f,           0.0f),
		FPlane4f( 0.0f,                                        -2.0f * ActiveUiScale / (float)ScreenSize.Y,         0.0f,           0.0f),
		FPlane4f( 0.0f,                                         0.0f,                                              1.0f / 5000.0f, 0.0f),
		FPlane4f( 2.0f * TX / (float)ScreenSize.X - 1.0f,
		         -2.0f * TY / (float)ScreenSize.Y + 1.0f,
		          0.5f, 1.0f));

#if !UE_BUILD_SHIPPING
	// DIAGNOSTIC: Log context vs screen dimensions (one-shot)
	{
		static bool bLoggedDims = false;
		if (!bLoggedDims)
		{
			Rml::Vector2i CtxDims = BoundContext->GetDimensions();
			FVector2D LocalSize = AllottedGeometry.GetLocalSize();
			FVector2D PhysSize = RenderTransform.TransformPoint(FVector2D(LocalSize.X, LocalSize.Y)) - RenderTranslation;
			float DPIScale = (LocalSize.X > 0.0f) ? (float)(PhysSize.X / LocalSize.X) : 1.0f;
			UE_LOG(LogUERmlUI, Log, TEXT("SRmlWidget: ContextDims=%dx%d LocalSize=%.0fx%.0f PhysSize=%.0fx%.0f ScreenSize=%.0fx%.0f DPIScale=%.4f UiScale=%.4f"),
				CtxDims.x, CtxDims.y, LocalSize.X, LocalSize.Y, PhysSize.X, PhysSize.Y, ScreenSize.X, ScreenSize.Y, DPIScale, ActiveUiScale);
			bLoggedDims = true;
		}
	}
#endif

	// Scale + translate: context space -> physical screen space (for scissor/clip rects).
	FSlateRenderTransform PhysicalTransform(ActiveUiScale, FVector2f(TX, TY));
	RenderInterface->BeginRender(
		BoundContext,
		PhysicalTransform,
		RenderMatrix,
		MyCullingRect);

	{ SCOPE_CYCLE_COUNTER(STAT_RmlUI_ContextRender); BoundContext->Render(); }

	RenderInterface->EndRender(OutDrawElements, LayerId);
	return LayerId + 1;
}

FReply SRmlWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bEnableRml || !BoundContext) return FReply::Unhandled();

	auto ModifierState = InKeyEvent.GetModifierKeys();
	return BoundContext->ProcessKeyDown(
		FRmlHelper::ConvertKey(InKeyEvent.GetKey()),
		FRmlHelper::GetKeyModifierState(ModifierState)) ? FReply::Unhandled() : FReply::Handled();
}

FReply SRmlWidget::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bEnableRml || !BoundContext) return FReply::Unhandled();

	auto ModifierState = InKeyEvent.GetModifierKeys();
	return BoundContext->ProcessKeyUp(
		FRmlHelper::ConvertKey(InKeyEvent.GetKey()),
		FRmlHelper::GetKeyModifierState(ModifierState)) ? FReply::Unhandled() : FReply::Handled();
}

FReply SRmlWidget::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (!bEnableRml || !BoundContext) return FReply::Unhandled();

	TCHAR Ch = InCharacterEvent.GetCharacter();

	// Convert Windows carriage return to newline
	if (Ch == TEXT('\r'))
		return BoundContext->ProcessTextInput(Rml::Character(TEXT('\n'))) ? FReply::Unhandled() : FReply::Handled();

	if (!FChar::IsPrint(Ch)) return FReply::Unhandled();

	return BoundContext->ProcessTextInput(Rml::Character(Ch)) ? FReply::Unhandled() : FReply::Handled();
}

FReply SRmlWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEnableRml || !BoundContext) return FReply::Unhandled();

	// Screen space -> RML context space. When ActiveUiScale != 1, the context
	// is in logical resolution (larger than viewport); divide by scale to get context coords.
	FVector2D RenderTranslation = MyGeometry.GetAccumulatedRenderTransform().GetTranslation();
	FVector2D MousePos = (FVector2D(MouseEvent.GetScreenSpacePosition()) - RenderTranslation)
		/ static_cast<double>(ActiveUiScale);

	auto ModifierState = MouseEvent.GetModifierKeys();
	return BoundContext->ProcessMouseMove(
		MousePos.X,
		MousePos.Y,
		FRmlHelper::GetKeyModifierState(ModifierState)) ? FReply::Unhandled() : FReply::Handled();
}

FReply SRmlWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEnableRml || !BoundContext) return FReply::Unhandled();

	// Reset intentional-release flag for each new press.
	bReleasingCapture = false;

	auto ModifierState = MouseEvent.GetModifierKeys();
	bool bHandled = !BoundContext->ProcessMouseButtonDown(
		FRmlHelper::GetMouseKey(MouseEvent.GetEffectingButton()),
		FRmlHelper::GetKeyModifierState(ModifierState));

	if (bHandled)
		return FReply::Handled().CaptureMouse(SharedThis(this));
	return FReply::Unhandled();
}

FReply SRmlWidget::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Slate routes the second click in a rapid sequence as a double-click event,
	// bypassing OnMouseButtonDown entirely. Without this override, ProcessMouseButtonDown
	// is never called for that press, leaving RmlUI's 'active' element null — which causes
	// ProcessMouseButtonUp to silently drop the Click event. This produces the strict
	// every-other-click failure seen on data-event-click elements.
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SRmlWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEnableRml || !BoundContext) return FReply::Unhandled();

	auto ModifierState = MouseEvent.GetModifierKeys();
	bool bHandled = !BoundContext->ProcessMouseButtonUp(
		FRmlHelper::GetMouseKey(MouseEvent.GetEffectingButton()),
		FRmlHelper::GetKeyModifierState(ModifierState));

	FReply Reply = bHandled ? FReply::Handled() : FReply::Unhandled();
	if (HasMouseCapture())
	{
		// Tell OnMouseCaptureLost whether to suppress ProcessMouseLeave.
		// Only suppress when the cursor is still over the widget: hover state
		// is then still valid and must be preserved for the next rapid click.
		// If the mouse was dragged outside before release, let ProcessMouseLeave
		// fire so stale :hover/:active pseudo-classes are cleared.
		bReleasingCapture = MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition());
		Reply.ReleaseMouseCapture();
	}
	return Reply;
}

FReply SRmlWidget::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bEnableRml || !BoundContext) return FReply::Unhandled();

	auto ModifierState = MouseEvent.GetModifierKeys();
	return BoundContext->ProcessMouseWheel(
		-MouseEvent.GetWheelDelta(),
		FRmlHelper::GetKeyModifierState(ModifierState)) ? FReply::Unhandled() : FReply::Handled();
}

FCursorReply SRmlWidget::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (!bEnableRml || !BoundContext || !CachedSystemInterface)
		return FCursorReply::Unhandled();

	return FCursorReply::Cursor(CachedSystemInterface->CachedCursorState());
}

void SRmlWidget::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (bReleasingCapture)
	{
		// Intentional release AND cursor still on widget: hover state is valid,
		// do not call ProcessMouseLeave so the next rapid click finds the correct
		// hovered element. bReleasingCapture is false when the mouse was dragged
		// outside before release — that path falls through to ProcessMouseLeave.
		bReleasingCapture = false;
		return;
	}

	// External capture loss or intentional release with cursor outside widget.
	// Clear RmlUI hover state so stale :hover/:active pseudo-classes are removed.
	if (BoundContext)
		BoundContext->ProcessMouseLeave();
}
