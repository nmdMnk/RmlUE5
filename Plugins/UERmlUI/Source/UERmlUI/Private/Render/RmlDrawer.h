#pragma once

#include "RmlShader.h"
#include "RmlUi/Core/RenderInterface.h"

class FRmlMesh;
class FRmlTextureEntry;
using FRmlTextureEntryPtr = TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>;

// ============================================================================
// Command buffer types
// ============================================================================

enum class ERmlCommand : uint8
{
	DrawMesh,
	EnableClipMask,
	RenderToClipMask,
	PushLayer,
	CompositeLayers,
	PopLayer,
	DrawShader,
	SaveLayerAsTexture,
	SaveLayerAsMaskImage,
};

// ============================================================================
// Compiled filter/shader data
// ============================================================================

enum class ERmlFilterType : uint8
{
	Invalid,
	Passthrough,	// opacity
	Blur,
	DropShadow,
	ColorMatrix,
	MaskImage,
};

struct FCompiledRmlFilter
{
	ERmlFilterType Type = ERmlFilterType::Invalid;
	float BlendFactor = 1.0f;
	float Sigma = 0.0f;
	FLinearColor Color = FLinearColor::Black;
	FVector2f Offset = FVector2f::ZeroVector;
	FMatrix44f ColorMatrix = FMatrix44f::Identity;
	FTextureRHIRef MaskTexture;
};

enum class ERmlShaderType : uint8
{
	Invalid,
	Gradient,
	Creation,
};

enum class ERmlGradientFunc : uint8
{
	Linear,
	Radial,
	Conic,
	RepeatingLinear,
	RepeatingRadial,
	RepeatingConic,
};

struct FCompiledRmlShader
{
	ERmlShaderType Type = ERmlShaderType::Invalid;
	ERmlGradientFunc GradientFunc = ERmlGradientFunc::Linear;
	FVector2f P = FVector2f::ZeroVector;
	FVector2f V = FVector2f::ZeroVector;
	TArray<float> StopPositions;
	TArray<FLinearColor> StopColors;
};

struct FRmlDrawCommand
{
	ERmlCommand Type;

	// DrawMesh / RenderToClipMask / DrawShader
	TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> Mesh;
	FRmlTextureEntryPtr Texture;
	FMatrix44f Transform;
	FIntRect ScissorRect;

	// Filled by DrawRenderThread pre-pass: offsets into the frame-level shared VB/IB.
	// BaseVertex is added to each index by the GPU; StartIndex is the first index to read.
	int32 BaseVertex = 0;
	int32 StartIndex = 0;

	// RenderToClipMask
	Rml::ClipMaskOperation ClipOp = Rml::ClipMaskOperation::Set;

	// EnableClipMask
	bool bClipMaskEnable = false;

	// PushLayer / CompositeLayers / PopLayer
	int32 SourceLayer = -1;
	int32 DestLayer = -1;
	Rml::BlendMode BlendMode = Rml::BlendMode::Blend;
	TArray<TSharedPtr<FCompiledRmlFilter>> Filters;		// resolved at recording time

	// DrawShader
	Rml::CompiledShaderHandle ShaderHandle = 0;

	// SaveLayerAsTexture / SaveLayerAsMaskImage
	FRmlTextureEntryPtr SavedTextureTarget;				// render thread sets OverrideRHI
	FCompiledRmlFilter* SavedMaskTarget = nullptr;		// render thread sets MaskTexture
	FIntPoint SaveTextureIdealSize = FIntPoint::ZeroValue;	// exact physical size matching GPU rasterizer

	// Factory methods
	static FRmlDrawCommand MakeDrawMesh(
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InMesh,
		FRmlTextureEntryPtr InTexture,
		const FMatrix44f& InTransform,
		const FIntRect& InScissorRect)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::DrawMesh;
		Cmd.Mesh = MoveTemp(InMesh);
		Cmd.Texture = MoveTemp(InTexture);
		Cmd.Transform = InTransform;
		Cmd.ScissorRect = InScissorRect;
		return Cmd;
	}

	static FRmlDrawCommand MakeEnableClipMask(bool bEnable)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::EnableClipMask;
		Cmd.bClipMaskEnable = bEnable;
		return Cmd;
	}

	static FRmlDrawCommand MakeRenderToClipMask(
		Rml::ClipMaskOperation Op,
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InMesh,
		const FMatrix44f& InTransform,
		const FIntRect& InScissorRect)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::RenderToClipMask;
		Cmd.ClipOp = Op;
		Cmd.Mesh = MoveTemp(InMesh);
		Cmd.Transform = InTransform;
		Cmd.ScissorRect = InScissorRect;
		return Cmd;
	}

	static FRmlDrawCommand MakePushLayer(int32 LayerIndex)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::PushLayer;
		Cmd.DestLayer = LayerIndex;
		return Cmd;
	}

	static FRmlDrawCommand MakeCompositeLayers(
		int32 Src, int32 Dst,
		Rml::BlendMode Mode,
		TArray<TSharedPtr<FCompiledRmlFilter>>&& InFilters,
		const FIntRect& InScissorRect)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::CompositeLayers;
		Cmd.SourceLayer = Src;
		Cmd.DestLayer = Dst;
		Cmd.BlendMode = Mode;
		Cmd.Filters = MoveTemp(InFilters);
		Cmd.ScissorRect = InScissorRect;
		return Cmd;
	}

	static FRmlDrawCommand MakePopLayer()
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::PopLayer;
		return Cmd;
	}

	static FRmlDrawCommand MakeDrawShader(
		Rml::CompiledShaderHandle InShader,
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InMesh,
		FRmlTextureEntryPtr InTexture,
		const FMatrix44f& InTransform,
		const FIntRect& InScissorRect)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::DrawShader;
		Cmd.ShaderHandle = InShader;
		Cmd.Mesh = MoveTemp(InMesh);
		Cmd.Texture = MoveTemp(InTexture);
		Cmd.Transform = InTransform;
		Cmd.ScissorRect = InScissorRect;
		return Cmd;
	}

	static FRmlDrawCommand MakeSaveLayerAsTexture(FRmlTextureEntryPtr Target, const FIntRect& InScissorRect, const FIntPoint& InIdealSize)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::SaveLayerAsTexture;
		Cmd.SavedTextureTarget = MoveTemp(Target);
		Cmd.ScissorRect = InScissorRect;
		Cmd.SaveTextureIdealSize = InIdealSize;
		return Cmd;
	}

	static FRmlDrawCommand MakeSaveLayerAsMaskImage(FCompiledRmlFilter* Target, const FIntRect& InScissorRect)
	{
		FRmlDrawCommand Cmd;
		Cmd.Type = ERmlCommand::SaveLayerAsMaskImage;
		Cmd.SavedMaskTarget = Target;
		Cmd.ScissorRect = InScissorRect;
		return Cmd;
	}
};

// ============================================================================
// Layer stack — manages per-layer render targets
// ============================================================================

struct FRmlLayer
{
	FTextureRHIRef ColorRT;		// MSAA or 1x
};

class FRmlLayerStack
{
public:
	void EnsureResources(FRHICommandListImmediate& RHICmdList, const FIntPoint& Size, EPixelFormat Format, int32 NumSamples);
	void Reset();

	// Push a new layer, returning its index
	int32 Push(FRHICommandListImmediate& RHICmdList);
	// Pop top layer
	void Pop();

	FTextureRHIRef GetColorRT(int32 Index) const { return Layers[Index].ColorRT; }
	void SetColorRT(int32 Index, FTextureRHIRef RT) { Layers[Index].ColorRT = RT; }
	FTextureRHIRef GetTopColorRT() const { return Layers[ActiveCount - 1].ColorRT; }
	int32 GetTopIndex() const { return ActiveCount - 1; }
	int32 GetActiveCount() const { return ActiveCount; }

	// Shared resources
	FTextureRHIRef SharedDepthStencil;

	// Postprocess buffers (1x, shader-readable)
	FTextureRHIRef PostprocessA;
	FTextureRHIRef PostprocessB;
	FTextureRHIRef PostprocessTemp;		// blur temp (FP16)
	FTextureRHIRef PostprocessBlurSrc;	// blur SourceDest (FP16) — paired with PostprocessTemp for all-FP16 blur chain
	FTextureRHIRef BlendMaskRT;			// SaveLayerAsMaskImage

private:
	TArray<FRmlLayer> Layers;
	int32 ActiveCount = 0;
	int32 CachedNumSamples = 0;
	FIntPoint CachedSize = FIntPoint::ZeroValue;
	EPixelFormat CachedFormat = PF_Unknown;

	void AllocLayer(FRHICommandListImmediate& RHICmdList, int32 Index);
};

// ============================================================================
// Main drawer — Slate custom element
// ============================================================================

class FRmlDrawer : public ICustomSlateElement
{
public:
	FRmlDrawer(bool bUsing = false);

	// ~Begin ICustomSlateElement API
	virtual void DrawRenderThread(FRHICommandListImmediate& RHICmdList, const void* RenderTarget) override;
	// ~End ICustomSlateElement API

	// Game-thread command recording
	FORCEINLINE void EmplaceMesh(
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InMesh,
		FRmlTextureEntryPtr InTexture,
		const FMatrix44f& InTransform,
		const FIntRect& InScissorRect)
	{
		CommandList.Add(FRmlDrawCommand::MakeDrawMesh(MoveTemp(InMesh), MoveTemp(InTexture), InTransform, InScissorRect));
	}

	void EmplaceEnableClipMask(bool bEnable)
	{
		CommandList.Add(FRmlDrawCommand::MakeEnableClipMask(bEnable));
	}

	void EmplaceRenderToClipMask(
		Rml::ClipMaskOperation Op,
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InMesh,
		const FMatrix44f& InTransform,
		const FIntRect& InScissorRect)
	{
		CommandList.Add(FRmlDrawCommand::MakeRenderToClipMask(Op, MoveTemp(InMesh), InTransform, InScissorRect));
	}

	void EmplacePushLayer(int32 LayerIndex)
	{
		CommandList.Add(FRmlDrawCommand::MakePushLayer(LayerIndex));
	}

	void EmplaceCompositeLayers(
		int32 Src, int32 Dst,
		Rml::BlendMode Mode,
		TArray<TSharedPtr<FCompiledRmlFilter>>&& Filters,
		const FIntRect& InScissorRect)
	{
		CommandList.Add(FRmlDrawCommand::MakeCompositeLayers(Src, Dst, Mode, MoveTemp(Filters), InScissorRect));
	}

	void EmplacePopLayer()
	{
		CommandList.Add(FRmlDrawCommand::MakePopLayer());
	}

	void EmplaceDrawShader(
		Rml::CompiledShaderHandle InShader,
		TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> InMesh,
		FRmlTextureEntryPtr InTexture,
		const FMatrix44f& InTransform,
		const FIntRect& InScissorRect)
	{
		CommandList.Add(FRmlDrawCommand::MakeDrawShader(InShader, MoveTemp(InMesh), MoveTemp(InTexture), InTransform, InScissorRect));
	}

	void EmplaceSaveLayerAsTexture(FRmlTextureEntryPtr Target, const FIntRect& InScissorRect, const FIntPoint& InIdealSize)
	{
		CommandList.Add(FRmlDrawCommand::MakeSaveLayerAsTexture(MoveTemp(Target), InScissorRect, InIdealSize));
	}

	void EmplaceSaveLayerAsMaskImage(FCompiledRmlFilter* Target, const FIntRect& InScissorRect)
	{
		CommandList.Add(FRmlDrawCommand::MakeSaveLayerAsMaskImage(Target, InScissorRect));
	}

	bool IsFree() const { return bIsFree; }
	void MarkUsing() { bIsFree = false; }
	void MarkFree()  { bIsFree = true; }
	void SetMSAASamples(int32 Samples) { MSAASamples = FMath::Max(Samples, 1); bUseMSAA = MSAASamples > 1; }

	// Compiled shader storage (owned by RenderInterface, shared via pointer)
	TMap<Rml::CompiledShaderHandle, TSharedPtr<FCompiledRmlShader>>* CompiledShaders = nullptr;

private:
	TArray<FRmlDrawCommand>	CommandList;
	bool					bIsFree;
	bool					bUseMSAA = true;
	int32					MSAASamples = 4;

	// Render resources
	FRmlLayerStack	LayerStack;
	FTextureRHIRef	ResolveTarget;		// always 1x, for final composite
	FBufferRHIRef	QuadVB;
	FBufferRHIRef	QuadIB;
	FIntPoint		CachedRTSize;

	// Per-frame shared geometry buffer. Accumulated from all mesh commands during the
	// pre-pass in DrawRenderThread. One RHICreateVertexBuffer/IndexBuffer per frame
	// replaces N×2 per-mesh allocations (main perf fix for benchmark).
	FBufferRHIRef	FrameVB;
	FBufferRHIRef	FrameIB;

	// Stencil state tracking
	bool			bClipMaskActive = false;
	int32			StencilRef = 0;

	void EnsureRenderResources(FRHICommandListImmediate& RHICmdList, const FIntPoint& RTSize, EPixelFormat RTFormat);

	// Pre-pass: accumulate all mesh vertices/indices into FrameVB/FrameIB and fill
	// BaseVertex/StartIndex in each DrawMesh/DrawShader/RenderToClipMask command.
	void BuildFrameGeometry(FRHICommandListImmediate& RHICmdList);

	// Command executors
	void ExecuteDrawMesh(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
		TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPs>& Ps, TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex,
		FGraphicsPipelineStateInitializer& PSOTex, FGraphicsPipelineStateInitializer& PSONoTex,
		FRmlTextureEntry*& CurTexture);

	void ExecuteEnableClipMask(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd);

	void ExecuteRenderToClipMask(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
		TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex);

	void ExecutePushLayer(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd, const FIntPoint& RTSize);

	void ExecuteCompositeLayers(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
		TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPs>& Ps, const FIntPoint& RTSize);

	void ExecutePopLayer(FRHICommandListImmediate& RHICmdList, const FIntPoint& RTSize);

	void ExecuteDrawShader(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
		TShaderMapRef<FRmlShaderVs>& Vs, const FIntPoint& RTSize);

	void ExecuteSaveLayerAsTexture(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd, const FIntPoint& RTSize);
	void ExecuteSaveLayerAsMaskImage(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd, const FIntPoint& RTSize);

	// Helpers
	void BeginLayerRenderPass(FRHICommandListImmediate& RHICmdList, int32 LayerIndex, const FIntPoint& RTSize,
		ERenderTargetLoadAction LoadAction, bool bClearStencil = false);
	void EndCurrentRenderPass(FRHICommandListImmediate& RHICmdList);
	void DrawFullscreenQuad(FRHICommandListImmediate& RHICmdList);
	// Returns pointer to the buffer containing the result (PostprocessA or PostprocessB).
	// ScissorRect: padded element bounds — all filter passes are scissored to this rect.
	FTextureRHIRef* ApplyFilters(FRHICommandListImmediate& RHICmdList, const TArray<TSharedPtr<FCompiledRmlFilter>>& Filters,
		TShaderMapRef<FRmlShaderVs>& Vs, const FIntPoint& RTSize, const FIntRect& ScissorRect);
	void RenderBlur(FRHICommandListImmediate& RHICmdList, float Sigma,
		FTextureRHIRef& SourceDest, FTextureRHIRef& Temp,
		TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPsBlur>& BlurPs,
		const FIntPoint& RTSize, const FIntRect& ScissorRect);

	// Rebuild PSOs with current stencil state
	void BuildDrawPSOs(FRHICommandListImmediate& RHICmdList,
		FGraphicsPipelineStateInitializer& OutPSOTex,
		FGraphicsPipelineStateInitializer& OutPSONoTex,
		TShaderMapRef<FRmlShaderVs>& Vs,
		TShaderMapRef<FRmlShaderPs>& Ps,
		TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex,
		FRHIBlendState* BlendState);

	bool bInRenderPass = false;
};
