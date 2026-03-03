#pragma once

#include "RmlUi/Core.h"

// ---------------------------------------------------------------------------
// Item definition — immutable, shared across widgets (inventory, trade, shop).
// NOT registered with the data model; used as a C++ lookup table.
// ---------------------------------------------------------------------------

struct FRmlItemDef
{
	Rml::String Id;
	Rml::String Name;
	Rml::String Icon;          // full decorator value, e.g. "image(/Game/Texture/present.present)"
	Rml::String RarityColor;   // hex color, e.g. "#a335eeff"
	Rml::String RarityName;    // "Common", "Epic", etc.
	Rml::String TypeName;      // "Weapon", "Head", "Consumable", etc.
	Rml::String Flavor;
	Rml::Vector<Rml::String> Stats;
	bool bStackable = false;
	int MaxStack = 1;
};

// ---------------------------------------------------------------------------
// Slot view-model — one per grid cell. Registered with the data model so RML
// can iterate via data-for. Display fields are denormalized from FRmlItemDef
// when an item is placed in a slot (C++ manages consistency).
// ---------------------------------------------------------------------------

struct FRmlSlotData
{
	Rml::String ItemId;                    // "" = empty slot
	Rml::String Name;
	Rml::String Icon        = "none";      // full decorator value; "none" avoids invalid CSS on empty slots
	Rml::String RarityColor = "transparent"; // hex for data-style-color
	int Qty = 0;                           // shown if > 1
	int Selected = 0;                      // 0=none, 1=selected
	int DragOrigin = 0;                    // 1=source item while dragging (details-only, no yellow highlight)
	int ShowChip = 0;                      // 1 = show corner chip
	int DropInvalid = 0;                   // 1 = current drag cannot be dropped here (temporary red feedback)
	int DropValid = 0;                     // 1 = current drag CAN be dropped here (green pulse on compatible equip slots)
	int Locked = 0;                        // 1 = slot locked (cannot equip or hover)
	Rml::String DetailsRml;                // pre-built details body HTML for data-rml
};

// ---------------------------------------------------------------------------
// Shared inventory helpers — used by both URmlInventory and URmlInventoryWidget
// ---------------------------------------------------------------------------

namespace RmlInventoryUtils
{

// Item database — returned by value so Live Coding can patch the function body
// (static data is NOT patched by Live Coding, only function code).
Rml::Vector<FRmlItemDef> GetItemDatabase();

inline const FRmlItemDef* FindItem(const Rml::Vector<FRmlItemDef>& DB, const Rml::String& ItemId)
{
	for (const FRmlItemDef& Item : DB)
		if (Item.Id == ItemId)
			return &Item;
	return nullptr;
}

inline Rml::String BuildDetailsRml(const FRmlItemDef& Def)
{
	Rml::String R;
	R += "<span class='details-title' style='color: " + Def.RarityColor + ";'>"
	   + Def.Name + "</span>";
	R += "<span class='details-rarity' style='color: " + Def.RarityColor + ";'>"
	   + Def.RarityName + "</span>";
	R += "<span class='details-type'>" + Def.TypeName + "</span>";
	R += "<div class='details-separator'></div>";
	R += "<span class='details-flavor'>\"" + Def.Flavor + "\"</span>";
	R += "<div class='details-separator'></div>";
	R += "<div class='details-stats-row'>";
	for (const Rml::String& Stat : Def.Stats)
		R += "<span class='details-stat'>" + Stat + "</span>";
	R += "</div>";
	return R;
}

inline void PopulateSlot(FRmlSlotData& Slot, const FRmlItemDef& Def,
                          int InQty, int InSelected = 0, int InShowChip = 0)
{
	Slot.ItemId      = Def.Id;
	Slot.Name        = Def.Name;
	Slot.Icon        = Def.Icon;
	Slot.RarityColor = Def.RarityColor;
	Slot.Qty         = InQty;
	Slot.Selected    = InSelected;
	Slot.ShowChip    = InShowChip;
	Slot.DetailsRml  = BuildDetailsRml(Def);
}

inline void ClearSlot(FRmlSlotData& Slot)
{
	Slot = FRmlSlotData{};
}

inline int GetElementAttr(const Rml::Element* El, const char* AttrName)
{
	for (; El; El = El->GetParentNode())
	{
		const Rml::Variant* Attr = El->GetAttribute(AttrName);
		if (Attr)
			return Attr->Get<int>(-1);
	}
	return -1;
}

inline int GetSlotIndex(const Rml::Element* El)  { return GetElementAttr(El, "data-index"); }
inline int GetEquipIndex(const Rml::Element* El) { return GetElementAttr(El, "data-equip"); }

inline constexpr int GEquipMainSlot = 0;
inline constexpr int GEquipSecondarySlot = 1;
inline constexpr int GEquipRangedSlot = 2;
inline constexpr int GEquipMainModSlot = 3;
inline constexpr int GEquipSecondaryModSlot = 4;
inline constexpr int GEquipRangedModSlot = 5;
inline constexpr int GEquipAccessoryStartSlot = 6;
inline constexpr int GNumEquipSlots = 11;  // 0-2 weapons, 3-5 weapon mods, 6-10 accessories

/** Resolve the hovered/clicked/dragged element to its slot data pointer.
    Returns nullptr when the element doesn't map to any known slot. */
inline FRmlSlotData* ResolveTargetSlot(
	Rml::Element* Target,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int& OutGridIdx,
	int& OutEquipIdx)
{
	OutGridIdx  = GetSlotIndex(Target);
	OutEquipIdx = GetEquipIndex(Target);
	const int SlotCount = static_cast<int>(Slots.size());

	if (OutGridIdx >= 0 && OutGridIdx < SlotCount)
		return &Slots[OutGridIdx];
	if (OutEquipIdx >= 0 && OutEquipIdx < GNumEquipSlots)
		return &EquipWeapons[OutEquipIdx];
	return nullptr;
}

/** Check if an item type is allowed in a given equip slot. */
inline bool CanEquip(int EquipSlot, const Rml::String& TypeName)
{
	switch (EquipSlot)
	{
	case GEquipMainSlot:         return TypeName == "Weapon";
	case GEquipSecondarySlot:    return TypeName == "Weapon" || TypeName == "Off Hand";
	case GEquipRangedSlot:       return TypeName == "Ranged";
	case GEquipMainModSlot:      // fallthrough
	case GEquipSecondaryModSlot: return TypeName == "Weapon Mod";
	case GEquipRangedModSlot:    return TypeName == "Ranged Weapon Mod";
	default:                     return EquipSlot >= GEquipAccessoryStartSlot && TypeName == "Accessory";
	}
}

inline constexpr const char* GEquipBindingNames[] = {
	"equip_main", "equip_secondary", "equip_range",
	"equip_mod_main", "equip_mod_secondary", "equip_mod_range",
	"equip_acc_1", "equip_acc_2", "equip_acc_3", "equip_acc_4", "equip_acc_5"
};
static_assert((sizeof(GEquipBindingNames) / sizeof(GEquipBindingNames[0])) == GNumEquipSlots,
	"GEquipBindingNames size must match GNumEquipSlots");

inline int GetChipColorForEquipSlot(int EquipSlot)
{
	return EquipSlot >= GEquipAccessoryStartSlot ? 2 : 1;
}

/** Register the "inventory" data model and bind to the provided members. */
inline Rml::DataModelHandle RegisterDataModel(
	Rml::Context* Ctx,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int& Coins,
	int& Gems,
	int& SlotsUsed,
	int& SlotsTotal)
{
	Rml::DataModelHandle Handle;
	if (Rml::DataModelConstructor C = Ctx->CreateDataModel("inventory"))
	{
		if (auto H = C.RegisterStruct<FRmlSlotData>())
		{
			H.RegisterMember("item_id",      &FRmlSlotData::ItemId);
			H.RegisterMember("name",          &FRmlSlotData::Name);
			H.RegisterMember("icon",          &FRmlSlotData::Icon);
			H.RegisterMember("rarity_color",  &FRmlSlotData::RarityColor);
			H.RegisterMember("qty",           &FRmlSlotData::Qty);
			H.RegisterMember("selected",      &FRmlSlotData::Selected);
			H.RegisterMember("drag_origin",   &FRmlSlotData::DragOrigin);
			H.RegisterMember("show_chip",     &FRmlSlotData::ShowChip);
			H.RegisterMember("drop_invalid",  &FRmlSlotData::DropInvalid);
			H.RegisterMember("drop_valid",    &FRmlSlotData::DropValid);
			H.RegisterMember("locked",        &FRmlSlotData::Locked);
			H.RegisterMember("details_rml",   &FRmlSlotData::DetailsRml);
		}

		C.RegisterArray<Rml::Vector<FRmlSlotData>>();

		C.Bind("slots", &Slots);
		for (int i = 0; i < GNumEquipSlots; ++i)
			C.Bind(GEquipBindingNames[i], &EquipWeapons[i]);
		C.Bind("coins", &Coins);
		C.Bind("gems",  &Gems);
		C.Bind("slots_used",  &SlotsUsed);
		C.Bind("slots_total", &SlotsTotal);

		Handle = C.GetModelHandle();
	}
	return Handle;
}

/** Populate 50 demo slots (11 items + 39 empty) and set currency. */
inline void PopulateDemoInventory(
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int& Coins,
	int& Gems,
	int& SlotsUsed,
	int& SlotsTotal,
	Rml::DataModelHandle& Handle)
{
	Slots.resize(50);

	Rml::Vector<FRmlItemDef> DB = GetItemDatabase();
	PopulateSlot(Slots[0], DB[0], 1);           // Void Sword
	PopulateSlot(Slots[1], DB[1], 1);           // Iron Helm
	PopulateSlot(Slots[2], DB[2], 1);           // Arcane Orb
	PopulateSlot(Slots[3], DB[3], 1);           // Ancient Shield
	PopulateSlot(Slots[4], DB[4], 13);          // Red Potion x13
	PopulateSlot(Slots[5], DB[5], 1);           // Dark Crystal
	PopulateSlot(Slots[6], DB[6], 1);           // Wood Bow-Ie
	PopulateSlot(Slots[7], DB[7], 1);           // Ancient Amulet
	PopulateSlot(Slots[8], DB[8], 1);           // Void Core Mod
	PopulateSlot(Slots[9], DB[9], 1);           // Falcon Scope Mod
	PopulateSlot(Slots[10], DB[10], 24);        // Crimson Cloth x24
	// Slots[11..49] remain default (empty)

	// Lock ACC 4-5 (equip indices 9-10)
	EquipWeapons[9].Locked = 1;
	EquipWeapons[10].Locked = 1;

	Coins = 1526;
	Gems  = 26;
	SlotsTotal = static_cast<int>(Slots.size());
	SlotsUsed = 0;
	for (const FRmlSlotData& Slot : Slots)
		if (!Slot.ItemId.empty())
			++SlotsUsed;

	Handle.DirtyVariable("slots");
	Handle.DirtyVariable("coins");
	Handle.DirtyVariable("gems");
	Handle.DirtyVariable("slots_used");
	Handle.DirtyVariable("slots_total");
}

/** Clear the Selected flag on all grid slots and equip slots. */
inline void ClearAllSelections(Rml::Vector<FRmlSlotData>& Slots, FRmlSlotData* EquipWeapons)
{
	for (auto& S : Slots)
	{
		S.Selected = 0;
		S.DragOrigin = 0;
	}
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		EquipWeapons[i].Selected = 0;
		EquipWeapons[i].DragOrigin = 0;
	}
}

inline void MarkSelectedAsDragOrigin(Rml::Vector<FRmlSlotData>& Slots, FRmlSlotData* EquipWeapons)
{
	for (auto& S : Slots)
	{
		if (S.Selected == 1)
			S.DragOrigin = 1;
	}
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipWeapons[i].Selected == 1)
			EquipWeapons[i].DragOrigin = 1;
	}
}

/** Dirty all equip variables that changed. */
inline void DirtyAllEquip(Rml::DataModelHandle& Handle)
{
	for (int i = 0; i < GNumEquipSlots; ++i)
		Handle.DirtyVariable(GEquipBindingNames[i]);
}

/** Refresh display fields (Icon, Name, RarityColor, DetailsRml) from GItemDatabase
    for all non-empty slots. Defined in RmlInventoryData.cpp for Live Coding support. */
void RefreshSlotsFromDatabase(
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	Rml::DataModelHandle& Handle);

/** Grid → Grid: swap slots and patch any equip-source refs pointing at either index. */
inline void HandleDragDrop_GridToGrid(
	int SrcGrid, int DstGrid,
	Rml::Vector<FRmlSlotData>& Slots,
	int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	if (SrcGrid == DstGrid) return;
	const int SlotCount = static_cast<int>(Slots.size());
	if (SrcGrid >= SlotCount || DstGrid >= SlotCount) return;

	std::swap(Slots[SrcGrid], Slots[DstGrid]);
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipSourceIndex[i] == SrcGrid)
			EquipSourceIndex[i] = DstGrid;
		else if (EquipSourceIndex[i] == DstGrid)
			EquipSourceIndex[i] = SrcGrid;
	}
	Handle.DirtyVariable("slots");
}

/** Grid → Equip: copy item into equip slot and mark the grid slot with a chip. */
inline void HandleDragDrop_GridToEquip(
	int SrcGrid, int DstEquip,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	const int SlotCount = static_cast<int>(Slots.size());
	if (SrcGrid >= SlotCount) return;
	if (Slots[SrcGrid].ItemId.empty()) return;

	if (EquipWeapons[DstEquip].Locked) return;

	// Type validation
	Rml::Vector<FRmlItemDef> DB = GetItemDatabase();
	const FRmlItemDef* Def = FindItem(DB, Slots[SrcGrid].ItemId);
	if (!Def || !CanEquip(DstEquip, Def->TypeName)) return;

	// If this grid item is already equipped elsewhere, clear that equip slot
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipSourceIndex[i] == SrcGrid)
		{
			ClearSlot(EquipWeapons[i]);
			EquipSourceIndex[i] = -1;
			Handle.DirtyVariable(GEquipBindingNames[i]);
			break;
		}
	}

	// If target equip was occupied, clear old chip in grid
	if (EquipSourceIndex[DstEquip] >= 0 && EquipSourceIndex[DstEquip] < SlotCount)
		Slots[EquipSourceIndex[DstEquip]].ShowChip = 0;

	// Copy item to equip display
	EquipWeapons[DstEquip] = Slots[SrcGrid];
	EquipWeapons[DstEquip].ShowChip = 0;
	EquipSourceIndex[DstEquip] = SrcGrid;
	Slots[SrcGrid].ShowChip = GetChipColorForEquipSlot(DstEquip);

	Handle.DirtyVariable("slots");
	Handle.DirtyVariable(GEquipBindingNames[DstEquip]);
}

/** Equip → Grid: unequip into an empty/ghost slot, or swap with an occupied slot. */
inline void HandleDragDrop_EquipToGrid(
	int SrcEquip, int DstGrid,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	const int SlotCount = static_cast<int>(Slots.size());
	if (DstGrid >= SlotCount) return;
	const int OldIdx = EquipSourceIndex[SrcEquip];
	if (OldIdx < 0 || OldIdx >= SlotCount) return;

	if (DstGrid == OldIdx || Slots[DstGrid].ItemId.empty())
	{
		// Drop on ghost slot or empty slot: unequip
		if (OldIdx != DstGrid)
		{
			Slots[DstGrid] = Slots[OldIdx];
			ClearSlot(Slots[OldIdx]);
		}
		Slots[DstGrid].ShowChip = 0;
		ClearSlot(EquipWeapons[SrcEquip]);
		EquipSourceIndex[SrcEquip] = -1;
	}
	else
	{
		Rml::Vector<FRmlItemDef> DB = GetItemDatabase();
		// Occupied target is allowed only if displaced item can stay equipped in SrcEquip.
		const FRmlItemDef* DstDef = FindItem(DB, Slots[DstGrid].ItemId);
		if (!DstDef || !CanEquip(SrcEquip, DstDef->TypeName))
			return;

		// Occupied target: swap grid slots, re-equip the displaced item
		std::swap(Slots[OldIdx], Slots[DstGrid]);

		// Update other equip refs that pointed at swapped positions
		for (int i = 0; i < GNumEquipSlots; ++i)
		{
			if (i == SrcEquip) continue;
			if (EquipSourceIndex[i] == DstGrid)
				EquipSourceIndex[i] = OldIdx;
			else if (EquipSourceIndex[i] == OldIdx)
				EquipSourceIndex[i] = DstGrid;
		}

		// OldIdx now holds the displaced item — re-equip it
		EquipWeapons[SrcEquip] = Slots[OldIdx];
		EquipWeapons[SrcEquip].ShowChip = 0;
		EquipSourceIndex[SrcEquip] = OldIdx;
		Slots[OldIdx].ShowChip = GetChipColorForEquipSlot(SrcEquip);
		Slots[DstGrid].ShowChip = 0;
	}

	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

/** Equip → Equip: swap equip displays, refs, and chip colors on the source grid items. */
inline void HandleDragDrop_EquipToEquip(
	int SrcEquip, int DstEquip,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	if (SrcEquip == DstEquip) return;
	if (EquipWeapons[DstEquip].Locked) return;
	const int SlotCount = static_cast<int>(Slots.size());

	// Validate both items fit in their new slots
	Rml::Vector<FRmlItemDef> DB = GetItemDatabase();
	if (!EquipWeapons[SrcEquip].ItemId.empty())
	{
		const FRmlItemDef* SrcDef = FindItem(DB, EquipWeapons[SrcEquip].ItemId);
		if (!SrcDef || !CanEquip(DstEquip, SrcDef->TypeName)) return;
	}
	if (!EquipWeapons[DstEquip].ItemId.empty())
	{
		const FRmlItemDef* DstDef = FindItem(DB, EquipWeapons[DstEquip].ItemId);
		if (!DstDef || !CanEquip(SrcEquip, DstDef->TypeName)) return;
	}

	std::swap(EquipWeapons[SrcEquip], EquipWeapons[DstEquip]);
	std::swap(EquipSourceIndex[SrcEquip], EquipSourceIndex[DstEquip]);

	// Update chip color on grid source items (weapon=1, accessory=2)
	for (int i : {SrcEquip, DstEquip})
	{
		const int Src = EquipSourceIndex[i];
		if (Src >= 0 && Src < SlotCount)
			Slots[Src].ShowChip = GetChipColorForEquipSlot(i);
	}
	Handle.DirtyVariable("slots");
	Handle.DirtyVariable(GEquipBindingNames[SrcEquip]);
	Handle.DirtyVariable(GEquipBindingNames[DstEquip]);
}

/**
 * Handle a dragdrop event.
 * Equip = copy+chip model: equipped items stay in the grid with a chip.
 * EquipSourceIndex tracks which grid index each equip slot references (-1 = empty).
 */
inline void HandleDragDrop(
	const Rml::Event& event,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	Rml::Element* DragElement = static_cast<Rml::Element*>(
		event.GetParameter<void*>("drag_element", nullptr));
	Rml::Element* DropTarget = event.GetTargetElement();

	if (!DragElement || !DropTarget)
		return;

	const int SrcGrid  = GetSlotIndex(DragElement);
	const int SrcEquip = GetEquipIndex(DragElement);
	const int DstGrid  = GetSlotIndex(DropTarget);
	const int DstEquip = GetEquipIndex(DropTarget);

	if (SrcGrid >= 0 && DstGrid >= 0)
		HandleDragDrop_GridToGrid(SrcGrid, DstGrid, Slots, EquipSourceIndex, Handle);
	else if (SrcGrid >= 0 && DstEquip >= 0 && DstEquip < GNumEquipSlots)
		HandleDragDrop_GridToEquip(SrcGrid, DstEquip, Slots, EquipWeapons, EquipSourceIndex, Handle);
	else if (SrcEquip >= 0 && SrcEquip < GNumEquipSlots && DstGrid >= 0)
		HandleDragDrop_EquipToGrid(SrcEquip, DstGrid, Slots, EquipWeapons, EquipSourceIndex, Handle);
	else if (SrcEquip >= 0 && SrcEquip < GNumEquipSlots && DstEquip >= 0 && DstEquip < GNumEquipSlots)
		HandleDragDrop_EquipToEquip(SrcEquip, DstEquip, Slots, EquipWeapons, EquipSourceIndex, Handle);
}

/** Sync selection between grid and equip: if the selected item is equipped, also select its counterpart. */
inline void SyncSelection(int GridIdx, int EquipIdx,
	Rml::Vector<FRmlSlotData>& Slots, FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex)
{
	const int SlotCount = static_cast<int>(Slots.size());
	if (GridIdx >= 0 && GridIdx < SlotCount)
	{
		// Selected a grid item — find if it's equipped somewhere
		for (int i = 0; i < GNumEquipSlots; ++i)
			if (EquipSourceIndex[i] == GridIdx)
				EquipWeapons[i].Selected = 1;
	}
	else if (EquipIdx >= 0 && EquipIdx < GNumEquipSlots)
	{
		// Selected an equip item — highlight the source grid slot
		const int Src = EquipSourceIndex[EquipIdx];
		if (Src >= 0 && Src < SlotCount)
			Slots[Src].Selected = 1;
	}
}

/** Handle a dragstart event - select the dragged item (non-toggle). */
inline void HandleDragStart(
	const Rml::Event& event,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	Rml::Element* Target = event.GetTargetElement();
	if (!Target)
		return;

	int GridIdx, EquipIdx;
	FRmlSlotData* Dragged = ResolveTargetSlot(Target, Slots, EquipWeapons, GridIdx, EquipIdx);
	if (!Dragged || Dragged->ItemId.empty())
		return;

	ClearAllSelections(Slots, EquipWeapons);
	Dragged->Selected = 1;
	SyncSelection(GridIdx, EquipIdx, Slots, EquipWeapons, EquipSourceIndex);
	MarkSelectedAsDragOrigin(Slots, EquipWeapons);

	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

/** Handle mouseover on items - hovered item becomes selected and persists. */
inline void HandleHover(
	const Rml::Event& event,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	Rml::Element* Target = event.GetTargetElement();
	if (!Target)
		return;

	int GridIdx, EquipIdx;
	FRmlSlotData* Hovered = ResolveTargetSlot(Target, Slots, EquipWeapons, GridIdx, EquipIdx);

	// Locked slots are completely inert — ignore hover.
	if (Hovered && Hovered->Locked)
		return;

	// Hovering any empty slot (grid, weapon, or accessory) clears selection.
	if (Hovered && Hovered->ItemId.empty())
	{
		ClearAllSelections(Slots, EquipWeapons);
		Handle.DirtyVariable("slots");
		DirtyAllEquip(Handle);
		return;
	}

	// Keep current selection when hovering non-slot areas (background, panels, etc.).
	if (!Hovered)
		return;

	// Avoid unnecessary churn when bubbling through children of same item.
	if (Hovered->Selected == 1)
		return;

	ClearAllSelections(Slots, EquipWeapons);
	Hovered->Selected = 1;
	SyncSelection(GridIdx, EquipIdx, Slots, EquipWeapons, EquipSourceIndex);

	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

/** Handle click - clicking empty/non-item clears selection, items do not toggle selection. */
inline void HandleClick(
	const Rml::Event& event,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	Rml::Element* Target = event.GetTargetElement();
	if (!Target)
		return;

	int GridIdx, EquipIdx;
	FRmlSlotData* Clicked = ResolveTargetSlot(Target, Slots, EquipWeapons, GridIdx, EquipIdx);

	// Selection is hover-driven. Click on an item must not toggle state.
	if (Clicked && !Clicked->ItemId.empty())
		return;

	ClearAllSelections(Slots, EquipWeapons);
	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

} // namespace RmlInventoryUtils
