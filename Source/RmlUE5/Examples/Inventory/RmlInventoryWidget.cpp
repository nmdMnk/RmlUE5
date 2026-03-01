#include "RmlInventoryWidget.h"
#include "RmlInventory.h"
#include "RmlUi/Core.h"

URmlInventoryWidget::URmlInventoryWidget()
{
	DocumentPath = TEXT("Content/RmlAssets/assets/Examples/Inventory/inventory.rml");
	Fonts.Add({TEXT("Content/RmlAssets/assets/LatoLatin-Regular.ttf")});

	TWeakObjectPtr<URmlInventoryWidget> WeakThis(this);
	OnContextReady = [WeakThis](Rml::Context* Ctx)
	{
		URmlInventoryWidget* Self = WeakThis.Get();
		if (!Self)
			return;
		if (!Self->Inventory)
			Self->Inventory = NewObject<URmlInventory>(Self);
		Self->Inventory->CreateDataModel(Ctx);
	};
}

TSharedRef<SWidget> URmlInventoryWidget::RebuildWidget()
{
	TSharedRef<SWidget> Result = Super::RebuildWidget();
	EnableEmptyClickCapture();
	OnDocumentsReloaded(GetContext());
	return Result;
}

void URmlInventoryWidget::OnEmptyClick()
{
	if (Inventory)
		Inventory->ClearSelection();
}

void URmlInventoryWidget::OnDocumentsReloaded(Rml::Context* Ctx)
{
	if (!Inventory || !Ctx) return;

	for (int i = 0; i < Ctx->GetNumDocuments(); ++i)
	{
		Rml::ElementDocument* Doc = Ctx->GetDocument(i);
		if (Doc && Doc->GetElementById("inv-grid"))
		{
			Inventory->BindToDocument(Doc);
			break;
		}
	}
}
