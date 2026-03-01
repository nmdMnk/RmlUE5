#pragma once
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "RmlUi/Core.h"
#include "RmlInterface/UERmlSystemInterface.h"

class FUERmlRenderInterface;

class UERMLUI_API SRmlWidget : public SLeafWidget
{
	SLATE_BEGIN_ARGS(SRmlWidget)
		: _InitContext(nullptr)
		, _InitSystemInterface(nullptr)
		, _InitEnableRml(true)
		, _InitHandleCursor(true)
	{}
		SLATE_ARGUMENT(Rml::Context*, InitContext)
		SLATE_ARGUMENT(FUERmlSystemInterface*, InitSystemInterface)
		SLATE_ARGUMENT(bool, InitEnableRml)
		SLATE_ARGUMENT(bool, InitHandleCursor)
		/** Fired when user clicks on empty space (no RmlUi element under cursor). */
		SLATE_EVENT(FSimpleDelegate, OnEmptyClick)
	SLATE_END_ARGS()
public:
	void Construct(const FArguments& InArgs);

	bool AddToViewport(UWorld* InWorld, int32 ZOrder = 0);
	bool RemoveFromParent(UWorld* InWorld);
	
	void Context(Rml::Context* InContext) { BoundContext = InContext; }
	Rml::Context* Context() const { return BoundContext; }
	void SetOnEmptyClick(FSimpleDelegate InDelegate) { OnEmptyClick = MoveTemp(InDelegate); }
protected:
	// ~Begin SWidget API
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	// ~End SWidget API
private:
	bool					bEnableRml;
	bool					bHandleCursor;
	Rml::Context*			BoundContext;
	FUERmlSystemInterface*	CachedSystemInterface = nullptr;
	bool					bNeedsWarmupSettle = true;
	// Cached DPI scale — initialized to 0 so the first Tick always calls
	// SetDensityIndependentPixelRatio, even if context size didn't change
	// (e.g. warmup set the exact same dimensions as the first physical size).
	float					CachedDPIScale = 0.0f;
	// Set to true in OnMouseButtonUp when releasing capture while the cursor is
	// still over the widget. Tells OnMouseCaptureLost to skip ProcessMouseLeave
	// so hover state stays valid for the next rapid click. Set to false when the
	// cursor was dragged outside before release — ProcessMouseLeave fires normally
	// to clear stale :hover/:active pseudo-classes.
	bool					bReleasingCapture = false;
	// True when a left-click on empty space (no RmlUI element) was captured in
	// OnMouseButtonDown. Only then will OnMouseButtonUp fire the OnEmptyClick
	// delegate, preventing false positives from right/middle clicks or
	// mismatched down/up sequences.
	bool					bEmptyClickArmed = false;

	// Context-level scaling state — when a document with ui_base_width is visible,
	// the context is extended to cover the full viewport (in logical coords) and
	// the Slate render transform applies the remaining sub-1.0 scale. The scaled
	// document is CSS-positioned centered within the extended context; non-scaled
	// documents fill the full context naturally (= full viewport).
	float					ActiveUiScale = 1.0f;
	Rml::Vector2i			ViewportPhysSize{0, 0};
	// Cached at Construct() — Rml::GetRenderInterface() is global and invariant
	// after Rml::Initialise(). Avoids repeated static_cast every paint/tick.
	FUERmlRenderInterface*	CachedRenderInterface = nullptr;
	FSimpleDelegate			OnEmptyClick;
};
