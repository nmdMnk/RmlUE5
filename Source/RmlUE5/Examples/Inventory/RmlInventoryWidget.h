#pragma once

#include "CoreMinimal.h"
#include "RmlUiWidget.h"
#include "RmlInventoryWidget.generated.h"

class URmlInventory;

/**
 * Self-contained inventory UMG widget.
 * Drop into a Widget Blueprint, set DocumentPath to Inventory/inventory.rml,
 * and the data model + drag-drop just work — no external glue code needed.
 */
UCLASS(meta = (DisplayName = "RmlUI Inventory Widget"))
class RMLUE5_API URmlInventoryWidget : public URmlUiWidget
{
	GENERATED_BODY()

public:
	URmlInventoryWidget();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void OnDocumentsReloaded(Rml::Context* Ctx) override;
	virtual void OnEmptyClick() override;

private:
	UPROPERTY()
	TObjectPtr<URmlInventory> Inventory;
};
