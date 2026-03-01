#include "RmlDocument.h"
#include "RmlUi/Core.h"

bool URmlDocument::Init(Rml::Context* InCtx, const FString& InDocPath)
{
	if (!InCtx) return false;
	if (BoundDocument) return false;

	// Register event listener instancer
	class UEEventListenerInstancer : public Rml::EventListenerInstancer
	{
	public:
		UEEventListenerInstancer(URmlDocument* InDoc) : Doc(InDoc) {}
		Rml::EventListener* InstanceEventListener(const Rml::String& value, Rml::Element* element) override
		{
			return Doc;
		}
		URmlDocument*	Doc;
	} Instancer(this);
	Rml::Factory::RegisterEventListenerInstancer(&Instancer);

	// Load document — path relative to project dir so RmlUi's URL class never
	// sees a Windows drive letter (it would encode "C:" as "C|", breaking hrefs).
	// Skip relativization if already relative (e.g. during Reload, where
	// GetSourceURL() returns a path relative to the project dir).
	FString RelDocPath = InDocPath;
	if (!FPaths::IsRelative(RelDocPath))
		FPaths::MakePathRelativeTo(RelDocPath, *FPaths::ProjectDir());
	BoundDocument = InCtx->LoadDocument(TCHAR_TO_UTF8(*RelDocPath));
	if (!BoundDocument) return false;

	// Unregister event listener instancer
	Rml::Factory::RegisterEventListenerInstancer(nullptr);

	BoundDocument->AddEventListener(Rml::EventId::Keydown, this);
	BoundDocument->AddEventListener(Rml::EventId::Keyup, this);

	BoundContext = InCtx;

	OnInit();

	return true;
}

bool URmlDocument::Reload()
{
	if (!BoundDocument || !BoundContext) return false;

	const bool bWasVisible = BoundDocument->IsVisible();
	const FString DocPath = FString(BoundDocument->GetSourceURL().c_str());
	Rml::Context* Ctx = BoundContext;

	// Let subclasses close extra documents/resources before we close ours.
	OnPreReload();

	BoundDocument->Close();
	BoundDocument = nullptr;
	BoundContext = nullptr;

	if (!Init(Ctx, DocPath))
		return false;

	if (bWasVisible)
		BoundDocument->Show();

	return true;
}

void URmlDocument::ShutDown()
{
	if (BoundDocument)
	{
		BoundDocument->Close();
		BoundDocument = nullptr;
		BoundContext = nullptr;
	}
}

void URmlDocument::ProcessEvent(Rml::Event& event)
{
	if (event == "keydown")
	{
		CurrentEvent = &event;
		OnKeyDown();
		return;
	}

	if (event == "keyup")
	{
		CurrentEvent = &event;
		OnKeyUp();
		return;
	}

	// Parse "Object:Function" from the onclick/onchange/etc. attribute.
	const Rml::Variant* AttrValue = event.GetCurrentElement()->GetAttribute("on" + event.GetType());
	if (!AttrValue)
		return;

	FString EventName(AttrValue->Get(Rml::String()).c_str());

	FString Object, Function;
	if (!EventName.Split(TEXT(":"), &Object, &Function))
	{
		Function = MoveTemp(EventName);
	}

	// Resolve target object and UFunction
	UObject* Obj = Object.IsEmpty() ? this : EventNotifyMap.FindRef(Object);

	UFunction* Func = Obj ? Obj->GetClass()->FindFunctionByName(FName(Function)) : nullptr;

	if (Obj && Func)
	{
		if (Func->ReturnValueOffset != 65535 || Func->NumParms != 0)
			return;

		CurrentEvent = &event;
		FFrame Stack(Obj, Func, nullptr);
		Func->Invoke(Obj, Stack, nullptr);
	}
}

void URmlDocument::BeginDestroy()
{
	Super::BeginDestroy();
	ShutDown();
}
