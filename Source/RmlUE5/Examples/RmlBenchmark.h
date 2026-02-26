#pragma once
#include "RmlDocument.h"
#include "RmlBenchmark.generated.h"

UCLASS()
class URmlBenchmark : public URmlDocument
{
	GENERATED_BODY()
protected:
	// ~Begin URmlDocument API
	virtual void OnInit() override;
	virtual void OnKeyDown() override;
	virtual void Tick(float DeltaTime) override;
	// ~End URmlDocument API

public:
	bool bDoPerformanceTest = false;

private:
	void PerformanceTest();
	bool bRunUpdate = true;
	bool bSingleUpdate = true;
};
