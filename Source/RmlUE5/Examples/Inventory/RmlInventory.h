#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlInventoryData.h"
#include "RmlInventory.generated.h"

struct FRmlCachedSlotRect { float Left = 0, Top = 0, Width = 0, Height = 0; };

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
	/** Cache grid slot rects and compute click offset for drag-clone positioning. */
	void InitDragGeometry(const Rml::Event& event);
	/** Clear all drag feedback (invalid markers, equip highlights, origin markers) and reset state. */
	void CleanupDragState();
	/** Reset all drag-tracking state to its default (no drag in progress). */
	void ResetDragState();
	Rml::Vector<FRmlSlotData> Slots;
	FRmlSlotData EquipWeapons[RmlInventoryUtils::GNumEquipSlots];      // 0-2 weapons, 3-5 weapon mods, 6-10 accessories
	int EquipSourceIndex[RmlInventoryUtils::GNumEquipSlots] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	int Coins = 0;
	int Gems = 0;
	int SlotsUsed = 0;
	int SlotsTotal = 0;
	int InvalidDropGridIndex = -1;
	int InvalidDropEquipIndex = -1;
	bool bDragInProgress = false;
	int DragSourceGridIndex = -1;
	int CurrentDragDetailsGridIndex = -1;
	double LastValidDragHoverTimeSeconds = 0.0;
	float DragClickOffsetX = 0.0f;
	float DragClickOffsetY = 0.0f;
	float DragSlotWidth = 0.0f;
	float DragSlotHeight = 0.0f;
	Rml::Vector<FRmlCachedSlotRect> CachedGridSlotRects;
	bool bDragCloneAdjusted = false;
	Rml::DataModelHandle InventoryHandle;
	bool bRootListenerBound = false;
};
