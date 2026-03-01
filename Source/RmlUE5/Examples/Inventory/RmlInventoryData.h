#pragma once

#include "RmlUi/Core/Types.h"
#include "RmlUi/Core/DataModelHandle.h"
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
	int Selected = 0;                      // 0=none, 1=yellow, 2=cyan
	int ShowChip = 0;                      // 1 = show corner chip
	Rml::String TooltipRml;                // pre-built HTML for data-rml
};

// ---------------------------------------------------------------------------
// Shared inventory helpers — used by both URmlInventory and URmlInventoryWidget
// ---------------------------------------------------------------------------

namespace RmlInventoryUtils
{

// Static item database — matches the 9 items from the original mockup.
inline const FRmlItemDef GItemDatabase[] =
{
	{
		"void_sword", "Void Sword",
		"image(/Game/Texture/present.present)",
		"#a335eeff", "Epic", "Weapon",
		"A blade forged in the void between stars",
		{"ATK  +45", "CRIT +15%"},
		false, 1
	},
	{
		"iron_helm", "Iron Helm",
		"image(/Game/Texture/alien_small.alien_small)",
		"#888888ff", "Common", "Head",
		"Standard military-grade helmet",
		{"DEF +12", "HP  +5"},
		false, 1
	},
	{
		"arcane_orb", "Arcane Orb",
		"image(/Game/Texture/high_scores_alien_1.high_scores_alien_1)",
		"#ff8000ff", "Legendary", "Accessory",
		"Pulses with inexhaustible cosmic energy",
		{"MANA  +80", "SPELL +35%"},
		false, 1
	},
	{
		"ancient_shield", "Ancient Shield",
		"image(/Game/Texture/high_scores_defender.high_scores_defender)",
		"#0070ddff", "Rare", "Off Hand",
		"Belonged to a fallen guardian",
		{"DEF   +28", "BLOCK +20%"},
		false, 1
	},
	{
		"red_potion", "Red Potion",
		"image(/Game/Texture/high_scores_alien_2.high_scores_alien_2)",
		"#888888ff", "Common", "Consumable",
		"Ruby liquid that restores the body",
		{"HP +150  (use)"},
		true, 99
	},
	{
		"dark_crystal", "Dark Crystal",
		"image(/Game/Texture/high_scores_alien_3.high_scores_alien_3)",
		"#a335eeff", "Epic", "Accessory",
		"Fragment of a dead star",
		{"ALL  +15", "DARK +25%"},
		true, 99
	},
	{
		"wind_boots", "Wind Boots",
		"image(/Game/Texture/present.present)",
		"#0070ddff", "Rare", "Feet",
		"Light as the evening air",
		{"SPD  +30", "EVA  +18%"},
		true, 99
	},
	{
		"ancient_amulet", "Ancient Amulet",
		"image(/Game/Texture/alien_small.alien_small)",
		"#1eff00ff", "Uncommon", "Neck",
		"Incomprehensible hieroglyphs carved in bronze",
		{"INT  +22", "MP   +40"},
		true, 99
	},
	{
		"crimson_cloth", "Crimson Cloth",
		"image(/Game/Texture/high_scores_defender.high_scores_defender)",
		"#888888ff", "Common", "Material",
		"A finely woven piece of crimson fabric",
		{"Crafting material"},
		false, 1
	},
};

inline constexpr int GItemDatabaseSize = sizeof(GItemDatabase) / sizeof(GItemDatabase[0]);

inline const FRmlItemDef* FindItem(const Rml::String& ItemId)
{
	for (int i = 0; i < GItemDatabaseSize; ++i)
		if (GItemDatabase[i].Id == ItemId)
			return &GItemDatabase[i];
	return nullptr;
}

inline Rml::String BuildTooltipRml(const FRmlItemDef& Def)
{
	Rml::String R;
	R += "<span class='tt-name' style='color: " + Def.RarityColor + ";'>"
	   + Def.Name + "</span>";
	R += "<span class='tt-type' style='color: " + Def.RarityColor + ";'>"
	   + Def.TypeName + " \xe2\x80\x94 " + Def.RarityName + "</span>";
	R += "<div class='tt-sep'></div>";
	for (const Rml::String& Stat : Def.Stats)
		R += "<span class='tt-stat'>" + Stat + "</span>";
	R += "<div class='tt-sep'></div>";
	R += "<span class='tt-flavor'>\"" + Def.Flavor + "\"</span>";
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
	Slot.TooltipRml  = BuildTooltipRml(Def);
}

inline void ClearSlot(FRmlSlotData& Slot)
{
	Slot = FRmlSlotData{};
}

inline int GetSlotIndex(const Rml::Element* El)
{
	for (; El; El = El->GetParentNode())
	{
		const Rml::Variant* Attr = El->GetAttribute("data-index");
		if (Attr)
			return Attr->Get<int>(-1);
	}
	return -1;
}

inline int GetEquipIndex(const Rml::Element* El)
{
	for (; El; El = El->GetParentNode())
	{
		const Rml::Variant* Attr = El->GetAttribute("data-equip");
		if (Attr)
			return Attr->Get<int>(-1);
	}
	return -1;
}

inline constexpr int GNumEquipSlots = 8;  // 0-2 weapons, 3-7 armor

inline constexpr const char* GEquipDirtyNames[] = {
	"equip_main", "equip_secondary", "equip_range",
	"equip_head", "equip_chest", "equip_hands", "equip_legs", "equip_feet"
};

/** Register the "inventory" data model and bind to the provided members. */
inline Rml::DataModelHandle RegisterDataModel(
	Rml::Context* Ctx,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	int& Coins,
	int& Gems)
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
			H.RegisterMember("show_chip",     &FRmlSlotData::ShowChip);
			H.RegisterMember("tooltip_rml",   &FRmlSlotData::TooltipRml);
		}

		C.RegisterArray<Rml::Vector<FRmlSlotData>>();

		C.Bind("slots", &Slots);
		C.Bind("equip_main",      &EquipWeapons[0]);
		C.Bind("equip_secondary", &EquipWeapons[1]);
		C.Bind("equip_range",     &EquipWeapons[2]);
		C.Bind("equip_head",      &EquipWeapons[3]);
		C.Bind("equip_chest",     &EquipWeapons[4]);
		C.Bind("equip_hands",     &EquipWeapons[5]);
		C.Bind("equip_legs",      &EquipWeapons[6]);
		C.Bind("equip_feet",      &EquipWeapons[7]);
		C.Bind("coins", &Coins);
		C.Bind("gems",  &Gems);

		Handle = C.GetModelHandle();
	}
	return Handle;
}

/** Populate 50 demo slots (9 items + 41 empty) and set currency. */
inline void PopulateDemoInventory(
	Rml::Vector<FRmlSlotData>& Slots,
	int& Coins,
	int& Gems,
	Rml::DataModelHandle& Handle)
{
	Slots.resize(50);

	PopulateSlot(Slots[0], GItemDatabase[0], 1);           // Void Sword
	PopulateSlot(Slots[1], GItemDatabase[1], 1);           // Iron Helm
	PopulateSlot(Slots[2], GItemDatabase[2], 1);           // Arcane Orb
	PopulateSlot(Slots[3], GItemDatabase[3], 1);           // Ancient Shield
	PopulateSlot(Slots[4], GItemDatabase[4], 13);          // Red Potion x13
	PopulateSlot(Slots[5], GItemDatabase[5], 2);           // Dark Crystal x2
	PopulateSlot(Slots[6], GItemDatabase[6], 25);          // Wind Boots x25
	PopulateSlot(Slots[7], GItemDatabase[7], 8);           // Ancient Amulet x8
	PopulateSlot(Slots[8], GItemDatabase[8], 1);           // Crimson Cloth
	// Slots[9..49] remain default (empty)

	Coins = 1526;
	Gems  = 26;

	Handle.DirtyVariable("slots");
	Handle.DirtyVariable("coins");
	Handle.DirtyVariable("gems");
}

/** Dirty all equip variables that changed. */
inline void DirtyAllEquip(Rml::DataModelHandle& Handle)
{
	for (int i = 0; i < GNumEquipSlots; ++i)
		Handle.DirtyVariable(GEquipDirtyNames[i]);
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

	const int SlotCount = static_cast<int>(Slots.size());

	if (SrcGrid >= 0 && DstGrid >= 0)
	{
		// Grid → Grid: swap, update equip refs that pointed at either index
		if (SrcGrid == DstGrid) return;
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
	else if (SrcGrid >= 0 && DstEquip >= 0 && DstEquip < GNumEquipSlots)
	{
		// Grid → Equip: copy item to equip, mark grid slot with chip
		if (SrcGrid >= SlotCount) return;
		if (Slots[SrcGrid].ItemId.empty()) return;

		// If this grid item is already equipped elsewhere, clear that equip slot
		for (int i = 0; i < GNumEquipSlots; ++i)
		{
			if (EquipSourceIndex[i] == SrcGrid)
			{
				ClearSlot(EquipWeapons[i]);
				EquipSourceIndex[i] = -1;
				Handle.DirtyVariable(GEquipDirtyNames[i]);
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
		Slots[SrcGrid].ShowChip = (DstEquip >= 3) ? 2 : 1;

		Handle.DirtyVariable("slots");
		Handle.DirtyVariable(GEquipDirtyNames[DstEquip]);
	}
	else if (SrcEquip >= 0 && SrcEquip < GNumEquipSlots && DstGrid >= 0)
	{
		// Equip → Grid: unequip (move to target) or swap
		if (DstGrid >= SlotCount) return;
		const int OldIdx = EquipSourceIndex[SrcEquip];
		if (OldIdx < 0 || OldIdx >= SlotCount) return;

		if (Slots[DstGrid].ItemId.empty())
		{
			// Empty target: move item from ghost to target, clear equip
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

			// OldIdx now has the displaced item — equip it
			EquipWeapons[SrcEquip] = Slots[OldIdx];
			EquipWeapons[SrcEquip].ShowChip = 0;
			EquipSourceIndex[SrcEquip] = OldIdx;
			Slots[OldIdx].ShowChip = (SrcEquip >= 3) ? 2 : 1;
			Slots[DstGrid].ShowChip = 0;
		}

		Handle.DirtyVariable("slots");
		DirtyAllEquip(Handle);
	}
	else if (SrcEquip >= 0 && SrcEquip < GNumEquipSlots && DstEquip >= 0 && DstEquip < GNumEquipSlots)
	{
		// Equip → Equip: swap displays and refs, update chip colors
		if (SrcEquip == DstEquip) return;
		std::swap(EquipWeapons[SrcEquip], EquipWeapons[DstEquip]);
		std::swap(EquipSourceIndex[SrcEquip], EquipSourceIndex[DstEquip]);
		// Update chip color on grid source items (weapon=1, armor=2)
		for (int i : {SrcEquip, DstEquip})
		{
			const int Src = EquipSourceIndex[i];
			if (Src >= 0 && Src < SlotCount)
				Slots[Src].ShowChip = (i >= 3) ? 2 : 1;
		}
		Handle.DirtyVariable("slots");
		Handle.DirtyVariable(GEquipDirtyNames[SrcEquip]);
		Handle.DirtyVariable(GEquipDirtyNames[DstEquip]);
	}
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

/** Handle a dragstart event — select the dragged item (non-toggle). */
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

	const int GridIdx  = GetSlotIndex(Target);
	const int EquipIdx = GetEquipIndex(Target);
	const int SlotCount = static_cast<int>(Slots.size());

	FRmlSlotData* Dragged = nullptr;
	if (GridIdx >= 0 && GridIdx < SlotCount)
		Dragged = &Slots[GridIdx];
	else if (EquipIdx >= 0 && EquipIdx < GNumEquipSlots)
		Dragged = &EquipWeapons[EquipIdx];

	if (!Dragged || Dragged->ItemId.empty())
		return;

	// Clear all selections
	for (auto& S : Slots)
		S.Selected = 0;
	for (int i = 0; i < GNumEquipSlots; ++i)
		EquipWeapons[i].Selected = 0;

	Dragged->Selected = 1;
	SyncSelection(GridIdx, EquipIdx, Slots, EquipWeapons, EquipSourceIndex);

	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

/** Handle a click event — toggle yellow selection on the clicked item. */
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

	const int GridIdx  = GetSlotIndex(Target);
	const int EquipIdx = GetEquipIndex(Target);
	const int SlotCount = static_cast<int>(Slots.size());

	FRmlSlotData* Clicked = nullptr;
	if (GridIdx >= 0 && GridIdx < SlotCount)
		Clicked = &Slots[GridIdx];
	else if (EquipIdx >= 0 && EquipIdx < GNumEquipSlots)
		Clicked = &EquipWeapons[EquipIdx];

	const bool WasSelected = Clicked && (Clicked->Selected == 1);

	// Clear all selections
	for (auto& S : Slots)
		S.Selected = 0;
	for (int i = 0; i < GNumEquipSlots; ++i)
		EquipWeapons[i].Selected = 0;

	if (!WasSelected && Clicked && !Clicked->ItemId.empty())
	{
		Clicked->Selected = 1;
		SyncSelection(GridIdx, EquipIdx, Slots, EquipWeapons, EquipSourceIndex);
	}

	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

} // namespace RmlInventoryUtils
