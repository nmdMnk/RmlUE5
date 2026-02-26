#pragma once
#include "CoreMinimal.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/EventListener.h"
#include "UObject/Object.h"
#include "RmlDocument.generated.h"

using RmlEventListener = Rml::EventListener;

UCLASS()
class UERMLUI_API URmlDocument
	: public UObject
	, public FTickableGameObject
	, public RmlEventListener
{
	GENERATED_BODY()
public:
	using UObject::ProcessEvent;
	bool Init(Rml::Context* InCtx, const FString& InDocPath);
	void ShutDown();

	/**
	 * Close the document and reload it from disk, then call OnInit() again.
	 * Preserves visibility state. Subclasses with extra documents should
	 * override OnPreReload() to close them before the reload happens.
	 * Call Rml::Factory::ClearStyleSheetCache() before if RCSS changes are involved.
	 * Returns false if re-init fails (document remains closed).
	 */
	bool Reload();

	Rml::ElementDocument* GetDocument() const { return BoundDocument; }

	UFUNCTION()
	void Show() { if (BoundDocument) BoundDocument->Show(); }

	UFUNCTION()
	void Hide() { if (BoundDocument) BoundDocument->Hide(); }

	void SetNotifyObject(const FString& InName, UObject* InObject) { EventNotifyMap.Add(InName, TObjectPtr<UObject>(InObject)); }
protected:
	virtual void OnInit() {}
	virtual void OnKeyDown() {}
	virtual void OnKeyUp() {}
	/** Called at the start of Reload(), before BoundDocument is closed.
	 *  Override to close extra documents owned by the subclass. */
	virtual void OnPreReload() {}

	// ~Begin Rml::EventListener API
	virtual void ProcessEvent(Rml::Event& event) override;
	// ~End Rml::EventListener API

	// ~Begin UObject API
	virtual void BeginDestroy() override;
	// ~End UObject API

	// ~Begin FTickableGameObject API
	virtual void Tick(float DeltaTime) override {}
	virtual TStatId GetStatId() const override { return Super::GetStatID(); }
	// ~End FTickableGameObject API

	Rml::ElementDocument*			BoundDocument  = nullptr;
	Rml::Context*					BoundContext   = nullptr;
	UPROPERTY()
	TMap<FString, TObjectPtr<UObject>>	EventNotifyMap;
	Rml::Event*						CurrentEvent   = nullptr;
};
