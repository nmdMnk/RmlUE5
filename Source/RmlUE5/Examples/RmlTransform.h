#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlTransform.generated.h"

UCLASS()
class URmlTransform : public URmlDocument
{
	GENERATED_BODY()
protected:
	// ~Begin URmlDocument API
	virtual void OnInit() override;
	virtual void OnKeyDown() override;
	virtual void Tick(float DeltaTime) override;
	// ~End URmlDocument API
private:
	bool bRunRotate = false;
	float RotationAngle = 0.f;
};
