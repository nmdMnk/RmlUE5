#include "RmlMockupInventory.h"
#include "RmlUi/Core.h"

void URmlMockupInventory::OnInit()
{
	if (auto* Grid = BoundDocument->GetElementById("inv-grid"))
		Grid->AddEventListener(Rml::EventId::Dragdrop, this);
}

void URmlMockupInventory::ProcessEvent(Rml::Event& event)
{
	if (event.GetId() == Rml::EventId::Dragdrop)
	{
		HandleDragDrop(event);
		return;
	}
	URmlDocument::ProcessEvent(event);
}

void URmlMockupInventory::HandleDragDrop(Rml::Event& event)
{
	Rml::Element* Grid = event.GetCurrentElement();
	Rml::Element* DropTarget = event.GetTargetElement();
	Rml::Element* DragElement = static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));

	if (!Grid || !DragElement)
		return;

	// Source slot is the direct parent of the dragged item.
	Rml::Element* SourceSlot = DragElement->GetParentNode();
	if (!SourceSlot || SourceSlot->GetParentNode() != Grid)
		return;

	// Walk up from DropTarget to find the target slot (direct child of Grid).
	Rml::Element* TargetSlot = DropTarget;
	while (TargetSlot && TargetSlot->GetParentNode() != Grid)
		TargetSlot = TargetSlot->GetParentNode();

	if (!TargetSlot || TargetSlot == SourceSlot)
		return;

	// Swap slot contents: move target's child (item or item-empty) to source,
	// move dragged item to target. Grid layout never changes (slots stay fixed).
	Rml::Element* TargetContent = TargetSlot->GetNumChildren() > 0 ? TargetSlot->GetChild(0) : nullptr;

	Rml::ElementPtr MovedItem = SourceSlot->RemoveChild(DragElement);
	if (TargetContent)
	{
		Rml::ElementPtr MovedContent = TargetSlot->RemoveChild(TargetContent);
		SourceSlot->AppendChild(std::move(MovedContent));
	}
	TargetSlot->AppendChild(std::move(MovedItem));
}
