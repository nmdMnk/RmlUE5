#include "RmlDataBinding.h"
#include "RmlUi/Core.h"
#include <numeric>

void URmlDataBinding::CreateDataModels(Rml::Context* InContext)
{
	// ---- Basics ----
	if (Rml::DataModelConstructor C = InContext->CreateDataModel("basics"))
	{
		C.Bind("title", &Basics.Title);
		C.Bind("animal", &Basics.Animal);
		C.Bind("show_text", &Basics.bShowText);
	}

	// ---- Events ----
	if (Rml::DataModelConstructor C = InContext->CreateDataModel("events"))
	{
		C.RegisterArray<Rml::Vector<float>>();

		if (auto Vec2Handle = C.RegisterStruct<Rml::Vector2f>())
		{
			Vec2Handle.RegisterMember("x", &Rml::Vector2f::x);
			Vec2Handle.RegisterMember("y", &Rml::Vector2f::y);
		}
		C.RegisterArray<Rml::Vector<Rml::Vector2f>>();

		C.Bind("hello_world", &Events.HelloWorld);
		C.Bind("mouse_detector", &Events.MouseDetector);
		C.Bind("rating", &Events.Rating);
		C.Bind("list", &Events.List);
		C.Bind("positions", &Events.Positions);

		C.BindFunc("good_rating",
			[this](Rml::Variant& Variant) { Variant = int(Events.Rating > 50); });
		C.BindFunc("great_rating",
			[this](Rml::Variant& Variant) { Variant = int(Events.Rating > 80); });

		C.BindEventCallback("add_mouse_pos",
			[this](Rml::DataModelHandle Model, Rml::Event& Ev, const Rml::VariantList&)
			{
				Events.Positions.emplace_back(
					Ev.GetParameter("mouse_x", 0.f),
					Ev.GetParameter("mouse_y", 0.f));
				Model.DirtyVariable("positions");
			});

		C.BindEventCallback("clear_positions",
			[this](Rml::DataModelHandle Model, Rml::Event&, const Rml::VariantList&)
			{
				Events.Positions.clear();
				Model.DirtyVariable("positions");
			});

		EventsHandle = C.GetModelHandle();
	}

	// ---- Invaders ----
	if (Rml::DataModelConstructor C = InContext->CreateDataModel("invaders"))
	{
		C.RegisterScalar<Rml::Colourb>(
			[](const Rml::Colourb& Color, Rml::Variant& Variant) { Variant = Rml::ToString(Color); });

		C.RegisterTransformFunc("format_time",
			[](const Rml::VariantList& Args) -> Rml::Variant
			{
				if (Args.empty())
					return {};
				const double T = Args[0].Get<double>();
				const int Minutes = int(T) / 60;
				const double Seconds = T - 60.0 * double(Minutes);
				return Rml::Variant(Rml::CreateString("%02d:%05.2f", Minutes, Seconds));
			});

		if (auto InvaderHandle = C.RegisterStruct<FInvader>())
		{
			InvaderHandle.RegisterMember("name", &FInvader::Name);
			InvaderHandle.RegisterMember("sprite", &FInvader::Sprite);
			InvaderHandle.RegisterMember("color", &FInvader::Color);
			InvaderHandle.RegisterMember("max_health", &FInvader::MaxHealth);
			InvaderHandle.RegisterMember("charge_rate", &FInvader::ChargeRate);
			InvaderHandle.RegisterMember("health", &FInvader::Health);
			InvaderHandle.RegisterMember("charge", &FInvader::Charge);
		}
		C.RegisterArray<decltype(Invaders.Invaders)>();

		C.Bind("invaders", &Invaders.Invaders);
		C.Bind("health", &Invaders.Health);
		C.Bind("charge", &Invaders.Charge);
		C.Bind("score", &Invaders.Score);
		C.Bind("elapsed_time", &Invaders.ElapsedTime);
		C.Bind("num_games_played", &Invaders.NumGamesPlayed);

		C.BindEventCallback("start_game",
			[this](Rml::DataModelHandle Model, Rml::Event&, const Rml::VariantList&)
			{
				Invaders.Health = 100;
				Invaders.Charge = 30;
				Invaders.Score = 0;
				Invaders.ElapsedTime = 0;
				Invaders.NextInvaderSpawnTime = 0;
				Invaders.NumGamesPlayed += 1;

				for (FInvader& Inv : Invaders.Invaders)
					Inv.Health = 0;

				Model.DirtyVariable("health");
				Model.DirtyVariable("charge");
				Model.DirtyVariable("score");
				Model.DirtyVariable("elapsed_time");
				Model.DirtyVariable("num_games_played");
				Model.DirtyVariable("invaders");
			});

		C.BindEventCallback("fire",
			[this](Rml::DataModelHandle Model, Rml::Event&, const Rml::VariantList& Args)
			{
				if (Args.size() != 1)
					return;
				const size_t Index = Args[0].Get<size_t>();
				if (Index >= Invaders.Invaders.size())
					return;

				FInvader& Inv = Invaders.Invaders[Index];
				if (Invaders.Health <= 0 || Inv.Health <= 0)
					return;

				const float NewHealth = Rml::Math::Max(
					Inv.Health - Invaders.Charge * Rml::Math::SquareRoot(Invaders.Charge), 0.0f);

				Invaders.Charge = 30.f;
				Invaders.Score += int(Inv.Health - NewHealth) + 1000 * (NewHealth == 0);
				Inv.Health = NewHealth;

				Model.DirtyVariable("invaders");
				Model.DirtyVariable("charge");
				Model.DirtyVariable("score");
			});

		InvadersHandle = C.GetModelHandle();
	}

	// ---- Forms ----
	if (Rml::DataModelConstructor C = InContext->CreateDataModel("forms"))
	{
		C.RegisterArray<Rml::Vector<Rml::String>>();

		C.Bind("rating", &Forms.Rating);
		C.Bind("pizza", &Forms.bPizza);
		C.Bind("pasta", &Forms.bPasta);
		C.Bind("lasagne", &Forms.bLasagne);
		C.Bind("animal", &Forms.Animal);
		C.Bind("subjects", &Forms.Subjects);
		C.Bind("selected_subject", &Forms.SelectedSubject);
		C.Bind("new_subject", &Forms.NewSubject);

		C.BindEventCallback("add_subject",
			[this](Rml::DataModelHandle Model, Rml::Event&, const Rml::VariantList& Args)
			{
				Rml::String Name = (Args.size() == 1 ? Args[0].Get<Rml::String>() : "");
				if (!Name.empty())
				{
					Forms.Subjects.push_back(std::move(Name));
					Model.DirtyVariable("subjects");
				}
			});

		C.BindEventCallback("erase_subject",
			[this](Rml::DataModelHandle Model, Rml::Event&, const Rml::VariantList& Args)
			{
				const int I = (Args.size() == 1 ? Args[0].Get<int>(-1) : -1);
				if (I >= 0 && I < (int)Forms.Subjects.size())
				{
					Forms.Subjects.erase(Forms.Subjects.begin() + I);
					Forms.SelectedSubject = 0;
					Model.DirtyVariable("subjects");
					Model.DirtyVariable("selected_subject");
				}
			});
	}
}

void URmlDataBinding::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// --- Events: propagate rating dirtiness ---
	if (EventsHandle && EventsHandle.IsVariableDirty("rating"))
	{
		EventsHandle.DirtyVariable("good_rating");
		EventsHandle.DirtyVariable("great_rating");

		size_t NewSize = Events.Rating / 10 + 1;
		if (NewSize != Events.List.size())
		{
			Events.List.resize(NewSize);
			std::iota(Events.List.begin(), Events.List.end(), float(NewSize));
			EventsHandle.DirtyVariable("list");
		}
	}

	// --- Invaders: game update ---
	if (!InvadersHandle || Invaders.Health <= 0)
		return;

	const double Dt = Rml::Math::Min((double)DeltaTime, 0.1);

	Invaders.ElapsedTime += Dt;
	InvadersHandle.DirtyVariable("elapsed_time");

	Invaders.Charge = Rml::Math::Min(Invaders.Charge + float(40.0 * Dt), 100.f);
	InvadersHandle.DirtyVariable("charge");

	// Spawn new invaders.
	if (Invaders.ElapsedTime >= Invaders.NextInvaderSpawnTime)
	{
		constexpr int NumItems = 4;
		static const Rml::Array<Rml::String, NumItems> Names = {"Angry invader", "Harmless invader", "Deceitful invader", "Cute invader"};
		static const Rml::Array<Rml::String, NumItems> Sprites = {"icon-invader", "icon-flag", "icon-game", "icon-waves"};
		static const Rml::Array<Rml::Colourb, NumItems> Colors = {{{255, 40, 30}, {20, 40, 255}, {255, 255, 30}, {230, 230, 230}}};

		FInvader NewInvader;
		NewInvader.Name = Names[Rml::Math::RandomInteger(NumItems)];
		NewInvader.Sprite = Sprites[Rml::Math::RandomInteger(NumItems)];
		NewInvader.Color = Colors[Rml::Math::RandomInteger(NumItems)];
		NewInvader.MaxHealth = 300.f + float(30.0 * Invaders.ElapsedTime) + Rml::Math::RandomReal(300.f);
		NewInvader.ChargeRate = 10.f + Rml::Math::RandomReal(50.f);
		NewInvader.Health = NewInvader.MaxHealth;

		const int IBegin = Rml::Math::RandomInteger(NumInvaders);
		for (int I = 0; I < NumInvaders; I++)
		{
			FInvader& Inv = Invaders.Invaders[(I + IBegin) % NumInvaders];
			if (Inv.Health <= 0)
			{
				Inv = std::move(NewInvader);
				InvadersHandle.DirtyVariable("invaders");
				break;
			}
		}

		Invaders.NextInvaderSpawnTime = Invaders.ElapsedTime + 60.0 / (IncomingInvadersRate + 0.1 * Invaders.ElapsedTime);
	}

	// Invaders fire at the player.
	for (FInvader& Inv : Invaders.Invaders)
	{
		if (Inv.Health > 0)
		{
			Inv.Charge = Inv.Charge + Inv.ChargeRate * float(Dt);

			if (Inv.Charge >= 100)
			{
				Invaders.Health = Rml::Math::Max(Invaders.Health - float(10.0 * Dt), 0.0f);
				InvadersHandle.DirtyVariable("health");
			}

			if (Inv.Charge >= 120)
				Inv.Charge = 0;

			InvadersHandle.DirtyVariable("invaders");
		}
	}
}
