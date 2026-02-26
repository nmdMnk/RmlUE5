#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlAnimation.generated.h"

UCLASS()
class URmlAnimation : public URmlDocument
{
	GENERATED_BODY()
protected:
	// ~Begin URmlDocument API
	virtual void OnInit() override;
	virtual void OnKeyDown() override;
	// ~End URmlDocument API

	// ~Begin FTickableGameObject API
	virtual void Tick(float DeltaTime) override;
	// ~End FTickableGameObject API

	// ~Begin Rml::EventListener API
	virtual void ProcessEvent(Rml::Event& event) override;
	// ~End Rml::EventListener API

	// !Begin Events
	UFUNCTION()
	void add_class();
	// !End Events
private:
	float FadeTimer = 0.0f;
};
