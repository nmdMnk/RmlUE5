#include "RmlTransform.h"
#include "RmlUi/Core.h"

void URmlTransform::OnInit()
{
	BoundDocument->GetElementById("title")->SetInnerRML("Transform Sample");
}

void URmlTransform::OnKeyDown()
{
	Rml::Input::KeyIdentifier Key = (Rml::Input::KeyIdentifier)CurrentEvent->GetParameter<int>("key_identifier", 0);
	if (Key == Rml::Input::KI_SPACE)
	{
		bRunRotate = !bRunRotate;
	}
}

void URmlTransform::Tick(float DeltaTime)
{
	if (!bRunRotate || !BoundDocument)
		return;

	RotationAngle = FMath::Fmod(RotationAngle + DeltaTime * 90.f, 360.f);

	Rml::String TransformValue = Rml::CreateString("translateZ(50dp) rotateY(%.1fdeg)", RotationAngle);

	Rml::ElementList Cubes;
	BoundDocument->GetElementsByClassName(Cubes, "cube");
	for (auto* Cube : Cubes)
	{
		Cube->SetProperty("transform", TransformValue);
	}
}
