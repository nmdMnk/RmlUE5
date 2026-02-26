#include "RmlEffects.h"
#include "RmlUi/Core.h"

void URmlEffects::CreateDataModel(Rml::Context* InContext)
{
	if (Rml::DataModelConstructor Constructor = InContext->CreateDataModel("effects"))
	{
		Constructor.Bind("show_menu", &bShowMenu);
		Constructor.Bind("submenu", &Submenu);

		Constructor.Bind("opacity", &Filter.Opacity);
		Constructor.Bind("sepia", &Filter.Sepia);
		Constructor.Bind("grayscale", &Filter.Grayscale);
		Constructor.Bind("saturate", &Filter.Saturate);
		Constructor.Bind("brightness", &Filter.Brightness);
		Constructor.Bind("contrast", &Filter.Contrast);
		Constructor.Bind("hue_rotate", &Filter.HueRotate);
		Constructor.Bind("invert", &Filter.Invert);
		Constructor.Bind("blur", &Filter.Blur);
		Constructor.Bind("drop_shadow", &Filter.bDropShadow);

		Constructor.Bind("scale", &Transform.Scale);
		Constructor.Bind("rotate_x", &Transform.RotateX);
		Constructor.Bind("rotate_y", &Transform.RotateY);
		Constructor.Bind("rotate_z", &Transform.RotateZ);
		Constructor.Bind("perspective", &Transform.Perspective);
		Constructor.Bind("perspective_origin_x", &Transform.PerspectiveOriginX);
		Constructor.Bind("perspective_origin_y", &Transform.PerspectiveOriginY);
		Constructor.Bind("transform_all", &Transform.bTransformAll);

		Constructor.BindEventCallback("reset",
			[this](Rml::DataModelHandle Handle, Rml::Event& /*Ev*/, const Rml::VariantList& /*Args*/)
			{
				if (Submenu == "transform")
					Transform = FTransformData{};
				else if (Submenu == "filter")
					Filter = FFilterData{};
				Handle.DirtyAllVariables();
			});
	}
}
