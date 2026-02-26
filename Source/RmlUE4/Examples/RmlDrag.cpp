#include "RmlDrag.h"
#include "RmlUi/Core.h"
#include "RmlUi/Core/Factory.h"

void URmlDrag::OnInit()
{
	// --- Inventory 1 (BoundDocument, loaded by Init) ---
	BoundDocument->GetElementById("title")->SetInnerRML("Inventory 1");
	BoundDocument->SetProperty(Rml::PropertyId::MarginTop, Rml::Property(0.f, Rml::Unit::PX));
	BoundDocument->SetProperty(Rml::PropertyId::MarginRight, Rml::Property(0.f, Rml::Unit::PX));
	BoundDocument->SetProperty(Rml::PropertyId::MarginBottom, Rml::Property(0.f, Rml::Unit::PX));
	BoundDocument->SetProperty(Rml::PropertyId::MarginLeft, Rml::Property(0.f, Rml::Unit::PX));
	BoundDocument->SetProperty(Rml::PropertyId::Left, Rml::Property(18.f, Rml::Unit::PERCENT));
	BoundDocument->SetProperty(Rml::PropertyId::Top, Rml::Property(25.f, Rml::Unit::PERCENT));

	AddItem(BoundDocument, "Mk III L.A.S.E.R.");
	AddItem(BoundDocument, "Gravity Descender");
	AddItem(BoundDocument, "Closed-Loop Ion Beam");
	AddItem(BoundDocument, "5kT Mega-Bomb");

	// Register drag listener on the content container.
	if (auto* Content = BoundDocument->GetElementById("content"))
		Content->AddEventListener(Rml::EventId::Dragdrop, this);

	// --- Inventory 2 (second document, loaded programmatically) ---
	FString DocUrl(BoundDocument->GetSourceURL().c_str());
	SecondDocument = BoundContext->LoadDocument(TCHAR_TO_UTF8(*DocUrl));
	if (SecondDocument)
	{
		SecondDocument->GetElementById("title")->SetInnerRML("Inventory 2");
		SecondDocument->SetProperty(Rml::PropertyId::MarginTop, Rml::Property(0.f, Rml::Unit::PX));
		SecondDocument->SetProperty(Rml::PropertyId::MarginRight, Rml::Property(0.f, Rml::Unit::PX));
		SecondDocument->SetProperty(Rml::PropertyId::MarginBottom, Rml::Property(0.f, Rml::Unit::PX));
		SecondDocument->SetProperty(Rml::PropertyId::MarginLeft, Rml::Property(0.f, Rml::Unit::PX));
		SecondDocument->SetProperty(Rml::PropertyId::Left, Rml::Property(52.f, Rml::Unit::PERCENT));
		SecondDocument->SetProperty(Rml::PropertyId::Top, Rml::Property(25.f, Rml::Unit::PERCENT));

		if (auto* Content = SecondDocument->GetElementById("content"))
			Content->AddEventListener(Rml::EventId::Dragdrop, this);

		// Wire close button — base ProcessEvent reads onclick="Controller:CloseDemo" attribute.
		if (auto* CloseBtn = SecondDocument->GetElementById("close_button"))
			CloseBtn->AddEventListener(Rml::EventId::Click, this);

		SecondDocument->Show();
	}
}

void URmlDrag::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Sync second document visibility with the first.
	if (SecondDocument && BoundDocument)
	{
		bool bVisible = BoundDocument->IsVisible();
		if (SecondDocument->IsVisible() != bVisible)
		{
			if (bVisible)
				SecondDocument->Show();
			else
				SecondDocument->Hide();
		}
	}
}

void URmlDrag::ProcessEvent(Rml::Event& event)
{
	if (event.GetId() == Rml::EventId::Dragdrop)
	{
		HandleDragDrop(event);
		return;
	}

	URmlDocument::ProcessEvent(event);
}

void URmlDrag::HandleDragDrop(Rml::Event& event)
{
	Rml::Element* DestContainer = event.GetCurrentElement();
	Rml::Element* DestElement = event.GetTargetElement();
	Rml::Element* DragElement = static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));

	if (!DestContainer || !DragElement)
		return;

	if (DestContainer == DestElement)
	{
		// Dragged directly onto a container — append at end.
		Rml::ElementPtr Moved = DragElement->GetParentNode()->RemoveChild(DragElement);
		DestContainer->AppendChild(std::move(Moved));
	}
	else
	{
		// Dragged onto an item inside a container — insert before that item.
		Rml::Element* InsertBefore = DestElement;

		// If reordering within the same container, check direction.
		if (DragElement->GetParentNode() == DestContainer)
		{
			Rml::Element* Prev = InsertBefore->GetPreviousSibling();
			while (Prev)
			{
				if (Prev == DragElement)
				{
					InsertBefore = InsertBefore->GetNextSibling();
					break;
				}
				Prev = Prev->GetPreviousSibling();
			}
		}

		Rml::ElementPtr Moved = DragElement->GetParentNode()->RemoveChild(DragElement);
		DestContainer->InsertBefore(std::move(Moved), InsertBefore);
	}
}

void URmlDrag::AddItem(Rml::ElementDocument* Doc, const Rml::String& Name)
{
	Rml::Element* Content = Doc->GetElementById("content");
	if (!Content)
		return;

	Rml::Element* Icon = Content->AppendChild(
		Rml::Factory::InstanceElement(Content, "icon", "icon", Rml::XMLAttributes()));
	Icon->SetInnerRML(Name);
}

void URmlDrag::OnPreReload()
{
	if (SecondDocument)
	{
		SecondDocument->Close();
		SecondDocument = nullptr;
	}
}

void URmlDrag::ShutDownAll()
{
	OnPreReload();
	ShutDown();
}
