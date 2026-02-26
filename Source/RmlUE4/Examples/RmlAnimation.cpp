#include "RmlAnimation.h"
#include "RmlUi/Core.h"

void URmlAnimation::OnInit()
{
	BoundDocument->GetElementById("title")->SetInnerRML("Animation Sample");

	// Listen for animationend to drive the random-position loop.
	BoundDocument->AddEventListener(Rml::EventId::Animationend, this);

	// --- Button animations ---
	{
		auto* El = BoundDocument->GetElementById("start_game");
		auto P1 = Rml::Transform::MakeProperty({Rml::Transforms::Rotate2D{10.f}, Rml::Transforms::TranslateX{100.f}});
		auto P2 = Rml::Transform::MakeProperty({Rml::Transforms::Scale2D{3.f}});
		El->Animate("transform", P1, 1.8f, Rml::Tween{Rml::Tween::Elastic, Rml::Tween::InOut}, -1, true);
		El->AddAnimationKey("transform", P2, 1.3f, Rml::Tween{Rml::Tween::Elastic, Rml::Tween::InOut});
	}
	{
		auto* El = BoundDocument->GetElementById("high_scores");
		El->Animate("margin-left", Rml::Property(0.f, Rml::Unit::PX), 0.3f, Rml::Tween{Rml::Tween::Sine, Rml::Tween::In}, 10, true, 1.f);
		El->AddAnimationKey("margin-left", Rml::Property(100.f, Rml::Unit::PX), 3.0f, Rml::Tween{Rml::Tween::Circular, Rml::Tween::Out});
	}
	{
		auto* El = BoundDocument->GetElementById("options");
		El->Animate("image-color", Rml::Property(Rml::Colourb(128, 255, 255, 255), Rml::Unit::COLOUR), 0.3f, Rml::Tween{}, -1, false);
		El->AddAnimationKey("image-color", Rml::Property(Rml::Colourb(128, 128, 255, 255), Rml::Unit::COLOUR), 0.3f);
		El->AddAnimationKey("image-color", Rml::Property(Rml::Colourb(0, 128, 128, 255), Rml::Unit::COLOUR), 0.3f);
		El->AddAnimationKey("image-color", Rml::Property(Rml::Colourb(64, 128, 255, 0), Rml::Unit::COLOUR), 0.9f);
		El->AddAnimationKey("image-color", Rml::Property(Rml::Colourb(255, 255, 255, 255), Rml::Unit::COLOUR), 0.3f);
	}
	{
		auto* El = BoundDocument->GetElementById("exit");
		Rml::PropertyDictionary PD;
		Rml::StyleSheetSpecification::ParsePropertyDeclaration(PD, "transform", "translate(200px, 200px) rotate(1215deg)");
		El->Animate("transform", *PD.GetProperty(Rml::PropertyId::Transform), 3.f, Rml::Tween{Rml::Tween::Bounce, Rml::Tween::Out}, -1);
	}

	// --- Transform tests ---
	{
		auto* El = BoundDocument->GetElementById("generic");
		auto P = Rml::Transform::MakeProperty(
			{Rml::Transforms::TranslateY{50, Rml::Unit::PX}, Rml::Transforms::Rotate3D{0, 0, 1, -90, Rml::Unit::DEG}, Rml::Transforms::ScaleY{0.8f}});
		El->Animate("transform", P, 1.5f, Rml::Tween{Rml::Tween::Sine, Rml::Tween::InOut}, -1, true);
	}
	{
		auto* El = BoundDocument->GetElementById("combine");
		auto P = Rml::Transform::MakeProperty({Rml::Transforms::Translate2D{50, 50, Rml::Unit::PX}, Rml::Transforms::Rotate2D(1215)});
		El->Animate("transform", P, 8.0f, Rml::Tween{}, -1, true);
	}
	{
		auto* El = BoundDocument->GetElementById("decomposition");
		auto P = Rml::Transform::MakeProperty({Rml::Transforms::TranslateY{50, Rml::Unit::PX}, Rml::Transforms::Rotate3D{0.8f, 0, 1, 110, Rml::Unit::DEG}});
		El->Animate("transform", P, 1.3f, Rml::Tween{Rml::Tween::Quadratic, Rml::Tween::InOut}, -1, true);
	}

	// --- Mixed units tests ---
	{
		auto* El = BoundDocument->GetElementById("abs_rel");
		El->Animate("margin-left", Rml::Property(50.f, Rml::Unit::PERCENT), 1.5f, Rml::Tween{}, -1, true);
	}
	{
		auto* El = BoundDocument->GetElementById("abs_rel_transform");
		auto P = Rml::Transform::MakeProperty({Rml::Transforms::TranslateX{0, Rml::Unit::PX}});
		El->Animate("transform", P, 1.5f, Rml::Tween{}, -1, true);
	}
	{
		auto* El = BoundDocument->GetElementById("animation_event");
		El->Animate("top", Rml::Property(Rml::Math::RandomReal(250.f), Rml::Unit::PX), 1.5f, Rml::Tween{Rml::Tween::Cubic, Rml::Tween::InOut});
		El->Animate("left", Rml::Property(Rml::Math::RandomReal(250.f), Rml::Unit::PX), 1.5f, Rml::Tween{Rml::Tween::Cubic, Rml::Tween::InOut});
	}
}

void URmlAnimation::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!BoundDocument) return;

	FadeTimer += DeltaTime;
	if (FadeTimer >= 1.4f)
	{
		FadeTimer = 0.0f;

		auto* El = BoundDocument->GetElementById("help");
		if (!El) return;

		if (El->IsClassSet("fadeout"))
		{
			El->SetClass("fadeout", false);
			El->SetClass("fadein", true);
		}
		else if (El->IsClassSet("fadein"))
		{
			El->SetClass("fadein", false);
			El->SetClass("textalign", true);
		}
		else
		{
			El->SetClass("textalign", false);
			El->SetClass("fadeout", true);
		}
	}
}

void URmlAnimation::OnKeyDown()
{
	Rml::Input::KeyIdentifier Key = (Rml::Input::KeyIdentifier)CurrentEvent->GetParameter<int>("key_identifier", 0);

	auto* El = BoundDocument->GetElementById("keyevent_response");
	if (!El) return;

	if (Key == Rml::Input::KI_LEFT)
	{
		El->Animate("left", Rml::Property{-200.f, Rml::Unit::DP}, 0.5f, Rml::Tween{Rml::Tween::Cubic});
	}
	else if (Key == Rml::Input::KI_RIGHT)
	{
		El->Animate("left", Rml::Property{200.f, Rml::Unit::DP}, 0.5f, Rml::Tween{Rml::Tween::Cubic});
	}
	else if (Key == Rml::Input::KI_UP)
	{
		auto OffsetRight = Rml::Property{200.f, Rml::Unit::DP};
		El->Animate("left", Rml::Property{0.f, Rml::Unit::PX}, 0.5f, Rml::Tween{Rml::Tween::Cubic}, 1, true, 0, &OffsetRight);
	}
	else if (Key == Rml::Input::KI_DOWN)
	{
		El->Animate("left", Rml::Property{0.f, Rml::Unit::PX}, 0.5f, Rml::Tween{Rml::Tween::Cubic});
	}
}

void URmlAnimation::ProcessEvent(Rml::Event& event)
{
	// Handle animationend for the random-position loop.
	if (event.GetId() == Rml::EventId::Animationend)
	{
		auto* El = event.GetTargetElement();
		if (El && El->GetId() == "animation_event")
		{
			El->Animate("top", Rml::Property(Rml::Math::RandomReal(200.f), Rml::Unit::PX), 1.2f, Rml::Tween{Rml::Tween::Cubic, Rml::Tween::InOut});
			El->Animate("left", Rml::Property(Rml::Math::RandomReal(100.f), Rml::Unit::PERCENT), 0.8f, Rml::Tween{Rml::Tween::Cubic, Rml::Tween::InOut});
		}
		return;
	}

	// Delegate all other events (keydown, keyup, onclick) to base.
	URmlDocument::ProcessEvent(event);
}

void URmlAnimation::add_class()
{
	if (auto* El = BoundDocument->GetElementById("transition_class"))
	{
		El->SetClass("move_me", !El->IsClassSet("move_me"));
	}
}
