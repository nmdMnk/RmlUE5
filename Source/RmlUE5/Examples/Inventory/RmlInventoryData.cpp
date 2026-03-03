#include "RmlInventoryData.h"

namespace RmlInventoryUtils
{

// Returns the item database by value. Live Coding patches function BODIES
// (including the string literals below) but NEVER patches static/global data.
// Returning by value means no file-scope statics — safe across patch modules.
Rml::Vector<FRmlItemDef> GetItemDatabase()
{
	return {
		{
			"void_sword", "Void Sword",
			"image(/Game/Texture/high_scores_alien_1.high_scores_alien_1)",
			"#a335eeff", "Epic", "Weapon",
			"A blade forged in the void between stars",
			{"ATK  +45", "CRIT +15%"},
			false, 1
		},
		{
			"iron_helm", "Iron Helm",
			"image(/Game/Texture/high_scores_alien_2.high_scores_alien_2)",
			"#888888ff", "Common", "Material",
			"A battered helm, useful only for scrap metal",
			{"Crafting material"},
			true, 99
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
			false, 1
		},
		{
			"wood_bow", "Wood Bow-Ie",
			"image(/Game/Texture/high_scores_alien_3.high_scores_alien_3)",
			"#1eff00ff", "Uncommon", "Ranged",
			"A simple but reliable hunting bow",
			{"ATK  +18", "RNG  +12"},
			false, 1
		},
		{
			"ancient_amulet", "Ancient Amulet",
			"image(/Game/Texture/high_scores_alien_2.high_scores_alien_2)",
			"#1eff00ff", "Uncommon", "Accessory",
			"Incomprehensible hieroglyphs carved in bronze",
			{"INT  +22", "MP   +40"},
			false, 1
		},
		{
			"void_core_mod", "Runebound Sigil",
			"image(/Game/Texture/high_scores_defender.high_scores_defender)",
			"#0070ddff", "Rare", "Weapon Mod",
			"An engraved sigil that binds steel to forgotten runes",
			{"ATK +11", "ARC +14"},
			false, 1
		},
		{
			"falcon_scope_mod", "Starseeker Lens",
			"image(/Game/Texture/high_scores_alien_3.high_scores_alien_3)",
			"#a335eeff", "Epic", "Ranged Weapon Mod",
			"A crystal lens that guides arrows toward their omen",
			{"RNG +18", "CRIT +8%"},
			false, 1
		},
		{
			"crimson_cloth", "Crimson Cloth",
			"image(/Game/Texture/high_scores_defender.high_scores_defender)",
			"#888888ff", "Common", "Material",
			"A finely woven piece of crimson fabric",
			{"Crafting material"},
			true, 99
		},
	};
}

void RefreshSlotsFromDatabase(
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	Rml::DataModelHandle& Handle)
{
	// Build fresh DB from patched function code (no statics involved).
	Rml::Vector<FRmlItemDef> DB = GetItemDatabase();

	for (FRmlSlotData& Slot : Slots)
	{
		if (Slot.ItemId.empty())
			continue;
		const FRmlItemDef* Def = FindItem(DB, Slot.ItemId);
		if (!Def)
			continue;
		Slot.Name        = Def->Name;
		Slot.Icon        = Def->Icon;
		Slot.RarityColor = Def->RarityColor;
		Slot.DetailsRml  = BuildDetailsRml(*Def);
	}
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipWeapons[i].ItemId.empty())
			continue;
		const FRmlItemDef* Def = FindItem(DB, EquipWeapons[i].ItemId);
		if (!Def)
			continue;
		EquipWeapons[i].Name        = Def->Name;
		EquipWeapons[i].Icon        = Def->Icon;
		EquipWeapons[i].RarityColor = Def->RarityColor;
		EquipWeapons[i].DetailsRml  = BuildDetailsRml(*Def);
	}
	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

} // namespace RmlInventoryUtils
