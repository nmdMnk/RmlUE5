#pragma once
#include "CoreMinimal.h"
#include "RmlDocument.h"
#include "RmlUi/Core/Types.h"
#include "RmlUi/Core/DataModelHandle.h"
#include "RmlDataBinding.generated.h"

UCLASS()
class URmlDataBinding : public URmlDocument
{
	GENERATED_BODY()
public:
	/** Must be called BEFORE Init() -- data models must exist before LoadDocument. */
	void CreateDataModels(Rml::Context* InContext);

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// --- Basics model ---
	struct FBasicsData
	{
		Rml::String Title = "Simple data binding example";
		Rml::String Animal = "dog";
		bool bShowText = true;
	} Basics;

	// --- Events model ---
	struct FEventsData
	{
		Rml::String HelloWorld = "Hello World!";
		Rml::String MouseDetector = "Mouse-move <em>Detector</em>.";
		int Rating = 99;
		Rml::Vector<float> List = {1, 2, 3, 4, 5};
		Rml::Vector<Rml::Vector2f> Positions;
	} Events;
	Rml::DataModelHandle EventsHandle;

	// --- Invaders model ---
	static constexpr int NumInvaders = 12;
	static constexpr double IncomingInvadersRate = 50.0;

	struct FInvader
	{
		Rml::String Name;
		Rml::String Sprite;
		Rml::Colourb Color{255, 255, 255};
		float MaxHealth = 0;
		float ChargeRate = 0;
		float Health = 0;
		float Charge = 0;
	};

	struct FInvadersData
	{
		float Health = 0;
		float Charge = 0;
		int Score = 0;
		double ElapsedTime = 0;
		double NextInvaderSpawnTime = 0;
		int NumGamesPlayed = 0;
		Rml::Array<FInvader, NumInvaders> Invaders;
	} Invaders;
	Rml::DataModelHandle InvadersHandle;

	// --- Forms model ---
	struct FFormsData
	{
		int Rating = 50;
		bool bPizza = true;
		bool bPasta = false;
		bool bLasagne = false;
		Rml::String Animal = "dog";
		Rml::Vector<Rml::String> Subjects = {"Choose your subject", "Feature request", "Bug report", "Praise", "Criticism"};
		int SelectedSubject = 0;
		Rml::String NewSubject = "New subject";
	} Forms;
};
