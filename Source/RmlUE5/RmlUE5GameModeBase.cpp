#include "RmlUE5GameModeBase.h"
#include "SRmlWidget.h"
#include "Examples/RmlAnimation.h"
#include "Examples/RmlBenchmark.h"
#include "Examples/RmlDemo.h"
#include "Examples/RmlTransform.h"
#include "Examples/RmlEffects.h"
#include "Examples/RmlDrag.h"
#include "Examples/RmlDataBinding.h"
#include "Examples/RmlMockupInventory.h"
#include "RmlHelper.h"
#include "RmlWarmer.h"
#include "RmlUiSettings.h"
#include "RmlUi/Core.h"
#include "Widgets/Images/SImage.h"
#include "Kismet/KismetSystemLibrary.h"
#if !UE_BUILD_SHIPPING
#include "RmlUi/Debugger/Debugger.h"
#endif

ARmlUE5GameModeBase::ARmlUE5GameModeBase()
	: MainDemo(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARmlUE5GameModeBase::BeginPlay()
{
	Super::BeginPlay();

	// Re-register all interfaces each PIE session.
	// UERmlUI module calls Initialise() once at startup; we must NOT call it again.
	// EndPlay nulls the interface pointers (to avoid dangling refs to our members),
	// so we must re-register them here on every BeginPlay.
	Rml::SetFileInterface(&RmlFileInterface);
	Rml::SetSystemInterface(&RmlSystemInterface);
	Rml::SetRenderInterface(&RmlRenderInterface);

	// load font face
	FString FontPath = FPaths::ProjectContentDir() / TEXT("RmlAssets/assets/");
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("LatoLatin-Regular.ttf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("LatoLatin-Bold.ttf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("LatoLatin-Italic.ttf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("LatoLatin-BoldItalic.ttf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("NotoEmoji-Regular.ttf"))), true);
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("STKAITI.TTF"))), true);
	
	// create context with initial viewport dimensions so percentage-based
	// layouts (e.g. width: 80%) resolve correctly at document load time.
	// SRmlWidget::Tick will set the actual dimensions on first tick.
	Rml::Vector2i InitialDims(1920, 1080);
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Vps;
		GEngine->GameViewport->GetViewportSize(Vps);
		if (!Vps.IsNearlyZero())
			InitialDims = Rml::Vector2i((int)Vps.X, (int)Vps.Y);
	}
	Context = Rml::CreateContext("Test Context", InitialDims);

	// init debugger
#if !UE_BUILD_SHIPPING
	Rml::Debugger::Initialise(Context);
#endif

	// load demo selector
	FString BasePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("RmlAssets/assets/Examples/"));
	DemoSelector = NewObject<URmlDocument>(this);
	if (DemoSelector->Init(Context, BasePath + TEXT("selectbar.rml")))
	{
		DemoSelector->Show();
	}

	// load demos
	_LoadDemos(BasePath);

	// Pre-warm font effect layers from saved warmup document list.
	// CurrentDrawer is null here, so Context::Render() fires texture-generation
	// callbacks without writing anything to the screen (warmup mode).
	// In capture mode (rmlui.CaptureWarmup 1), document URLs are recorded
	// automatically in GenerateTexture() and saved to DefaultGame.ini on EndPlay.
	FRmlWarmer::WarmContext(Context, URmlUiSettings::Get()->WarmupDocuments, &RmlRenderInterface);

	// create widget
	RmlWidget = SNew(SRmlWidget)
		.InitContext(Context)
		.InitSystemInterface(&RmlSystemInterface);

	// add widget to viewport
	RmlWidget->AddToViewport(GetWorld());

	// setup input mode
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(RmlWidget.ToSharedRef());
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	GetWorld()->GetFirstPlayerController()->SetInputMode(InputMode);

	// enable cursor
	GetWorld()->GetFirstPlayerController()->bShowMouseCursor = true;
}

void ARmlUE5GameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void ARmlUE5GameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// Null the context on the Slate widget FIRST, before any cleanup.
	// The widget may still be in the viewport when EndPlay runs (Slate teardown
	// happens asynchronously). Without this, OnPaint/Tick would call into a
	// destroyed Rml::Context, causing AsShared() assertion on stale texture handles.
	if (RmlWidget.IsValid())
	{
		RmlWidget->Context(nullptr);
		RmlWidget->RemoveFromParent(GetWorld());
		RmlWidget.Reset();
	}

#if !UE_BUILD_SHIPPING
	// If capture mode was active, persist the recorded document URLs to
	// DefaultGame.ini so the next run can pre-warm without user interaction.
	if (FRmlWarmer::IsCaptureEnabled())
	{
		FRmlWarmer::SaveCapturedEntries(&RmlRenderInterface);
	}
#endif

	// shut down documents (guard against failed Init())
	if (DemoSelector) DemoSelector->ShutDown();
	if (MainDemo) MainDemo->ShutDown();
	if (BenchMark) BenchMark->ShutDown();
	if (Animation) Animation->ShutDown();
	if (Transform) Transform->ShutDown();
	if (Sprites) Sprites->ShutDown();
	if (Effects) Effects->ShutDown();
	if (Drag) Drag->ShutDownAll();
	if (DataBinding) DataBinding->ShutDown();
	if (MockupInventory) MockupInventory->ShutDown();

	// Shut down debugger BEFORE removing its host context — the debugger owns
	// documents/elements inside the context that must be cleaned up first.
#if !UE_BUILD_SHIPPING
	Rml::Debugger::Shutdown();
#endif

	// release context
	Rml::RemoveContext("Test Context");
	Context = nullptr;

	// Release all file textures from RmlUI's RenderManager for our interface.
	// RmlUI caches RenderManagers keyed by RenderInterface* — because the game
	// mode is often allocated at the same address each PIE session, the same
	// RenderManager is reused next session. File-texture handles in that manager
	// are NEVER released by RemoveContext (only by RenderManager destruction,
	// which happens only at Rml::Shutdown). Without this call, the handles remain
	// as stale raw pointers into the now-destroyed FRmlTextureEntry objects,
	// causing the AsShared() assertion at the start of the next PIE session.
	// Passing our interface pointer releases only our manager's textures.
	Rml::ReleaseTextures(&RmlRenderInterface);

	// Null the interface pointers before GameMode members are destroyed.
	// Do NOT call Rml::Shutdown() — the UERmlUI module owns the lifecycle.
	Rml::SetFileInterface(nullptr);
	Rml::SetSystemInterface(nullptr);
	Rml::SetRenderInterface(nullptr);
}

void ARmlUE5GameModeBase::_LoadDemos(const FString& InBasePath)
{
	// setup notify object
	DemoSelector->SetNotifyObject(TEXT("Controller"), this);

	// main demo
	MainDemo = NewObject<URmlDemo>(this);
	MainDemo->Init(Context, InBasePath + TEXT("demo.rml"));
	MainDemo->SetNotifyObject(TEXT("Controller"), this);

	// benchmark
	BenchMark = NewObject<URmlBenchmark>(this);
	BenchMark->Init(Context, InBasePath + TEXT("benchmark.rml"));
	BenchMark->SetNotifyObject(TEXT("Controller"), this);

	// animation
	Animation = NewObject<URmlAnimation>(this);
	Animation->Init(Context, InBasePath + TEXT("animation.rml"));
	Animation->SetNotifyObject(TEXT("Controller"), this);

	// transform
	Transform = NewObject<URmlTransform>(this);
	Transform->Init(Context, InBasePath + TEXT("transform.rml"));
	Transform->SetNotifyObject(TEXT("Controller"), this);

	// sprites viewer
	Sprites = NewObject<URmlDocument>(this);
	Sprites->Init(Context, InBasePath + TEXT("sprites.rml"));
	Sprites->SetNotifyObject(TEXT("Controller"), this);
	_SetDocumentTitle(Sprites);

	// effects (data model must exist before LoadDocument)
	Effects = NewObject<URmlEffects>(this);
	Effects->CreateDataModel(Context);
	Effects->Init(Context, InBasePath + TEXT("effects.rml"));
	Effects->SetNotifyObject(TEXT("Controller"), this);

	// drag
	Drag = NewObject<URmlDrag>(this);
	Drag->Init(Context, InBasePath + TEXT("drag/inventory.rml"));
	Drag->SetNotifyObject(TEXT("Controller"), this);

	// data binding (data models must exist before LoadDocument)
	DataBinding = NewObject<URmlDataBinding>(this);
	DataBinding->CreateDataModels(Context);
	DataBinding->Init(Context, InBasePath + TEXT("data_binding.rml"));
	DataBinding->SetNotifyObject(TEXT("Controller"), this);

	// mockup inventory
	MockupInventory = NewObject<URmlMockupInventory>(this);
	MockupInventory->Init(Context, InBasePath + TEXT("mockupinventory.rml"));
	MockupInventory->SetNotifyObject(TEXT("Controller"), this);
	_SetDocumentTitle(MockupInventory);
}

void ARmlUE5GameModeBase::OpenDemo() { _ChangeShowItem(MainDemo); }
void ARmlUE5GameModeBase::OpenBenchMark() { _ChangeShowItem(BenchMark); }
void ARmlUE5GameModeBase::OpenAnimation() { _ChangeShowItem(Animation); }
void ARmlUE5GameModeBase::OpenTransform() { _ChangeShowItem(Transform); }
void ARmlUE5GameModeBase::OpenSprites() { _ChangeShowItem(Sprites); }
void ARmlUE5GameModeBase::OpenEffects() { _ChangeShowItem(Effects); }
void ARmlUE5GameModeBase::OpenDrag() { _ChangeShowItem(Drag); }
void ARmlUE5GameModeBase::OpenDataBinding() { _ChangeShowItem(DataBinding); }
void ARmlUE5GameModeBase::OpenMockupInventory() { _ChangeShowItem(MockupInventory); }

void ARmlUE5GameModeBase::CloseDemo()
{
	if (!CurrentElement) return;

	_ChangeShowItem(nullptr);
}

void ARmlUE5GameModeBase::_SetDocumentTitle(URmlDocument* InDocument)
{
	auto* Doc = InDocument->GetDocument();
	if (!Doc) return;

	if (auto* TitleEl = Doc->GetElementById("title"))
		TitleEl->SetInnerRML(Doc->GetTitle());
}

void ARmlUE5GameModeBase::_ChangeShowItem(URmlDocument* InDocument)
{
	// Stop and clear benchmark if it was active.
	if (CurrentElement == BenchMark)
	{
		BenchMark->bDoPerformanceTest = false;
		// Clear generated rows so the hidden document doesn't keep ~500 live
		// elements that bloat Context::Update() every frame after closing.
		if (auto* Doc = BenchMark->GetDocument())
			if (auto* El = Doc->GetElementById("performance"))
				El->SetInnerRML("");
	}

	if (CurrentElement)
		CurrentElement->GetDocument()->Hide();

	CurrentElement = InDocument;

	if (InDocument)
	{
		InDocument->GetDocument()->Show();

		if (InDocument == BenchMark)
			BenchMark->bDoPerformanceTest = true;
	}
}
