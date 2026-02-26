#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlEffects.generated.h"

UCLASS()
class URmlEffects : public URmlDocument
{
	GENERATED_BODY()
public:
	/** Must be called BEFORE Init() -- data model must exist before LoadDocument. */
	void CreateDataModel(Rml::Context* InContext);

private:
	static constexpr float PerspectiveMax = 3000.f;

	// Menu state
	bool bShowMenu = false;
	Rml::String Submenu = "filter";

	// Filter data
	struct FFilterData
	{
		float Opacity = 1.0f;
		float Sepia = 0.0f;
		float Grayscale = 0.0f;
		float Saturate = 1.0f;
		float Brightness = 1.0f;
		float Contrast = 1.0f;
		float HueRotate = 0.0f;
		float Invert = 0.0f;
		float Blur = 0.0f;
		bool bDropShadow = false;
	} Filter;

	// Transform data
	struct FTransformData
	{
		float Scale = 1.0f;
		float RotateX = 0.0f;
		float RotateY = 0.0f;
		float RotateZ = 0.0f;
		float Perspective = PerspectiveMax;
		float PerspectiveOriginX = 50.f;
		float PerspectiveOriginY = 50.f;
		bool bTransformAll = false;
	} Transform;
};
