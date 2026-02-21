#include "RmlUE4GameModeBase.h"
#include "Widgets/Images/SImage.h"
#include "Kismet/KismetSystemLibrary.h"
#include "RmlUi/Debugger/Debugger.h"
#include <sstream>


ARmlUE4GameModeBase::ARmlUE4GameModeBase()
	: MainDemo(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARmlUE4GameModeBase::BeginPlay()
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
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("Delicious-Bold.otf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("Delicious-BoldItalic.otf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("Delicious-Italic.otf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("Delicious-Roman.otf"))));
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("NotoEmoji-Regular.ttf"))), true);
	Rml::LoadFontFace(TCHAR_TO_UTF8(*(FontPath + TEXT("STKAITI.TTF"))), true);
	
	// create context with initial viewport dimensions so percentage-based
	// layouts (e.g. width: 80%) resolve correctly at document load time.
	// SRmlWidget::OnPaint will set the actual dimensions on first paint.
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
	// Rml::Debugger::Initialise(Context);

	// show debugger 
	// Rml::Debugger::SetVisible(true);
	
	// load demo selector 
	FString BasePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("RmlAssets/assets/Examples/"));
	DemoSelector = NewObject<URmlDocument>(this);
	if (DemoSelector->Init(Context, BasePath + TEXT("selectbar.rml")))
	{
		DemoSelector->Show();
	}

	// load demos
	_LoadDemos(BasePath);
	
	// create widget 
	auto RmlWidget = SNew(SRmlWidget)
	.InitContext(Context);

	// add widget to viewport 
	RmlWidget->AddToViewport(GetWorld());
	
	// setup input mode 
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(RmlWidget);
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	GetWorld()->GetFirstPlayerController()->SetInputMode(InputMode);

	// enable cursor 
	GetWorld()->GetFirstPlayerController()->bShowMouseCursor = true;
}

void ARmlUE4GameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void ARmlUE4GameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// shut down documents (guard against failed Init())
	if (DemoSelector) DemoSelector->ShutDown();
	if (MainDemo) MainDemo->ShutDown();
	if (BenchMark) BenchMark->ShutDown();

	// release context
	Rml::RemoveContext("Test Context");
	Context = nullptr;

	// Null the interface pointers before GameMode members are destroyed.
	// Do NOT call Rml::Shutdown() — the UERmlUI module owns the lifecycle.
	Rml::SetFileInterface(nullptr);
	Rml::SetSystemInterface(nullptr);
	Rml::SetRenderInterface(nullptr);
}

void ARmlUE4GameModeBase::_LoadDemos(const FString& InBasePath)
{
	// setup notify object 
	DemoSelector->SetNotifyObject(TEXT("Controller"), this);

	// main demo 
	MainDemo = NewObject<URmlDemo>(this);
	MainDemo->Init(Context, InBasePath + TEXT("demo.rml"));
	
	// benchmark
	BenchMark = NewObject<URmlBenchmark>(this);
	BenchMark->Init(Context, InBasePath + TEXT("benchmark.rml"));
}

void ARmlUE4GameModeBase::_ChangeShowItem(URmlDocument* InDocument)
{
	if (CurrentElement == BenchMark)
	{
		BenchMark->bDoPerformanceTest = false;
	}
	
	if (CurrentElement) CurrentElement->GetDocument()->Hide();
	InDocument->GetDocument()->Show();
	CurrentElement = InDocument;

	if (InDocument == BenchMark)
	{
		BenchMark->bDoPerformanceTest = true;
	}
}
