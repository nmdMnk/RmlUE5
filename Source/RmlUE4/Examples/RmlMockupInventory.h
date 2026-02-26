#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlMockupInventory.generated.h"

UCLASS()
class URmlMockupInventory : public URmlDocument
{
	GENERATED_BODY()
protected:
	// ~Begin URmlDocument API
	virtual void OnInit() override;
	// ~End URmlDocument API

	// ~Begin Rml::EventListener API
	virtual void ProcessEvent(Rml::Event& event) override;
	// ~End Rml::EventListener API

private:
	void HandleDragDrop(Rml::Event& event);
};
