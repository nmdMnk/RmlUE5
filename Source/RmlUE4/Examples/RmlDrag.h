#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlDrag.generated.h"

UCLASS()
class URmlDrag : public URmlDocument
{
	GENERATED_BODY()
protected:
	// ~Begin URmlDocument API
	virtual void OnInit() override;
	virtual void OnPreReload() override;
	// ~End URmlDocument API

	// ~Begin FTickableGameObject API
	virtual void Tick(float DeltaTime) override;
	// ~End FTickableGameObject API

	// ~Begin Rml::EventListener API
	virtual void ProcessEvent(Rml::Event& event) override;
	// ~End Rml::EventListener API

public:
	/** Close both inventory documents. Must be called before context removal. */
	void ShutDownAll();

private:
	void HandleDragDrop(Rml::Event& event);
	void AddItem(Rml::ElementDocument* Doc, const Rml::String& Name);

	Rml::ElementDocument* SecondDocument = nullptr;
};
