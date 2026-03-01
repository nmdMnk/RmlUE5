#include "RmlInventory.h"

using namespace RmlInventoryUtils;

void URmlInventory::CreateDataModel(Rml::Context* InContext)
{
	InventoryHandle = RegisterDataModel(InContext, Slots, EquipWeapons, Coins, Gems);
	PopulateDemoInventory(Slots, Coins, Gems, InventoryHandle);
}

void URmlInventory::BindToDocument(Rml::ElementDocument* Doc)
{
	if (!Doc) return;

	// Root listener only once per context — catches clicks anywhere:
	// items and empty grid space bubble up item → document → root;
	// clicks on other documents (selectbar) hit the root directly.
	if (!bRootListenerBound)
	{
		if (Rml::Context* Ctx = Doc->GetContext())
		{
			Ctx->GetRootElement()->AddEventListener(Rml::EventId::Click, this);
			bRootListenerBound = true;
		}
	}

	// Document element listeners — always rebind (elements are new after reload).
	if (Rml::Element* Grid = Doc->GetElementById("inv-grid"))
	{
		Grid->AddEventListener(Rml::EventId::Dragdrop, this);
		Grid->AddEventListener(Rml::EventId::Dragstart, this);
	}

	const char* EquipIds[] = {
		"equip-main", "equip-secondary", "equip-range",
		"equip-head", "equip-chest", "equip-hands", "equip-legs", "equip-feet"
	};
	for (const char* Id : EquipIds)
		if (Rml::Element* El = Doc->GetElementById(Id))
		{
			El->AddEventListener(Rml::EventId::Dragdrop, this);
			El->AddEventListener(Rml::EventId::Dragstart, this);
		}
}

void URmlInventory::ClearSelection()
{
	for (auto& S : Slots)
		S.Selected = 0;
	for (int i = 0; i < GNumEquipSlots; ++i)
		EquipWeapons[i].Selected = 0;
	InventoryHandle.DirtyVariable("slots");
	DirtyAllEquip(InventoryHandle);
}

void URmlInventory::OnInit()
{
	BindToDocument(BoundDocument);
}

void URmlInventory::ProcessEvent(Rml::Event& event)
{
	if (event.GetId() == Rml::EventId::Dragdrop)
	{
		HandleDragDrop(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		return;
	}
	if (event.GetId() == Rml::EventId::Dragstart)
	{
		HandleDragStart(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		return;
	}
	if (event.GetId() == Rml::EventId::Click)
		HandleClick(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);

	URmlDocument::ProcessEvent(event);
}
