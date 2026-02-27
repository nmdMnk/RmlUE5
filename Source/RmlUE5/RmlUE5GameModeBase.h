#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RmlInterface/UERmlFileInterface.h"
#include "RmlInterface/UERmlRenderInterface.h"
#include "RmlInterface/UERmlSystemInterface.h"
#include "RmlUE5GameModeBase.generated.h"

class SRmlWidget;
class URmlDocument;
class URmlDemo;
class URmlBenchmark;
class URmlAnimation;
class URmlTransform;
class URmlEffects;
class URmlDrag;
class URmlDataBinding;
class URmlMockupInventory;
namespace Rml { class Context; }

UCLASS()
class RMLUE5_API ARmlUE5GameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARmlUE5GameModeBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	void _LoadDemos(const FString& InBasePath);
	void _ChangeShowItem(URmlDocument* InDocument);
	static void _SetDocumentTitle(URmlDocument* InDocument);

	// !Begin Event
	UFUNCTION()
	void OpenDemo();
	UFUNCTION()
	void OpenBenchMark();
	UFUNCTION()
	void OpenAnimation();
	UFUNCTION()
	void OpenTransform();
	UFUNCTION()
	void OpenSprites();
	UFUNCTION()
	void OpenEffects();
	UFUNCTION()
	void OpenDrag();
	UFUNCTION()
	void OpenDataBinding();
	UFUNCTION()
	void OpenMockupInventory();
	UFUNCTION()
	void CloseDemo();
	// !End Event

	FUERmlFileInterface			RmlFileInterface;
	FUERmlSystemInterface		RmlSystemInterface;
	FUERmlRenderInterface		RmlRenderInterface;
	Rml::Context*				Context = nullptr;

#if WITH_EDITOR
	// Saved in BeginPlay, restored in EndPlay so URmlUiWidget stays functional after PIE.
	Rml::FileInterface*			PrevFileInterface   = nullptr;
	Rml::SystemInterface*		PrevSystemInterface = nullptr;
	Rml::RenderInterface*		PrevRenderInterface = nullptr;
#endif
	TSharedPtr<SRmlWidget>		RmlWidget;

	UPROPERTY()
	TObjectPtr<URmlDocument>	DemoSelector;

	UPROPERTY()
	TObjectPtr<URmlDemo>		MainDemo;

	UPROPERTY()
	TObjectPtr<URmlBenchmark>	BenchMark;

	UPROPERTY()
	TObjectPtr<URmlAnimation>	Animation;

	UPROPERTY()
	TObjectPtr<URmlTransform>	Transform;

	UPROPERTY()
	TObjectPtr<URmlDocument>	Sprites;

	UPROPERTY()
	TObjectPtr<URmlEffects>		Effects;

	UPROPERTY()
	TObjectPtr<URmlDrag>		Drag;

	UPROPERTY()
	TObjectPtr<URmlDataBinding>	DataBinding;

	UPROPERTY()
	TObjectPtr<URmlMockupInventory>	MockupInventory;

	UPROPERTY()
	TObjectPtr<URmlDocument>	CurrentElement;
};
