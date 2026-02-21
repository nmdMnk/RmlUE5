#pragma once

#include "RmlShader.h"

class FRmlMesh;
class FRmlTextureEntry;

struct FRmlMeshDrawInfo
{
	FRmlMeshDrawInfo() : BoundTexture(nullptr) {}
	FRmlMeshDrawInfo(
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InBoundMesh,
		FRmlTextureEntry*                         InTexture,
		const FMatrix44f&                         InRenderTransform,
		const FIntRect&                           InScissorRect)
		: BoundMesh(InBoundMesh)
		, BoundTexture(InTexture)
		, RenderTransform(InRenderTransform)
		, ScissorRect(InScissorRect)
	{}

	TSharedPtr<FRmlMesh, ESPMode::ThreadSafe>	BoundMesh;
	FRmlTextureEntry*							BoundTexture;	// null if untextured
	FMatrix44f									RenderTransform;
	FIntRect									ScissorRect;
};

class FRmlDrawer : public ICustomSlateElement
{
public:
	FRmlDrawer(bool bUsing = false);

	// ~Begin ICustomSlateElement API
	virtual void DrawRenderThread(FRHICommandListImmediate& RHICmdList, const void* RenderTarget) override;
	// ~End ICustomSlateElement API

	FORCEINLINE void EmplaceMesh(
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InBoundMesh,
		FRmlTextureEntry*                         InTexture,
		const FMatrix44f&                         InRenderTransform,
		const FIntRect&                           InScissorRect)
	{
		DrawList.Emplace(InBoundMesh, InTexture, InRenderTransform, InScissorRect);
	}

	bool IsFree() const { return bIsFree; }
	void MarkUsing() { bIsFree = false; }
	void MarkFree()  { bIsFree = true; }
	void SetMSAA(bool bEnable) { bUseMSAA = bEnable; }
private:
	TArray<FRmlMeshDrawInfo>	DrawList;
	bool						bIsFree;
	bool						bUseMSAA = true;

	// MSAA resources (4x)
	FTextureRHIRef	MSAATarget;
	FTextureRHIRef	ResolveTarget;
	FBufferRHIRef	QuadVB;
	FBufferRHIRef	QuadIB;
	FIntPoint		CachedRTSize;

	void EnsureMSAAResources(FRHICommandListImmediate& RHICmdList, const FIntPoint& RTSize, EPixelFormat RTFormat);

	void DrawGeometry(
		FRHICommandListImmediate& RHICmdList,
		TShaderMapRef<FRmlShaderVs>& Vs,
		TShaderMapRef<FRmlShaderPs>& Ps,
		TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex,
		FRHIBlendState* BlendState);
};
