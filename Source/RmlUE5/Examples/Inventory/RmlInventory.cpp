#include "RmlInventory.h"

using namespace RmlInventoryUtils;

// Delay (seconds) after leaving a valid hover slot before the details panel
// falls back to showing the drag source item. Shared by Dragover and Drag handlers.
static constexpr double KDragHoverFallbackDelaySeconds = 0.10;

static void ClearInvalidDropFeedback(
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	Rml::DataModelHandle& Handle,
	int& InOutGridIndex,
	int& InOutEquipIndex)
{
	const int SlotCount = static_cast<int>(Slots.size());

	bool bSlotsChanged = false;
	if (InOutGridIndex >= 0 && InOutGridIndex < SlotCount && Slots[InOutGridIndex].DropInvalid == 1)
	{
		Slots[InOutGridIndex].DropInvalid = 0;
		bSlotsChanged = true;
	}

	if (bSlotsChanged)
		Handle.DirtyVariable("slots");

	if (InOutEquipIndex >= 0 && InOutEquipIndex < GNumEquipSlots)
	{
		FRmlSlotData& EquipSlot = EquipWeapons[InOutEquipIndex];
		if (EquipSlot.DropInvalid == 1)
		{
			EquipSlot.DropInvalid = 0;
			Handle.DirtyVariable(GEquipBindingNames[InOutEquipIndex]);
		}
	}

	InOutGridIndex = -1;
	InOutEquipIndex = -1;
}

static void SetCompatibleEquipHighlight(
	const Rml::String& TypeName,
	FRmlSlotData* EquipWeapons,
	Rml::DataModelHandle& Handle)
{
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipWeapons[i].Locked)
			continue;
		if (CanEquip(i, TypeName) && EquipWeapons[i].DropValid != 1)
		{
			EquipWeapons[i].DropValid = 1;
			Handle.DirtyVariable(GEquipBindingNames[i]);
		}
	}
}

static void ClearCompatibleEquipHighlight(
	FRmlSlotData* EquipWeapons,
	Rml::DataModelHandle& Handle)
{
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipWeapons[i].DropValid != 0)
		{
			EquipWeapons[i].DropValid = 0;
			Handle.DirtyVariable(GEquipBindingNames[i]);
		}
	}
}

static bool IsDropValidForTarget(
	int SrcGrid,
	int SrcEquip,
	int DstGrid,
	int DstEquip,
	const Rml::Vector<FRmlSlotData>& Slots,
	const FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex)
{
	const int SlotCount = static_cast<int>(Slots.size());
	Rml::Vector<FRmlItemDef> DB = GetItemDatabase();
	const bool bSrcGridValid = SrcGrid >= 0 && SrcGrid < SlotCount;
	const bool bSrcEquipValid = SrcEquip >= 0 && SrcEquip < GNumEquipSlots;
	const bool bDstGridValid = DstGrid >= 0 && DstGrid < SlotCount;
	const bool bDstEquipValid = DstEquip >= 0 && DstEquip < GNumEquipSlots;

	// Ignore drags not coming from inventory/equip slots.
	if (!bSrcGridValid && !bSrcEquipValid)
		return true;

	// Ignore non-slot hover targets while dragging.
	if (!bDstGridValid && !bDstEquipValid)
		return true;

	if (bSrcGridValid && Slots[SrcGrid].ItemId.empty())
		return false;
	if (bSrcEquipValid && EquipWeapons[SrcEquip].ItemId.empty())
		return false;

	// Grid -> Grid
	if (bSrcGridValid && bDstGridValid)
		return true;

	// Grid -> Equip
	if (bSrcGridValid && bDstEquipValid)
	{
		if (EquipWeapons[DstEquip].Locked)
			return false;

		const FRmlItemDef* Def = FindItem(DB, Slots[SrcGrid].ItemId);
		return Def && CanEquip(DstEquip, Def->TypeName);
	}

	// Equip -> Grid
	if (bSrcEquipValid && bDstGridValid)
	{
		const int SrcGridFromEquip = EquipSourceIndex ? EquipSourceIndex[SrcEquip] : -1;
		if (SrcGridFromEquip < 0 || SrcGridFromEquip >= SlotCount)
			return false;

		// Dropping onto own source slot or empty grid is always valid (unequip/move).
		if (DstGrid == SrcGridFromEquip || Slots[DstGrid].ItemId.empty())
			return true;

		// Occupied grid: displaced item must be valid for the source equip slot.
		const FRmlItemDef* DstDef = FindItem(DB, Slots[DstGrid].ItemId);
		return DstDef && CanEquip(SrcEquip, DstDef->TypeName);
	}

	// Equip -> Equip
	if (bSrcEquipValid && bDstEquipValid)
	{
		if (EquipWeapons[DstEquip].Locked)
			return false;

		if (!EquipWeapons[SrcEquip].ItemId.empty())
		{
			const FRmlItemDef* SrcDef = FindItem(DB, EquipWeapons[SrcEquip].ItemId);
			if (!SrcDef || !CanEquip(DstEquip, SrcDef->TypeName))
				return false;
		}

		if (!EquipWeapons[DstEquip].ItemId.empty())
		{
			const FRmlItemDef* DstDef = FindItem(DB, EquipWeapons[DstEquip].ItemId);
			if (!DstDef || !CanEquip(SrcEquip, DstDef->TypeName))
				return false;
		}

		return true;
	}

	return true;
}

static void UpdateInvalidDropFeedback(
	const Rml::Event& Event,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	Rml::DataModelHandle& Handle,
	int& InOutGridIndex,
	int& InOutEquipIndex,
	const int* EquipSourceIndex)
{
	Rml::Element* DragElement = static_cast<Rml::Element*>(
		Event.GetParameter<void*>("drag_element", nullptr));
	Rml::Element* DropTarget = Event.GetTargetElement();

	if (!DragElement || !DropTarget)
	{
		ClearInvalidDropFeedback(Slots, EquipWeapons, Handle, InOutGridIndex, InOutEquipIndex);
		return;
	}

	const int SrcGrid = GetSlotIndex(DragElement);
	const int SrcEquip = GetEquipIndex(DragElement);
	const int DstGrid = GetSlotIndex(DropTarget);
	const int DstEquip = GetEquipIndex(DropTarget);

	const int SlotCount = static_cast<int>(Slots.size());
	const bool bDstGridValid = DstGrid >= 0 && DstGrid < SlotCount;
	const bool bDstEquipValid = DstEquip >= 0 && DstEquip < GNumEquipSlots;

	// Dragging over non-slot areas should remove invalid feedback.
	if (!bDstGridValid && !bDstEquipValid)
	{
		ClearInvalidDropFeedback(Slots, EquipWeapons, Handle, InOutGridIndex, InOutEquipIndex);
		return;
	}

	if (IsDropValidForTarget(SrcGrid, SrcEquip, DstGrid, DstEquip, Slots, EquipWeapons, EquipSourceIndex))
	{
		ClearInvalidDropFeedback(Slots, EquipWeapons, Handle, InOutGridIndex, InOutEquipIndex);
		return;
	}

	// Already highlighting the same invalid target.
	if (bDstGridValid && InOutGridIndex == DstGrid)
		return;
	if (bDstEquipValid && InOutEquipIndex == DstEquip)
		return;

	ClearInvalidDropFeedback(Slots, EquipWeapons, Handle, InOutGridIndex, InOutEquipIndex);

	if (bDstGridValid)
	{
		Slots[DstGrid].DropInvalid = 1;
		Handle.DirtyVariable("slots");
		InOutGridIndex = DstGrid;
		InOutEquipIndex = -1;
	}
	else
	{
		FRmlSlotData& EquipSlot = EquipWeapons[DstEquip];
		EquipSlot.DropInvalid = 1;
		Handle.DirtyVariable(GEquipBindingNames[DstEquip]);
		InOutGridIndex = -1;
		InOutEquipIndex = DstEquip;
	}
}

static int GetSelectedGridIndex(const Rml::Vector<FRmlSlotData>& Slots)
{
	const int SlotCount = static_cast<int>(Slots.size());
	for (int i = 0; i < SlotCount; ++i)
	{
		if (Slots[i].Selected == 1)
			return i;
	}
	return -1;
}

static int ResolveGridSelectionIndexFromElement(
	Rml::Element* Target,
	const Rml::Vector<FRmlSlotData>& Slots,
	const FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex)
{
	if (!Target)
		return -1;

	const int SlotCount = static_cast<int>(Slots.size());
	const int GridIdx = GetSlotIndex(Target);
	if (GridIdx >= 0 && GridIdx < SlotCount && !Slots[GridIdx].ItemId.empty())
		return GridIdx;

	const int EquipIdx = GetEquipIndex(Target);
	if (EquipIdx >= 0 && EquipIdx < GNumEquipSlots)
	{
		if (EquipWeapons[EquipIdx].Locked || EquipWeapons[EquipIdx].ItemId.empty())
			return -1;

		const int SrcGrid = EquipSourceIndex ? EquipSourceIndex[EquipIdx] : -1;
		if (SrcGrid >= 0 && SrcGrid < SlotCount && !Slots[SrcGrid].ItemId.empty())
			return SrcGrid;
	}

	return -1;
}

static bool ResolveHoverSelectionFromElement(
	Rml::Element* Target,
	const Rml::Vector<FRmlSlotData>& Slots,
	const FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex,
	int& OutDetailsGridIdx,
	int& OutHoverGridIdx,
	int& OutHoverEquipIdx)
{
	OutDetailsGridIdx = -1;
	OutHoverGridIdx = -1;
	OutHoverEquipIdx = -1;

	if (!Target)
		return false;

	const int SlotCount = static_cast<int>(Slots.size());
	const int GridIdx = GetSlotIndex(Target);
	if (GridIdx >= 0 && GridIdx < SlotCount && !Slots[GridIdx].ItemId.empty())
	{
		OutDetailsGridIdx = GridIdx;
		OutHoverGridIdx = GridIdx;
		return true;
	}

	const int EquipIdx = GetEquipIndex(Target);
	if (EquipIdx >= 0 && EquipIdx < GNumEquipSlots &&
		!EquipWeapons[EquipIdx].Locked && !EquipWeapons[EquipIdx].ItemId.empty())
	{
		const int SrcGrid = EquipSourceIndex ? EquipSourceIndex[EquipIdx] : -1;
		if (SrcGrid >= 0 && SrcGrid < SlotCount && !Slots[SrcGrid].ItemId.empty())
		{
			OutDetailsGridIdx = SrcGrid;
			OutHoverEquipIdx = EquipIdx;
			return true;
		}
	}

	return false;
}

static Rml::Element* ResolveSlotContainerElement(Rml::Element* Target)
{
	for (Rml::Element* It = Target; It; It = It->GetParentNode())
	{
		if (GetSlotIndex(It) >= 0 || GetEquipIndex(It) >= 0)
			return It;
	}
	return nullptr;
}

static void CacheGridSlotRects(
	Rml::ElementDocument* Doc,
	Rml::Vector<FRmlCachedSlotRect>& OutRects,
	int SlotCount)
{
	OutRects.resize(SlotCount, {0, 0, 0, 0});
	Rml::Element* Grid = Doc ? Doc->GetElementById("inv-grid") : nullptr;
	if (!Grid)
		return;
	for (int i = 0; i < Grid->GetNumChildren(); ++i)
	{
		Rml::Element* Child = Grid->GetChild(i);
		const int Idx = GetSlotIndex(Child);
		if (Idx >= 0 && Idx < SlotCount)
		{
			OutRects[Idx] = {
				Child->GetAbsoluteLeft(),
				Child->GetAbsoluteTop(),
				Child->GetOffsetWidth(),
				Child->GetOffsetHeight()
			};
		}
	}
}

static int FindBestOverlappingGridSlot(
	float MouseX,
	float MouseY,
	float ClickOffsetX,
	float ClickOffsetY,
	float SlotW,
	float SlotH,
	const Rml::Vector<FRmlCachedSlotRect>& CachedRects,
	const Rml::Vector<FRmlSlotData>& Slots)
{
	const float DragLeft   = MouseX - ClickOffsetX;
	const float DragTop    = MouseY - ClickOffsetY;
	const float DragRight  = DragLeft + SlotW;
	const float DragBottom = DragTop  + SlotH;

	int BestIdx = -1;
	float BestArea = 0.0f;
	const int Count = FMath::Min(
		static_cast<int>(CachedRects.size()),
		static_cast<int>(Slots.size()));

	for (int i = 0; i < Count; ++i)
	{
		if (Slots[i].ItemId.empty())
			continue;
		const FRmlCachedSlotRect& R = CachedRects[i];
		const float OvL = FMath::Max(DragLeft,  R.Left);
		const float OvT = FMath::Max(DragTop,   R.Top);
		const float OvR = FMath::Min(DragRight,  R.Left + R.Width);
		const float OvB = FMath::Min(DragBottom, R.Top  + R.Height);
		const float Area = FMath::Max(0.0f, OvR - OvL) *
		                   FMath::Max(0.0f, OvB - OvT);
		if (Area > BestArea)
		{
			BestArea = Area;
			BestIdx = i;
		}
	}
	return BestIdx;
}

static int FindEquipBySourceGrid(int GridIdx, const int* EquipSourceIndex)
{
	if (!EquipSourceIndex || GridIdx < 0)
		return -1;
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipSourceIndex[i] == GridIdx)
			return i;
	}
	return -1;
}

static void SelectGridForDetails(
	int GridIdx,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex,
	Rml::DataModelHandle& Handle,
	bool bMarkAsDragOrigin)
{
	const int SlotCount = static_cast<int>(Slots.size());
	if (GridIdx < 0 || GridIdx >= SlotCount || Slots[GridIdx].ItemId.empty())
		return;

	const int CurrentSelected = GetSelectedGridIndex(Slots);
	if (CurrentSelected == GridIdx)
	{
		if (bMarkAsDragOrigin)
		{
			// Only dirty if any selected slot actually needs DragOrigin set.
			bool bNeedsMark = false;
			for (const FRmlSlotData& S : Slots)
				if (S.Selected == 1 && S.DragOrigin == 0) { bNeedsMark = true; break; }
			if (!bNeedsMark)
				for (int i = 0; i < GNumEquipSlots; ++i)
					if (EquipWeapons[i].Selected == 1 && EquipWeapons[i].DragOrigin == 0) { bNeedsMark = true; break; }
			if (bNeedsMark)
			{
				MarkSelectedAsDragOrigin(Slots, EquipWeapons);
				Handle.DirtyVariable("slots");
				DirtyAllEquip(Handle);
			}
		}
		return;
	}

	ClearAllSelections(Slots, EquipWeapons);
	Slots[GridIdx].Selected = 1;
	SyncSelection(GridIdx, -1, Slots, EquipWeapons, EquipSourceIndex);
	if (bMarkAsDragOrigin)
		MarkSelectedAsDragOrigin(Slots, EquipWeapons);
	Handle.DirtyVariable("slots");
	DirtyAllEquip(Handle);
}

static void ApplyDragHoverHighlightMask(
	int DetailsGridIdx,
	int HoverGridIdx,
	int HoverEquipIdx,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex,
	Rml::DataModelHandle& Handle)
{
	bool bSlotsChanged = false;
	bool bEquipChanged = false;

	// Reset suppression on currently selected elements.
	for (FRmlSlotData& Slot : Slots)
	{
		if (Slot.Selected == 1 && Slot.DragOrigin == 1)
		{
			Slot.DragOrigin = 0;
			bSlotsChanged = true;
		}
	}
	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipWeapons[i].Selected == 1 && EquipWeapons[i].DragOrigin == 1)
		{
			EquipWeapons[i].DragOrigin = 0;
			bEquipChanged = true;
		}
	}

	// During drag, highlight only the hovered target; suppress its synced copy.
	if (HoverEquipIdx >= 0)
	{
		if (DetailsGridIdx >= 0 && DetailsGridIdx < static_cast<int>(Slots.size()) &&
			Slots[DetailsGridIdx].Selected == 1 && Slots[DetailsGridIdx].DragOrigin == 0)
		{
			Slots[DetailsGridIdx].DragOrigin = 1; // hide grid copy highlight
			bSlotsChanged = true;
		}
	}
	else if (HoverGridIdx >= 0)
	{
		const int SyncedEquipIdx = FindEquipBySourceGrid(DetailsGridIdx, EquipSourceIndex);
		if (SyncedEquipIdx >= 0 && SyncedEquipIdx < GNumEquipSlots &&
			EquipWeapons[SyncedEquipIdx].Selected == 1 && EquipWeapons[SyncedEquipIdx].DragOrigin == 0)
		{
			EquipWeapons[SyncedEquipIdx].DragOrigin = 1; // hide equip copy highlight
			bEquipChanged = true;
		}
	}

	if (bSlotsChanged)
		Handle.DirtyVariable("slots");
	if (bEquipChanged)
		DirtyAllEquip(Handle);
}

/** Update drag details panel from grid overlap detection (overlap or fallback to source). */
static void UpdateDragDetailsFromOverlap(
	float MouseX,
	float MouseY,
	float ClickOffsetX,
	float ClickOffsetY,
	float SlotW,
	float SlotH,
	const Rml::Vector<FRmlCachedSlotRect>& CachedRects,
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	const int* EquipSourceIndex,
	Rml::DataModelHandle& Handle,
	int DragSourceGridIndex,
	int& CurrentDragDetailsGridIndex,
	double& LastValidDragHoverTimeSeconds,
	bool bMarkAsDragOrigin)
{
	const double Now = FPlatformTime::Seconds();
	const int OverlapIdx = FindBestOverlappingGridSlot(
		MouseX, MouseY, ClickOffsetX, ClickOffsetY,
		SlotW, SlotH, CachedRects, Slots);

	if (OverlapIdx >= 0 && OverlapIdx != CurrentDragDetailsGridIndex)
	{
		LastValidDragHoverTimeSeconds = Now;
		SelectGridForDetails(
			OverlapIdx, Slots, EquipWeapons, EquipSourceIndex,
			Handle, false);
		CurrentDragDetailsGridIndex = OverlapIdx;
		ApplyDragHoverHighlightMask(
			OverlapIdx, OverlapIdx, -1,
			Slots, EquipWeapons, EquipSourceIndex, Handle);
	}
	else if (OverlapIdx < 0 &&
	         (Now - LastValidDragHoverTimeSeconds >= KDragHoverFallbackDelaySeconds))
	{
		SelectGridForDetails(
			DragSourceGridIndex, Slots, EquipWeapons,
			EquipSourceIndex, Handle, bMarkAsDragOrigin);
		CurrentDragDetailsGridIndex = DragSourceGridIndex;
	}
}

static void ClearDragOriginMarkers(
	Rml::Vector<FRmlSlotData>& Slots,
	FRmlSlotData* EquipWeapons,
	Rml::DataModelHandle& Handle)
{
	bool bSlotsChanged = false;
	bool bEquipChanged = false;

	for (FRmlSlotData& Slot : Slots)
	{
		if (Slot.DragOrigin == 1)
		{
			Slot.DragOrigin = 0;
			bSlotsChanged = true;
		}
	}

	for (int i = 0; i < GNumEquipSlots; ++i)
	{
		if (EquipWeapons[i].DragOrigin == 1)
		{
			EquipWeapons[i].DragOrigin = 0;
			bEquipChanged = true;
		}
	}

	if (bSlotsChanged)
		Handle.DirtyVariable("slots");
	if (bEquipChanged)
		DirtyAllEquip(Handle);
}

void URmlInventory::InitDragGeometry(const Rml::Event& event)
{
	Rml::Element* SlotEl = ResolveSlotContainerElement(event.GetTargetElement());
	if (SlotEl)
	{
		const float MX = event.GetParameter<float>("mouse_x", 0.0f);
		const float MY = event.GetParameter<float>("mouse_y", 0.0f);
		DragClickOffsetX = MX - SlotEl->GetAbsoluteLeft();
		DragClickOffsetY = MY - SlotEl->GetAbsoluteTop();
	}
	CacheGridSlotRects(BoundDocument, CachedGridSlotRects, static_cast<int>(Slots.size()));

	// Use grid slot pixel size as the virtual drag rect (matches drag clone CSS).
	DragSlotWidth = 0.0f;
	DragSlotHeight = 0.0f;
	for (const FRmlCachedSlotRect& R : CachedGridSlotRects)
	{
		if (R.Width > 0 && R.Height > 0)
		{
			DragSlotWidth = R.Width;
			DragSlotHeight = R.Height;
			break;
		}
	}

	// Clamp click offset so the mouse stays within the virtual rect.
	const float KClampMargin = 10.0f;
	if (DragSlotWidth > 0.0f)
		DragClickOffsetX = FMath::Clamp(DragClickOffsetX, KClampMargin, DragSlotWidth - KClampMargin);
	if (DragSlotHeight > 0.0f)
		DragClickOffsetY = FMath::Clamp(DragClickOffsetY, KClampMargin, DragSlotHeight - KClampMargin);

	bDragCloneAdjusted = false;
}

void URmlInventory::ResetDragState()
{
	bDragInProgress = false;
	DragSourceGridIndex = -1;
	CurrentDragDetailsGridIndex = -1;
	LastValidDragHoverTimeSeconds = 0.0;
	DragClickOffsetX = 0.0f;
	DragClickOffsetY = 0.0f;
	DragSlotWidth = 0.0f;
	DragSlotHeight = 0.0f;
	CachedGridSlotRects.clear();
	bDragCloneAdjusted = false;
}

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
			Ctx->GetRootElement()->AddEventListener(Rml::EventId::Drag, this, true);
			Ctx->GetRootElement()->AddEventListener(Rml::EventId::Dragend, this);
			bRootListenerBound = true;
		}
	}

	// Document element listeners are rebound after each reload.
	if (Rml::Element* Grid = Doc->GetElementById("inv-grid"))
	{
		Grid->AddEventListener(Rml::EventId::Dragdrop, this);
		Grid->AddEventListener(Rml::EventId::Dragstart, this);
		Grid->AddEventListener(Rml::EventId::Dragover, this);
		Grid->AddEventListener(Rml::EventId::Dragout, this);
	}

	const char* EquipIds[] = {
		"equip-main", "equip-secondary", "equip-range",
		"equip-sub-main", "equip-sub-secondary", "equip-sub-range",
		"equip-acc-1", "equip-acc-2", "equip-acc-3", "equip-acc-4", "equip-acc-5"
	};

	for (const char* Id : EquipIds)
	{
		if (Rml::Element* El = Doc->GetElementById(Id))
		{
			El->AddEventListener(Rml::EventId::Dragdrop, this);
			El->AddEventListener(Rml::EventId::Dragstart, this);
			El->AddEventListener(Rml::EventId::Dragover, this);
			El->AddEventListener(Rml::EventId::Dragout, this);
		}
	}
}

void URmlInventory::CleanupDragState()
{
	ClearInvalidDropFeedback(
		Slots, EquipWeapons, InventoryHandle,
		InvalidDropGridIndex, InvalidDropEquipIndex);
	ClearCompatibleEquipHighlight(EquipWeapons, InventoryHandle);
	ClearDragOriginMarkers(Slots, EquipWeapons, InventoryHandle);
	ResetDragState();
}

void URmlInventory::ClearSelection()
{
	ClearInvalidDropFeedback(
		Slots, EquipWeapons, InventoryHandle,
		InvalidDropGridIndex, InvalidDropEquipIndex);
	ClearAllSelections(Slots, EquipWeapons);
	InventoryHandle.DirtyVariable("slots");
	DirtyAllEquip(InventoryHandle);
	ResetDragState();
}

void URmlInventory::OnInit()
{
	RefreshSlotsFromDatabase(Slots, EquipWeapons, InventoryHandle);
	BindToDocument(BoundDocument);
}

void URmlInventory::ProcessEvent(Rml::Event& event)
{
	switch (event.GetId())
	{
	case Rml::EventId::Dragdrop:
		HandleDragDrop(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		CleanupDragState();
		break;
	case Rml::EventId::Dragstart:
		ClearDragOriginMarkers(Slots, EquipWeapons, InventoryHandle);
		HandleDragStart(event, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle);
		ClearInvalidDropFeedback(
			Slots, EquipWeapons, InventoryHandle,
			InvalidDropGridIndex, InvalidDropEquipIndex);
		DragSourceGridIndex = ResolveGridSelectionIndexFromElement(
			event.GetTargetElement(), Slots, EquipWeapons, EquipSourceIndex);
		bDragInProgress = (DragSourceGridIndex >= 0);
		CurrentDragDetailsGridIndex = DragSourceGridIndex;
		LastValidDragHoverTimeSeconds = FPlatformTime::Seconds();
		InitDragGeometry(event);
		if (bDragInProgress)
		{
			Rml::Vector<FRmlItemDef> DB = GetItemDatabase();
			const FRmlItemDef* Def = FindItem(DB, Slots[DragSourceGridIndex].ItemId);
			if (Def)
				SetCompatibleEquipHighlight(Def->TypeName, EquipWeapons, InventoryHandle);
		}
		break;
	case Rml::EventId::Dragover:
		UpdateInvalidDropFeedback(
			event,
			Slots,
			EquipWeapons,
			InventoryHandle,
			InvalidDropGridIndex,
			InvalidDropEquipIndex,
			EquipSourceIndex);
		if (bDragInProgress)
		{
			const double Now = FPlatformTime::Seconds();
			int DetailsGridIndex = -1;
			int HoverGridIndex = -1;
			int HoverEquipIndex = -1;
			if (ResolveHoverSelectionFromElement(
				event.GetTargetElement(),
				Slots,
				EquipWeapons,
				EquipSourceIndex,
				DetailsGridIndex,
				HoverGridIndex,
				HoverEquipIndex))
			{
				LastValidDragHoverTimeSeconds = Now;
				SelectGridForDetails(
					DetailsGridIndex, Slots, EquipWeapons, EquipSourceIndex, InventoryHandle, false);
				CurrentDragDetailsGridIndex = DetailsGridIndex;
				ApplyDragHoverHighlightMask(
					DetailsGridIndex,
					HoverGridIndex,
					HoverEquipIndex,
					Slots,
					EquipWeapons,
					EquipSourceIndex,
					InventoryHandle);
			}
			else
			{
				UpdateDragDetailsFromOverlap(
					event.GetParameter<float>("mouse_x", 0.0f),
					event.GetParameter<float>("mouse_y", 0.0f),
					DragClickOffsetX, DragClickOffsetY,
					DragSlotWidth, DragSlotHeight,
					CachedGridSlotRects, Slots, EquipWeapons, EquipSourceIndex,
					InventoryHandle, DragSourceGridIndex,
					CurrentDragDetailsGridIndex, LastValidDragHoverTimeSeconds, false);
			}
		}
		break;
	case Rml::EventId::Drag:
		if (bDragInProgress)
		{
			// On first Drag event, reposition clone so the cursor is within its bounds.
			// CreateDragClone runs AFTER Dragstart, so the clone only exists here.
			if (!bDragCloneAdjusted)
			{
				bDragCloneAdjusted = true;
				if (Rml::Element* Clone = BoundContext ? BoundContext->GetDragClone() : nullptr)
				{
					Clone->SetProperty(Rml::PropertyId::Left,
						Rml::Property(-DragClickOffsetX, Rml::Unit::PX));
					Clone->SetProperty(Rml::PropertyId::Top,
						Rml::Property(-DragClickOffsetY, Rml::Unit::PX));
				}
			}

			UpdateDragDetailsFromOverlap(
				event.GetParameter<float>("mouse_x", 0.0f),
				event.GetParameter<float>("mouse_y", 0.0f),
				DragClickOffsetX, DragClickOffsetY,
				DragSlotWidth, DragSlotHeight,
				CachedGridSlotRects, Slots, EquipWeapons, EquipSourceIndex,
				InventoryHandle, DragSourceGridIndex,
				CurrentDragDetailsGridIndex, LastValidDragHoverTimeSeconds, true);
		}
		break;
	case Rml::EventId::Dragout:
		ClearInvalidDropFeedback(
			Slots, EquipWeapons, InventoryHandle,
			InvalidDropGridIndex, InvalidDropEquipIndex);
		break;
	case Rml::EventId::Dragend:
		CleanupDragState();
		break;
	case Rml::EventId::Mouseover:
		if (!bDragInProgress)
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
