#include "RmlInventory.h"

using namespace RmlInventoryUtils;

void URmlInventory::CreateDataModel(Rml::Context* InContext)
{
	InventoryHandle = RegisterDataModel(
		InContext, Slots, EquipWeapons, Coins, Gems, SlotsUsed, SlotsTotal);
	PopulateDemoInventory(
		Slots, EquipWeapons, Coins, Gems, SlotsUsed, SlotsTotal, InventoryHandle);
}

void URmlInventory::BindToDocument(Rml::ElementDocument* Doc)
{
	if (!Doc)
		return;

	// Root listener is bound once per context.
	// Item and grid-space events bubble item -> document -> root.
	// Clicks on other documents also hit the context root.
	if (!bRootListenerBound)
	{
		if (Rml::Context* Ctx = Doc->GetContext())
		{
			Ctx->GetRootElement()->AddEventListener(Rml::EventId::Click, this);
			Ctx->GetRootElement()->AddEventListener(Rml::EventId::Mouseover, this);
			bRootListenerBound = true;
		}
	}

	// Document element listeners are rebound after each reload.
	if (Rml::Element* Grid = Doc->GetElementById("inv-grid"))
	{
		Grid->AddEventListener(Rml::EventId::Dragdrop, this);
		Grid->AddEventListener(Rml::EventId::Dragstart, this);
	}

	const char* EquipIds[] = {
		"equip-main", "equip-secondary", "equip-range",
		"equip-acc-1", "equip-acc-2", "equip-acc-3", "equip-acc-4", "equip-acc-5"
	};

	for (const char* Id : EquipIds)
	{
		if (Rml::Element* El = Doc->GetElementById(Id))
		{
			El->AddEventListener(Rml::EventId::Dragdrop, this);
			El->AddEventListener(Rml::EventId::Dragstart, this);
		}
	}
}

void URmlInventory::ClearSelection()
{
	ClearAllSelections(Slots, EquipWeapons);
	InventoryHandle.DirtyVariable("slots");
	DirtyAllEquip(InventoryHandle);
}

void URmlInventory::OnInit()
{
	BindToDocument(BoundDocument);
}

void URmlInventory::ProcessEvent(Rml::Event& event)
{
	switch (event.GetId())
	{
	case Rml::EventId::Dragdrop:
		HandleDragDrop(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		break;
	case Rml::EventId::Dragstart:
		HandleDragStart(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		break;
	case Rml::EventId::Mouseover:
		HandleHover(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		break;
	case Rml::EventId::Click:
		HandleClick(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		break;
	default:
		break;
	}

	URmlDocument::ProcessEvent(event);
}
