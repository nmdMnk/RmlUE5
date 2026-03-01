#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlInventoryData.h"
#include "RmlInventory.generated.h"

UCLASS()
class URmlInventory : public URmlDocument
{
	GENERATED_BODY()
public:
	/**
	 * Register the "inventory" data model and populate demo slots.
	 * Must be called BEFORE the document loads (data-for needs data).
	 */
	void CreateDataModel(Rml::Context* InContext);

	/**
	 * Attach drag-drop event listener on #inv-grid in the given document.
	 * Called automatically by OnInit() for the GameModeBase path.
	 * Call manually after document load for the URmlUiWidget path.
	 */
	void BindToDocument(Rml::ElementDocument* Doc);

	/** Clear all grid + equip selections and dirty the data model. */
	void ClearSelection();

protected:
	virtual void OnInit() override;
	virtual void ProcessEvent(Rml::Event& event) override;

private:
	Rml::Vector<FRmlSlotData> Slots;
	FRmlSlotData EquipWeapons[RmlInventoryUtils::GNumEquipSlots];      // 0-2 weapons, 3-7 armor
	int EquipSourceIndex[RmlInventoryUtils::GNumEquipSlots] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int Coins = 0;
	int Gems = 0;
	Rml::DataModelHandle InventoryHandle;
	bool bRootListenerBound = false;
};
