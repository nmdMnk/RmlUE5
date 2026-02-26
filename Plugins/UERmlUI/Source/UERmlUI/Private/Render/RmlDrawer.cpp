#include "RmlDrawer.h"
#include "RmlMesh.h"
#include "RmlShader.h"
#include "Render/TextureEntries.h"
#include "Logging.h"
#include "HAL/PlatformTime.h"

DECLARE_STATS_GROUP(TEXT("RmlUI_RT"), STATGROUP_RmlUI_RT, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("RmlUI DrawRenderThread"), STAT_RmlUI_DrawRenderThread, STATGROUP_RmlUI_RT);
DECLARE_CYCLE_STAT(TEXT("RmlUI DrawMesh"),         STAT_RmlUI_DrawMesh,         STATGROUP_RmlUI_RT);

// ============================================================================
// FRmlLayerStack
// ============================================================================

void FRmlLayerStack::EnsureResources(FRHICommandListImmediate& RHICmdList, const FIntPoint& Size, EPixelFormat Format, int32 NumSamples)
{
	// Force 8-bit RGBA for all internal render targets.  The Slate RT may use
	// A2B10G10R10 (10-bit color, but only 2-bit alpha = 4 alpha levels).
	// RmlUI layer compositing relies heavily on smooth alpha gradients (box-shadow
	// edges, blur falloff, drop-shadow) — 2-bit alpha produces hard staircases.
	// PF_B8G8R8A8 gives us full 8-bit alpha (256 levels).
	Format = PF_B8G8R8A8;

	if (CachedSize == Size && CachedFormat == Format && CachedNumSamples == NumSamples && SharedDepthStencil.IsValid())
		return;

	CachedSize = Size;
	CachedFormat = Format;
	CachedNumSamples = NumSamples;

	// Release all existing layers
	Layers.Reset();
	ActiveCount = 0;

	// Shared depth/stencil
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RmlUI_DepthStencil"), Size.X, Size.Y, PF_DepthStencil);
		Desc.NumSamples = NumSamples;
		Desc.Flags = ETextureCreateFlags::DepthStencilTargetable;
		Desc.ClearValue = FClearValueBinding::DepthFar;
		SharedDepthStencil = RHICmdList.CreateTexture(Desc);
	}

	// Postprocess buffers (always 1x, shader-readable)
	auto CreatePostprocess = [&](const TCHAR* Name) -> FTextureRHIRef
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(Name, Size.X, Size.Y, Format);
		Desc.Flags = ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource;
		Desc.ClearValue = FClearValueBinding::Transparent;
		return RHICmdList.CreateTexture(Desc);
	};

	PostprocessA = CreatePostprocess(TEXT("RmlUI_PostA"));
	PostprocessB = CreatePostprocess(TEXT("RmlUI_PostB"));
	// FP16 blur buffers — the entire downsample→blur→upsample chain runs in FP16
	// to avoid repeated 8-bit quantization of alpha gradients (box-shadow, filter:blur).
	// PostprocessBlurSrc + PostprocessTemp form a matched FP16 pair for RenderBlur.
	auto CreateFP16 = [&](const TCHAR* Name) -> FTextureRHIRef
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(Name, Size.X, Size.Y, PF_FloatRGBA);
		Desc.Flags = ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource;
		Desc.ClearValue = FClearValueBinding::Transparent;
		return RHICmdList.CreateTexture(Desc);
	};
	PostprocessTemp = CreateFP16(TEXT("RmlUI_PostTemp"));
	PostprocessBlurSrc = CreateFP16(TEXT("RmlUI_BlurSrc"));

	// BlendMask allocated lazily
	BlendMaskRT.SafeRelease();
}

void FRmlLayerStack::Reset()
{
	ActiveCount = 0;
}

int32 FRmlLayerStack::Push(FRHICommandListImmediate& RHICmdList)
{
	int32 Index = ActiveCount;
	ActiveCount++;

	if (Index >= Layers.Num())
	{
		Layers.AddDefaulted(1);
		AllocLayer(RHICmdList, Index);
	}
	else if (!Layers[Index].ColorRT.IsValid())
	{
		AllocLayer(RHICmdList, Index);
	}

	return Index;
}

void FRmlLayerStack::Pop()
{
	check(ActiveCount > 0);
	ActiveCount--;
}

void FRmlLayerStack::AllocLayer(FRHICommandListImmediate& RHICmdList, int32 Index)
{
	FString Name = FString::Printf(TEXT("RmlUI_Layer%d"), Index);
	FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(*Name, CachedSize.X, CachedSize.Y, CachedFormat);
	Desc.NumSamples = CachedNumSamples;
	// ShaderResource for ALL layers (including MSAA) — enables Texture2DMS binding
	// for scissored resolve in CompositeLayers, avoiding fullscreen hardware resolve.
	Desc.Flags = ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource;
	Desc.ClearValue = FClearValueBinding::Transparent;
	Layers[Index].ColorRT = RHICmdList.CreateTexture(Desc);
}

// ============================================================================
// FRmlDrawer
// ============================================================================

FRmlDrawer::FRmlDrawer(bool bUsing)
	: bIsFree(!bUsing)
	, CachedRTSize(0, 0)
{
}

void FRmlDrawer::EnsureRenderResources(FRHICommandListImmediate& RHICmdList, const FIntPoint& RTSize, EPixelFormat RTFormat)
{
	const int32 NumSamples = bUseMSAA ? MSAASamples : 1;

	if (CachedRTSize == RTSize && ResolveTarget.IsValid())
	{
		// Just ensure layer stack resources are up to date
		LayerStack.EnsureResources(RHICmdList, RTSize, RTFormat, NumSamples);
		return;
	}

	CachedRTSize = RTSize;

	// Resolve target — keeps Slate RT format so CopyTexture (format-matching) works in MSAA path.
	// Only used as a temporary copy of the Slate RT background, not for layer compositing.
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RmlUI_Resolve"), RTSize.X, RTSize.Y, RTFormat);
		Desc.Flags = ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource;
		Desc.ClearValue = FClearValueBinding::Transparent;
		ResolveTarget = RHICmdList.CreateTexture(Desc);
	}

	// Layer stack — internally forces PF_B8G8R8A8 for full 8-bit alpha
	LayerStack.EnsureResources(RHICmdList, RTSize, RTFormat, NumSamples);

	// Fullscreen quad VB/IB (only once, size-independent)
	if (!QuadVB.IsValid())
	{
		struct FQuadVert { FVector2f Pos; FColor Col; FVector2f UV; };

		TResourceArray<FQuadVert> VData;
		VData.SetNumUninitialized(4);
		VData[0] = { FVector2f(-1.f,  1.f), FColor(255,255,255,255), FVector2f(0.f, 0.f) };
		VData[1] = { FVector2f( 1.f,  1.f), FColor(255,255,255,255), FVector2f(1.f, 0.f) };
		VData[2] = { FVector2f( 1.f, -1.f), FColor(255,255,255,255), FVector2f(1.f, 1.f) };
		VData[3] = { FVector2f(-1.f, -1.f), FColor(255,255,255,255), FVector2f(0.f, 1.f) };

		FRHIResourceCreateInfo VInfo(TEXT("RmlUI_QuadVB"), &VData);
		QuadVB = RHICmdList.CreateVertexBuffer(sizeof(FQuadVert) * 4, BUF_Static, VInfo);

		TResourceArray<uint16> IData;
		IData.SetNumUninitialized(6);
		IData[0] = 0; IData[1] = 2; IData[2] = 1;
		IData[3] = 0; IData[4] = 3; IData[5] = 2;

		FRHIResourceCreateInfo IInfo(TEXT("RmlUI_QuadIB"), &IData);
		QuadIB = RHICmdList.CreateIndexBuffer(sizeof(uint16), sizeof(uint16) * 6, BUF_Static, IInfo);
	}
}

// ============================================================================
// Render pass helpers
// ============================================================================

void FRmlDrawer::BeginLayerRenderPass(FRHICommandListImmediate& RHICmdList, int32 LayerIndex, const FIntPoint& RTSize,
	ERenderTargetLoadAction LoadAction, bool bClearStencil)
{
	check(!bInRenderPass);

	FTextureRHIRef ColorRT = LayerStack.GetColorRT(LayerIndex);

	// Stencil action: only clear when explicitly requested (NOT tied to color clear).
	// The stencil buffer is shared across all layers — clearing it on PushLayer would
	// destroy active clip masks. GL3 reference: PushLayer only clears color, not stencil.
	FRHIRenderPassInfo PassInfo;
	PassInfo.ColorRenderTargets[0].RenderTarget = ColorRT;
	PassInfo.ColorRenderTargets[0].Action = MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore);
	PassInfo.DepthStencilRenderTarget.DepthStencilTarget = LayerStack.SharedDepthStencil;
	PassInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(
		MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction),
		bClearStencil
			? MakeRenderTargetActions(ERenderTargetLoadAction::EClear, ERenderTargetStoreAction::EStore)
			: MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore));
	PassInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;

	RHICmdList.BeginRenderPass(PassInfo, TEXT("RmlUI_Layer"));
	RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
	bInRenderPass = true;
}

void FRmlDrawer::EndCurrentRenderPass(FRHICommandListImmediate& RHICmdList)
{
	if (bInRenderPass)
	{
		RHICmdList.EndRenderPass();
		bInRenderPass = false;
	}
}

void FRmlDrawer::DrawFullscreenQuad(FRHICommandListImmediate& RHICmdList)
{
	RHICmdList.SetStreamSource(0, QuadVB, 0);
	RHICmdList.DrawIndexedPrimitive(QuadIB, 0, 0, 4, 0, 2, 1);
}

// ============================================================================
// PSO building
// ============================================================================

void FRmlDrawer::BuildDrawPSOs(FRHICommandListImmediate& RHICmdList,
	FGraphicsPipelineStateInitializer& OutPSOTex,
	FGraphicsPipelineStateInitializer& OutPSONoTex,
	TShaderMapRef<FRmlShaderVs>& Vs,
	TShaderMapRef<FRmlShaderPs>& Ps,
	TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex,
	FRHIBlendState* BlendState)
{
	// Premultiplied alpha blend
	FRHIDepthStencilState* DSS;
	if (bClipMaskActive)
	{
		// Stencil test: draw only where stencil == StencilRef
		DSS = TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			0xFF, 0x00>::GetRHI();
	}
	else
	{
		DSS = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	}

	// Build a base PSO, then copy with the alternate pixel shader.
	RHICmdList.ApplyCachedRenderTargets(OutPSOTex);
	OutPSOTex.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
	OutPSOTex.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
	OutPSOTex.BoundShaderState.PixelShaderRHI       = Ps.GetPixelShader();
	OutPSOTex.PrimitiveType     = PT_TriangleList;
	OutPSOTex.BlendState        = BlendState;
	OutPSOTex.RasterizerState   = TStaticRasterizerState<>::GetRHI();
	OutPSOTex.DepthStencilState = DSS;

	OutPSONoTex = OutPSOTex;
	OutPSONoTex.BoundShaderState.PixelShaderRHI = PsNoTex.GetPixelShader();
}

// ============================================================================
// Command executors
// ============================================================================

void FRmlDrawer::ExecuteDrawMesh(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
	TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPs>& Ps, TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex,
	FGraphicsPipelineStateInitializer& PSOTex, FGraphicsPipelineStateInitializer& PSONoTex,
	FRmlTextureEntry*& CurTexture)
{
	SCOPE_CYCLE_COUNTER(STAT_RmlUI_DrawMesh);
	FRmlTextureEntry* NewTexture = Cmd.Texture.Get();

	if (NewTexture != CurTexture)
	{
		if ((CurTexture != nullptr) != (NewTexture != nullptr))
			SetGraphicsPipelineState(RHICmdList, NewTexture ? PSOTex : PSONoTex, 0);

		if (NewTexture)
			Ps->SetParameters(RHICmdList, Ps.GetPixelShader(), NewTexture->GetTextureRHI(),
				NewTexture->bPremultiplied ? 0.0f : 1.0f, NewTexture->bWrapSampler,
				NewTexture->GetNonSRGBSRV(RHICmdList));

		CurTexture = NewTexture;
	}

	if (bClipMaskActive)
		RHICmdList.SetStencilRef(StencilRef);

	Vs->SetParameters(RHICmdList, Cmd.Transform);

	RHICmdList.SetScissorRect(
		true,
		Cmd.ScissorRect.Min.X,
		Cmd.ScissorRect.Min.Y,
		Cmd.ScissorRect.Max.X,
		Cmd.ScissorRect.Max.Y);

	RHICmdList.SetStreamSource(0, FrameVB, 0);
	RHICmdList.DrawIndexedPrimitive(FrameIB, Cmd.BaseVertex, 0, Cmd.Mesh->NumVertices, Cmd.StartIndex, Cmd.Mesh->NumTriangles, 1);
}

void FRmlDrawer::ExecuteEnableClipMask(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd)
{
	bClipMaskActive = Cmd.bClipMaskEnable;
	// PSOs will be rebuilt on next DrawMesh when stencil state changed
}

void FRmlDrawer::ExecuteRenderToClipMask(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
	TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex)
{
	// No color write — stencil only
	auto* NoColorWrite = TStaticBlendState<CW_NONE>::GetRHI();

	// Shared stencil-replace DSS — used by Set, SetInverse, and both sub-steps.
	FRHIDepthStencilState* ReplaceDSS = TStaticDepthStencilState<
		false, CF_Always,
		true, CF_Always, SO_Replace, SO_Replace, SO_Replace,
		false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
		0xFF, 0xFF>::GetRHI();

	// Build a reusable stencil-only PSO (no color write).
	FGraphicsPipelineStateInitializer StencilPSO;
	RHICmdList.ApplyCachedRenderTargets(StencilPSO);
	StencilPSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
	StencilPSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
	StencilPSO.BoundShaderState.PixelShaderRHI       = PsNoTex.GetPixelShader();
	StencilPSO.PrimitiveType     = PT_TriangleList;
	StencilPSO.BlendState        = NoColorWrite;
	StencilPSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
	StencilPSO.DepthStencilState = ReplaceDSS;

	switch (Cmd.ClipOp)
	{
	case Rml::ClipMaskOperation::Set:
	case Rml::ClipMaskOperation::SetInverse:
	{
		// Set: fill stencil with 0, write 1 where geometry is.
		// SetInverse: fill stencil with 1, write 0 where geometry is.
		const bool bInverse = (Cmd.ClipOp == Rml::ClipMaskOperation::SetInverse);
		const uint32 FillRef  = bInverse ? 1 : 0;
		const uint32 WriteRef = bInverse ? 0 : 1;

		// Step 1: Fill stencil via fullscreen quad
		SetGraphicsPipelineState(RHICmdList, StencilPSO, 0);
		RHICmdList.SetStencilRef(FillRef);
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
		DrawFullscreenQuad(RHICmdList);

		// Step 2: Write opposite value where geometry covers
		SetGraphicsPipelineState(RHICmdList, StencilPSO, 0);
		RHICmdList.SetStencilRef(WriteRef);
		Vs->SetParameters(RHICmdList, Cmd.Transform);
		RHICmdList.SetScissorRect(
			true,
			Cmd.ScissorRect.Min.X,
			Cmd.ScissorRect.Min.Y,
			Cmd.ScissorRect.Max.X,
			Cmd.ScissorRect.Max.Y);
		RHICmdList.SetStreamSource(0, FrameVB, 0);
		RHICmdList.DrawIndexedPrimitive(FrameIB, Cmd.BaseVertex, 0, Cmd.Mesh->NumVertices, Cmd.StartIndex, Cmd.Mesh->NumTriangles, 1);

		StencilRef = 1;
		break;
	}

	case Rml::ClipMaskOperation::Intersect:
	{
		// Increment stencil where geometry covers. Subsequent test uses ref+1.
		StencilRef++;

		StencilPSO.DepthStencilState = TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Always, SO_SaturatedIncrement, SO_SaturatedIncrement, SO_SaturatedIncrement,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			0xFF, 0xFF>::GetRHI();

		SetGraphicsPipelineState(RHICmdList, StencilPSO, 0);
		RHICmdList.SetStencilRef(0);
		Vs->SetParameters(RHICmdList, Cmd.Transform);
		RHICmdList.SetScissorRect(
			true,
			Cmd.ScissorRect.Min.X,
			Cmd.ScissorRect.Min.Y,
			Cmd.ScissorRect.Max.X,
			Cmd.ScissorRect.Max.Y);
		RHICmdList.SetStreamSource(0, FrameVB, 0);
		RHICmdList.DrawIndexedPrimitive(FrameIB, Cmd.BaseVertex, 0, Cmd.Mesh->NumVertices, Cmd.StartIndex, Cmd.Mesh->NumTriangles, 1);
		break;
	}
	}
}

void FRmlDrawer::ExecutePushLayer(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd, const FIntPoint& RTSize)
{
	EndCurrentRenderPass(RHICmdList);

	int32 NewIndex = LayerStack.Push(RHICmdList);
	check(NewIndex == Cmd.DestLayer);

	BeginLayerRenderPass(RHICmdList, NewIndex, RTSize, ERenderTargetLoadAction::EClear);
}

void FRmlDrawer::ExecuteCompositeLayers(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
	TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPs>& Ps, const FIntPoint& RTSize)
{
	EndCurrentRenderPass(RHICmdList);

	FTextureRHIRef SrcRT = LayerStack.GetColorRT(Cmd.SourceLayer);
	FTextureRHIRef DstRT = LayerStack.GetColorRT(Cmd.DestLayer);

	// Check if ALL filters are identity (will be skipped by ApplyFilters).
	// When true, the entire resolve → filter → composite pipeline can be replaced
	// with a single scissored composite pass, saving massive bandwidth at high res.
	bool bAllIdentity = true;
	for (const auto& F : Cmd.Filters)
	{
		if (!F) continue;
		switch (F->Type)
		{
		case ERmlFilterType::Passthrough:
			if (F->BlendFactor < 1.0f) bAllIdentity = false;
			break;
		case ERmlFilterType::Blur:
			if (F->Sigma >= 0.01f) bAllIdentity = false;
			break;
		case ERmlFilterType::ColorMatrix:
			if (!F->ColorMatrix.Equals(FMatrix44f::Identity, KINDA_SMALL_NUMBER)) bAllIdentity = false;
			break;
		default:
			bAllIdentity = false;
			break;
		}
		if (!bAllIdentity) break;
	}

	// --- Common blend and stencil state (shared by all paths) ---
	FRHIBlendState* CompBlend;
	if (Cmd.BlendMode == Rml::BlendMode::Replace)
		CompBlend = TStaticBlendState<>::GetRHI();
	else
		CompBlend = TStaticBlendState<CW_RGBA,
			BO_Add, BF_One, BF_InverseSourceAlpha,
			BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

	FRHIDepthStencilState* CompDSS;
	if (bClipMaskActive)
		CompDSS = TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			0xFF, 0x00>::GetRHI();
	else
		CompDSS = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	// ================================================================
	// FAST PATH: All filters are identity — single scissored pass.
	// Skips the separate MSAA resolve and PostprocessA detour entirely.
	// For MSAA: uses Texture2DMS manual resolve in the pixel shader.
	// For non-MSAA: samples the source layer directly (has ShaderResource).
	// Scissoring limits fill to the element bounds instead of fullscreen.
	// ================================================================
	if (bAllIdentity)
	{
		auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

		// Select pixel shader based on MSAA mode
		FRHIPixelShader* PixelShaderRHI;
		TShaderMapRef<FRmlShaderPsMsaaResolve> MsaaResolvePs(ShaderMap);
		TShaderMapRef<FRmlShaderPsPassthrough> PassPs(ShaderMap);
		if (bUseMSAA)
			PixelShaderRHI = MsaaResolvePs.GetPixelShader();
		else
			PixelShaderRHI = PassPs.GetPixelShader();

		// Begin render pass directly on dest layer
		FRHIRenderPassInfo CompPassInfo;
		CompPassInfo.ColorRenderTargets[0].RenderTarget = DstRT;
		CompPassInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Load_Store;
		CompPassInfo.DepthStencilRenderTarget.DepthStencilTarget = LayerStack.SharedDepthStencil;
		CompPassInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(
			MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction),
			MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore));
		CompPassInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
		RHICmdList.BeginRenderPass(CompPassInfo, TEXT("RmlUI_CompositeDirect"));
		RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
		bInRenderPass = true;

		FGraphicsPipelineStateInitializer PSO;
		RHICmdList.ApplyCachedRenderTargets(PSO);
		PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSO.BoundShaderState.PixelShaderRHI       = PixelShaderRHI;
		PSO.PrimitiveType     = PT_TriangleList;
		PSO.BlendState        = CompBlend;
		PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
		PSO.DepthStencilState = CompDSS;
		SetGraphicsPipelineState(RHICmdList, PSO, 0);

		if (bClipMaskActive)
			RHICmdList.SetStencilRef(StencilRef);

		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
		if (bUseMSAA)
			MsaaResolvePs->SetParameters(RHICmdList, PixelShaderRHI, SrcRT, MSAASamples);
		else
			PassPs->SetParameters(RHICmdList, PixelShaderRHI, SrcRT, 1.0f);

		// Identity filters have no ink overflow — scissor to element bounds.
		RHICmdList.SetScissorRect(true,
			Cmd.ScissorRect.Min.X, Cmd.ScissorRect.Min.Y,
			Cmd.ScissorRect.Max.X, Cmd.ScissorRect.Max.Y);

		DrawFullscreenQuad(RHICmdList);

		EndCurrentRenderPass(RHICmdList);
		BeginLayerRenderPass(RHICmdList, LayerStack.GetTopIndex(), RTSize, ERenderTargetLoadAction::ELoad);
		return;
	}

	// ================================================================
	// NORMAL PATH: Non-identity filters — scissored resolve → filter → composite.
	// All passes are scissored to the padded element bounds, reducing fill rate
	// from fullscreen (7.37M pixels at 5120p) to element size (~120K pixels).
	// ================================================================

	// Compute padded scissor that covers filter ink overflow (blur extent + shadow offset).
	// Pushed layers contain only the element's content — the rest is transparent.
	FIntRect PaddedScissor = Cmd.ScissorRect;
	for (const auto& F : Cmd.Filters)
	{
		if (!F) continue;
		if (F->Type == ERmlFilterType::Blur && F->Sigma >= 0.01f)
		{
			int32 Pad = FMath::CeilToInt(F->Sigma * 3.0f) + 1;
			PaddedScissor.Min -= FIntPoint(Pad, Pad);
			PaddedScissor.Max += FIntPoint(Pad, Pad);
		}
		else if (F->Type == ERmlFilterType::DropShadow)
		{
			int32 BlurPad = FMath::CeilToInt(F->Sigma * 3.0f) + 1;
			int32 OffPad = FMath::Max(FMath::CeilToInt(FMath::Abs(F->Offset.X)),
									  FMath::CeilToInt(FMath::Abs(F->Offset.Y)));
			int32 TotalPad = BlurPad + OffPad;
			PaddedScissor.Min -= FIntPoint(TotalPad, TotalPad);
			PaddedScissor.Max += FIntPoint(TotalPad, TotalPad);
		}
	}
	PaddedScissor.Min.X = FMath::Max(0, PaddedScissor.Min.X);
	PaddedScissor.Min.Y = FMath::Max(0, PaddedScissor.Min.Y);
	PaddedScissor.Max.X = FMath::Min(RTSize.X, PaddedScissor.Max.X);
	PaddedScissor.Max.Y = FMath::Min(RTSize.Y, PaddedScissor.Max.Y);

	UE_LOG(LogUERmlUI, Verbose, TEXT("CompositeLayers: RTSize=%dx%d Scissor=[%d,%d]-[%d,%d] Padded=[%d,%d]-[%d,%d] Filters=%d"),
		RTSize.X, RTSize.Y,
		Cmd.ScissorRect.Min.X, Cmd.ScissorRect.Min.Y, Cmd.ScissorRect.Max.X, Cmd.ScissorRect.Max.Y,
		PaddedScissor.Min.X, PaddedScissor.Min.Y, PaddedScissor.Max.X, PaddedScissor.Max.Y,
		Cmd.Filters.Num());

	// Step 1: Scissored resolve/blit source layer → PostprocessA.
	// Clear_Store fast-clears to transparent (hardware fast clear, ~zero cost),
	// then the scissored draw populates only the padded element region.
	// Zeros outside ensure blur/shadow kernels read transparent at boundaries.
	{
		auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
		TShaderMapRef<FRmlShaderPsMsaaResolve> MsaaResolvePs(ShaderMap);
		TShaderMapRef<FRmlShaderPsPassthrough> ResolvePassPs(ShaderMap);

		FRHIRenderPassInfo ResolvePassInfo(LayerStack.PostprocessA, ERenderTargetActions::Clear_Store);
		RHICmdList.BeginRenderPass(ResolvePassInfo, TEXT("RmlUI_ScissoredResolve"));
		RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
		RHICmdList.SetScissorRect(true, PaddedScissor.Min.X, PaddedScissor.Min.Y,
			PaddedScissor.Max.X, PaddedScissor.Max.Y);

		FRHIPixelShader* PixelShaderRHI = bUseMSAA
			? MsaaResolvePs.GetPixelShader() : ResolvePassPs.GetPixelShader();

		auto* NoBlend = TStaticBlendState<>::GetRHI();
		FGraphicsPipelineStateInitializer PSO;
		RHICmdList.ApplyCachedRenderTargets(PSO);
		PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSO.BoundShaderState.PixelShaderRHI       = PixelShaderRHI;
		PSO.PrimitiveType     = PT_TriangleList;
		PSO.BlendState        = NoBlend;
		PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
		PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		SetGraphicsPipelineState(RHICmdList, PSO, 0);

		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
		if (bUseMSAA)
			MsaaResolvePs->SetParameters(RHICmdList, PixelShaderRHI, SrcRT, MSAASamples);
		else
			ResolvePassPs->SetParameters(RHICmdList, PixelShaderRHI, SrcRT, 1.0f);
		DrawFullscreenQuad(RHICmdList);
		RHICmdList.EndRenderPass();
	}

	// Step 2: Apply filters (all passes scissored to PaddedScissor)
	FTextureRHIRef FilteredSource = LayerStack.PostprocessA;
	if (Cmd.Filters.Num() > 0)
	{
		FTextureRHIRef* Result = ApplyFilters(RHICmdList, Cmd.Filters, Vs, RTSize, PaddedScissor);
		FilteredSource = *Result;
	}

	// Step 3: Composite filtered source onto destination layer (scissored)
	FRHIRenderPassInfo CompPassInfo;
	CompPassInfo.ColorRenderTargets[0].RenderTarget = DstRT;
	CompPassInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Load_Store;
	CompPassInfo.DepthStencilRenderTarget.DepthStencilTarget = LayerStack.SharedDepthStencil;
	CompPassInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(
		MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction),
		MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore));
	CompPassInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
	RHICmdList.BeginRenderPass(CompPassInfo, TEXT("RmlUI_Composite"));
	RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
	bInRenderPass = true;

	auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FRmlShaderPsPassthrough> PassPs(ShaderMap);

	FGraphicsPipelineStateInitializer PSO;
	RHICmdList.ApplyCachedRenderTargets(PSO);
	PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
	PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
	PSO.BoundShaderState.PixelShaderRHI       = PassPs.GetPixelShader();
	PSO.PrimitiveType     = PT_TriangleList;
	PSO.BlendState        = CompBlend;
	PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
	PSO.DepthStencilState = CompDSS;

	SetGraphicsPipelineState(RHICmdList, PSO, 0);

	if (bClipMaskActive)
		RHICmdList.SetStencilRef(StencilRef);

	Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
	PassPs->SetParameters(RHICmdList, PassPs.GetPixelShader(), FilteredSource, 1.0f);

	// Scissor to padded element bounds — covers all filter ink overflow.
	// Transparent pixels outside compose correctly with premul blend (srcA=0 → no change).
	RHICmdList.SetScissorRect(true,
		PaddedScissor.Min.X, PaddedScissor.Min.Y,
		PaddedScissor.Max.X, PaddedScissor.Max.Y);

	DrawFullscreenQuad(RHICmdList);

	EndCurrentRenderPass(RHICmdList);
	BeginLayerRenderPass(RHICmdList, LayerStack.GetTopIndex(), RTSize, ERenderTargetLoadAction::ELoad);
}

void FRmlDrawer::ExecutePopLayer(FRHICommandListImmediate& RHICmdList, const FIntPoint& RTSize)
{
	EndCurrentRenderPass(RHICmdList);
	LayerStack.Pop();

	if (LayerStack.GetActiveCount() > 0)
	{
		BeginLayerRenderPass(RHICmdList, LayerStack.GetTopIndex(), RTSize, ERenderTargetLoadAction::ELoad);
	}
}

void FRmlDrawer::ExecuteSaveLayerAsTexture(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd, const FIntPoint& RTSize)
{
	if (!Cmd.SavedTextureTarget) return;

	const FIntRect& Region = Cmd.ScissorRect;
	// Use the ideal texture size (matches GPU rasterizer pixel count) instead of
	// the conservative scissor rect size (Floor/Ceil, can be 1px wider).
	const int32 W = (Cmd.SaveTextureIdealSize.X > 0) ? Cmd.SaveTextureIdealSize.X : Region.Width();
	const int32 H = (Cmd.SaveTextureIdealSize.Y > 0) ? Cmd.SaveTextureIdealSize.Y : Region.Height();
	if (W <= 0 || H <= 0) return;

	EndCurrentRenderPass(RHICmdList);

	FTextureRHIRef TopRT = LayerStack.GetTopColorRT();

	UE_LOG(LogUERmlUI, Verbose, TEXT("SaveLayerAsTexture: RTSize=%dx%d Scissor=[%d,%d]-[%d,%d] Ideal=%dx%d"),
		RTSize.X, RTSize.Y,
		Region.Min.X, Region.Min.Y, Region.Max.X, Region.Max.Y,
		W, H);

	// Resolve MSAA to 1x if needed
	FTextureRHIRef ResolvedRT;
	if (bUseMSAA)
	{
		FRHIRenderPassInfo ResolvePassInfo;
		ResolvePassInfo.ColorRenderTargets[0].RenderTarget = TopRT;
		ResolvePassInfo.ColorRenderTargets[0].ResolveTarget = LayerStack.PostprocessA;
		ResolvePassInfo.ColorRenderTargets[0].Action = MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EMultisampleResolve);
		RHICmdList.BeginRenderPass(ResolvePassInfo, TEXT("RmlUI_SaveLayerResolve"));
		RHICmdList.EndRenderPass();
		ResolvedRT = LayerStack.PostprocessA;
	}
	else
	{
		ResolvedRT = TopRT;
	}

	// Create a texture matching the scissor region
	const EPixelFormat Fmt = ResolvedRT->GetFormat();
	FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RmlUI_SavedLayer"), W, H, Fmt);
	Desc.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource);
	Desc.SetClearValue(FClearValueBinding::Transparent);
	FTextureRHIRef SavedRT = RHICmdList.CreateTexture(Desc);

	// Shader blit: render the scissor region from resolved layer into the saved texture.
	// Uses the Passthrough shader with UVOffset+UVScale to sample only the scissor region.
	// This goes through the full graphics pipeline (guaranteed resource transitions) instead
	// of CopyTexture which could have D3D12 barrier issues.
	{
		auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
		TShaderMapRef<FRmlShaderVs>            Vs(ShaderMap);
		TShaderMapRef<FRmlShaderPsPassthrough> PassPs(ShaderMap);

		FRHIRenderPassInfo PassInfo(SavedRT, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(PassInfo, TEXT("RmlUI_SaveLayerBlit"));
		RHICmdList.SetViewport(0, 0, 0.0f, (float)W, (float)H, 1.0f);
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		auto* NoBlend = TStaticBlendState<>::GetRHI();
		FGraphicsPipelineStateInitializer PSO;
		RHICmdList.ApplyCachedRenderTargets(PSO);
		PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSO.BoundShaderState.PixelShaderRHI       = PassPs.GetPixelShader();
		PSO.PrimitiveType     = PT_TriangleList;
		PSO.BlendState        = NoBlend;
		PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
		PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		SetGraphicsPipelineState(RHICmdList, PSO, 0);
		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);

		// Map fullscreen quad UV (0,0)→(1,1) to the ideal-sized region in the source texture
		// (starting at scissor Min, reading exactly W×H pixels = ideal size)
		const FVector2f UVScale(
			(float)W / (float)RTSize.X,
			(float)H / (float)RTSize.Y);
		const FVector2f UVOffset(
			(float)Region.Min.X / (float)RTSize.X,
			(float)Region.Min.Y / (float)RTSize.Y);
		PassPs->SetParameters(RHICmdList, PassPs.GetPixelShader(), ResolvedRT, 1.0f, UVScale, UVOffset);
		DrawFullscreenQuad(RHICmdList);

		RHICmdList.EndRenderPass();
	}

	// Store the RHI texture on the entry — subsequent draw commands will pick it up
	Cmd.SavedTextureTarget->OverrideRHI = SavedRT;

	// Resume rendering on the current top layer
	BeginLayerRenderPass(RHICmdList, LayerStack.GetTopIndex(), RTSize, ERenderTargetLoadAction::ELoad);
}

void FRmlDrawer::ExecuteSaveLayerAsMaskImage(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd, const FIntPoint& RTSize)
{
	if (!Cmd.SavedMaskTarget) return;

	EndCurrentRenderPass(RHICmdList);

	FTextureRHIRef TopRT = LayerStack.GetTopColorRT();

	// Resolve MSAA if needed
	FTextureRHIRef ResolvedRT;
	if (bUseMSAA)
	{
		FRHIRenderPassInfo ResolvePassInfo;
		ResolvePassInfo.ColorRenderTargets[0].RenderTarget = TopRT;
		ResolvePassInfo.ColorRenderTargets[0].ResolveTarget = LayerStack.PostprocessA;
		ResolvePassInfo.ColorRenderTargets[0].Action = MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EMultisampleResolve);
		RHICmdList.BeginRenderPass(ResolvePassInfo, TEXT("RmlUI_SaveMaskResolve"));
		RHICmdList.EndRenderPass();
		ResolvedRT = LayerStack.PostprocessA;
	}
	else
	{
		ResolvedRT = TopRT;
	}

	// Use full-size BlendMaskRT (matching GL3 reference) so UVs align with the
	// postprocess buffers during BlendMask filter application.
	if (!LayerStack.BlendMaskRT.IsValid()
		|| (int32)LayerStack.BlendMaskRT->GetSizeX() != RTSize.X
		|| (int32)LayerStack.BlendMaskRT->GetSizeY() != RTSize.Y)
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RmlUI_BlendMask"), RTSize.X, RTSize.Y, PF_B8G8R8A8);
		Desc.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource);
		Desc.SetClearValue(FClearValueBinding::Transparent);
		LayerStack.BlendMaskRT = RHICmdList.CreateTexture(Desc);
	}

	// Copy full layer to BlendMaskRT
	FRHICopyTextureInfo CopyInfo;
	RHICmdList.CopyTexture(ResolvedRT, LayerStack.BlendMaskRT, CopyInfo);

	// Store as the mask filter's texture
	Cmd.SavedMaskTarget->MaskTexture = LayerStack.BlendMaskRT;

	// Resume rendering
	BeginLayerRenderPass(RHICmdList, LayerStack.GetTopIndex(), RTSize, ERenderTargetLoadAction::ELoad);
}

void FRmlDrawer::ExecuteDrawShader(FRHICommandListImmediate& RHICmdList, const FRmlDrawCommand& Cmd,
	TShaderMapRef<FRmlShaderVs>& Vs, const FIntPoint& RTSize)
{
	if (!CompiledShaders || !CompiledShaders->Contains(Cmd.ShaderHandle))
		return;

	const FCompiledRmlShader& Shader = **CompiledShaders->Find(Cmd.ShaderHandle);

	auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FRmlShaderPsGradient> GradientPs(ShaderMap);
	TShaderMapRef<FRmlShaderPsNoTex> NoTexPs(ShaderMap);

	FRHIPixelShader* PixelShader = nullptr;
	switch (Shader.Type)
	{
	case ERmlShaderType::Gradient:
		PixelShader = GradientPs.GetPixelShader();
		break;
	case ERmlShaderType::Creation:
		// Stub: just render vertex color (white from RmlUI).
		PixelShader = NoTexPs.GetPixelShader();
		break;
	default:
		UE_LOG(LogUERmlUI, Warning, TEXT("ExecuteDrawShader: unsupported shader type %d"), (int32)Shader.Type);
		return;
	}

	FRHIBlendState* PremulBlend = TStaticBlendState<CW_RGBA,
		BO_Add, BF_One, BF_InverseSourceAlpha,
		BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

	FRHIDepthStencilState* DSS;
	if (bClipMaskActive)
	{
		DSS = TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			0xFF, 0x00>::GetRHI();
	}
	else
	{
		DSS = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	}

	FGraphicsPipelineStateInitializer PSO;
	RHICmdList.ApplyCachedRenderTargets(PSO);
	PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
	PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
	PSO.BoundShaderState.PixelShaderRHI       = PixelShader;
	PSO.PrimitiveType     = PT_TriangleList;
	PSO.BlendState        = PremulBlend;
	PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
	PSO.DepthStencilState = DSS;

	SetGraphicsPipelineState(RHICmdList, PSO, 0);

	if (bClipMaskActive)
		RHICmdList.SetStencilRef(StencilRef);

	Vs->SetParameters(RHICmdList, Cmd.Transform);
	switch (Shader.Type)
	{
	case ERmlShaderType::Gradient:
		GradientPs->SetParameters(RHICmdList, GradientPs.GetPixelShader(), Shader);
		break;
	case ERmlShaderType::Creation:
		// No parameters needed — NoTex PS just outputs vertex color.
		break;
	default:
		return;
	}

	RHICmdList.SetScissorRect(
		true,
		Cmd.ScissorRect.Min.X,
		Cmd.ScissorRect.Min.Y,
		Cmd.ScissorRect.Max.X,
		Cmd.ScissorRect.Max.Y);

	RHICmdList.SetStreamSource(0, FrameVB, 0);
	RHICmdList.DrawIndexedPrimitive(FrameIB, Cmd.BaseVertex, 0, Cmd.Mesh->NumVertices, Cmd.StartIndex, Cmd.Mesh->NumTriangles, 1);
}

// ============================================================================
// Filter pipeline
// ============================================================================

FTextureRHIRef* FRmlDrawer::ApplyFilters(FRHICommandListImmediate& RHICmdList, const TArray<TSharedPtr<FCompiledRmlFilter>>& Filters,
	TShaderMapRef<FRmlShaderVs>& Vs, const FIntPoint& RTSize, const FIntRect& ScissorRect)
{
	if (Filters.Num() == 0)
		return &LayerStack.PostprocessA;

	auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FRmlShaderPs>             TexPs(ShaderMap);
	TShaderMapRef<FRmlShaderPsPassthrough>  PassthroughPs(ShaderMap);
	TShaderMapRef<FRmlShaderPsBlur>         BlurPs(ShaderMap);
	TShaderMapRef<FRmlShaderPsDropShadow>   DropShadowPs(ShaderMap);
	TShaderMapRef<FRmlShaderPsColorMatrix>  ColorMatrixPs(ShaderMap);
	TShaderMapRef<FRmlShaderPsBlendMask>    BlendMaskPs(ShaderMap);

	// Current source/dest: ping-pong between PostprocessA and PostprocessB
	FTextureRHIRef* Src = &LayerStack.PostprocessA;
	FTextureRHIRef* Dst = &LayerStack.PostprocessB;

	auto SwapBuffers = [&]() { Swap(Src, Dst); };

	// Blit between textures via passthrough shader (handles format conversion, e.g. 8-bit↔FP16).
	// bClear=true: fast-clear target to transparent before scissored draw — required before
	// blur so that the kernel reads transparent (not undefined) outside the element bounds.
	auto BlitScissored = [&](FTextureRHIRef& ReadTex, FTextureRHIRef& WriteTex, const TCHAR* DebugName, bool bClear = false)
	{
		ERenderTargetActions Actions = bClear ? ERenderTargetActions::Clear_Store : ERenderTargetActions::DontLoad_Store;
		FRHIRenderPassInfo PassInfo(WriteTex, Actions);
		RHICmdList.BeginRenderPass(PassInfo, DebugName);
		RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
		RHICmdList.SetScissorRect(true, ScissorRect.Min.X, ScissorRect.Min.Y,
			ScissorRect.Max.X, ScissorRect.Max.Y);

		auto* NoBlend = TStaticBlendState<>::GetRHI();
		FGraphicsPipelineStateInitializer PSO;
		RHICmdList.ApplyCachedRenderTargets(PSO);
		PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSO.BoundShaderState.PixelShaderRHI       = PassthroughPs.GetPixelShader();
		PSO.PrimitiveType     = PT_TriangleList;
		PSO.BlendState        = NoBlend;
		PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
		PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		SetGraphicsPipelineState(RHICmdList, PSO, 0);
		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
		PassthroughPs->SetParameters(RHICmdList, PassthroughPs.GetPixelShader(), ReadTex, 1.0f);
		DrawFullscreenQuad(RHICmdList);
		RHICmdList.EndRenderPass();
	};

	auto BeginPostprocessPass = [&](FTextureRHIRef& Target)
	{
		FRHIRenderPassInfo PassInfo(Target, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(PassInfo, TEXT("RmlUI_Filter"));
		RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
		RHICmdList.SetScissorRect(true, ScissorRect.Min.X, ScissorRect.Min.Y,
			ScissorRect.Max.X, ScissorRect.Max.Y);
	};

	for (int32 FilterIdx = 0; FilterIdx < Filters.Num(); ++FilterIdx)
	{
		const TSharedPtr<FCompiledRmlFilter>& FilterPtr = Filters[FilterIdx];
		if (!FilterPtr) continue;
		const FCompiledRmlFilter& Filter = *FilterPtr;

		switch (Filter.Type)
		{
		case ERmlFilterType::Passthrough:
		{
			if (Filter.BlendFactor >= 1.0f)
				break; // Identity: opacity(1) has no visual effect, skip pass
			BeginPostprocessPass(*Dst);

			// Opaque replace — the shader already multiplies by BlendFactor, so the
			// output is the final filtered image. Premul blend here would leak stale
			// PostprocessB content through transparent areas (ghosting/permanent copies).
			auto* NoBlend = TStaticBlendState<>::GetRHI();

			FGraphicsPipelineStateInitializer PSO;
			RHICmdList.ApplyCachedRenderTargets(PSO);
			PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
			PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
			PSO.BoundShaderState.PixelShaderRHI       = PassthroughPs.GetPixelShader();
			PSO.PrimitiveType     = PT_TriangleList;
			PSO.BlendState        = NoBlend;
			PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
			PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, PSO, 0);
			Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
			PassthroughPs->SetParameters(RHICmdList, PassthroughPs.GetPixelShader(), *Src, Filter.BlendFactor);
			DrawFullscreenQuad(RHICmdList);

			RHICmdList.EndRenderPass();
			SwapBuffers();
			break;
		}

		case ERmlFilterType::Blur:
		{
			if (Filter.Sigma < 0.01f)
				break; // Skip no-op blur (edge case: animated values approaching 0)
			// GL3-style downsample → blur → upsample.
			// Run the entire chain in FP16 to avoid repeated 8-bit quantization
			// of alpha gradients (box-shadow, drop-shadow, filter:blur).
			// PostprocessBlurSrc + PostprocessTemp are both FP16.
			// bClear=true for BlurToFP16: ensures transparent outside scissor so blur
			// kernel reads zeros (not undefined) at the boundary.
			BlitScissored(*Src, LayerStack.PostprocessBlurSrc, TEXT("RmlUI_BlurToFP16"), /*bClear=*/true);
			RenderBlur(RHICmdList, Filter.Sigma, LayerStack.PostprocessBlurSrc, LayerStack.PostprocessTemp, Vs, BlurPs, RTSize, ScissorRect);
			BlitScissored(LayerStack.PostprocessBlurSrc, *Src, TEXT("RmlUI_BlurFromFP16"));
			// Result is back in *Src — no swap needed.
			break;
		}

		case ERmlFilterType::DropShadow:
		{
			// GL3 reference pattern: Primary (original) is never modified.
			// Shadow goes into Dst, blur modifies Dst in-place using Temp,
			// then original (Src) is drawn on top of shadow (Dst) with premul blend.

			FVector2f TexelSize(1.0f / (float)RTSize.X, 1.0f / (float)RTSize.Y);
			auto* NoBlend = TStaticBlendState<>::GetRHI();

			// Step 1: Draw shadow from Src into Dst (no swap — Src stays as original)
			{
				BeginPostprocessPass(*Dst);

				FGraphicsPipelineStateInitializer PSO;
				RHICmdList.ApplyCachedRenderTargets(PSO);
				PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
				PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
				PSO.BoundShaderState.PixelShaderRHI       = DropShadowPs.GetPixelShader();
				PSO.PrimitiveType     = PT_TriangleList;
				PSO.BlendState        = NoBlend;
				PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
				PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				SetGraphicsPipelineState(RHICmdList, PSO, 0);
				Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
				// Negate offset: shadow at (+30,+20) means we sample alpha from (-30,-20) in UV space.
			DropShadowPs->SetParameters(RHICmdList, DropShadowPs.GetPixelShader(), *Src, Filter.Color, -Filter.Offset * TexelSize);
				DrawFullscreenQuad(RHICmdList);
				RHICmdList.EndRenderPass();
			}

			// Step 2: Blur Dst in-place using GL3-style downsample → blur → upsample
			// Run in FP16 to avoid 8-bit quantization of alpha gradients.
			if (Filter.Sigma > 0.0f)
			{
				BlitScissored(*Dst, LayerStack.PostprocessBlurSrc, TEXT("RmlUI_DropShadowBlurToFP16"), /*bClear=*/true);
				RenderBlur(RHICmdList, Filter.Sigma, LayerStack.PostprocessBlurSrc, LayerStack.PostprocessTemp, Vs, BlurPs, RTSize, ScissorRect);
				BlitScissored(LayerStack.PostprocessBlurSrc, *Dst, TEXT("RmlUI_DropShadowBlurFromFP16"));
			}

			// Step 3: Overlay original (Src) on top of shadow (Dst) with premul blend
			// Begin render pass on Dst with Load to keep the shadow content
			{
				FRHIRenderPassInfo PassInfo(*Dst, ERenderTargetActions::Load_Store);
				RHICmdList.BeginRenderPass(PassInfo, TEXT("RmlUI_DropShadowOverlay"));
				RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
				RHICmdList.SetScissorRect(true, ScissorRect.Min.X, ScissorRect.Min.Y,
					ScissorRect.Max.X, ScissorRect.Max.Y);

				auto* PremulBlend = TStaticBlendState<CW_RGBA,
					BO_Add, BF_One, BF_InverseSourceAlpha,
					BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

				FGraphicsPipelineStateInitializer OPSO;
				RHICmdList.ApplyCachedRenderTargets(OPSO);
				OPSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
				OPSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
				OPSO.BoundShaderState.PixelShaderRHI       = TexPs.GetPixelShader();
				OPSO.PrimitiveType     = PT_TriangleList;
				OPSO.BlendState        = PremulBlend;
				OPSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
				OPSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				SetGraphicsPipelineState(RHICmdList, OPSO, 0);
				Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
				TexPs->SetParameters(RHICmdList, TexPs.GetPixelShader(), *Src, 0.0f);
				DrawFullscreenQuad(RHICmdList);
				RHICmdList.EndRenderPass();
			}

			// Swap so result (Dst with shadow+original) becomes Src
			SwapBuffers();
			break;
		}

		case ERmlFilterType::ColorMatrix:
		{
			// Merge consecutive ColorMatrix filters into a single matrix multiplication.
			// This turns N separate render passes into 1 when multiple color filters
			// (brightness, contrast, sepia, etc.) are active simultaneously.
			FMatrix44f MergedMatrix = Filter.ColorMatrix;
			while (FilterIdx + 1 < Filters.Num())
			{
				const TSharedPtr<FCompiledRmlFilter>& NextPtr = Filters[FilterIdx + 1];
				if (!NextPtr || NextPtr->Type != ERmlFilterType::ColorMatrix)
					break;
				MergedMatrix = MergedMatrix * NextPtr->ColorMatrix;
				FilterIdx++;	// skip merged filter
			}

			if (MergedMatrix.Equals(FMatrix44f::Identity, KINDA_SMALL_NUMBER))
				break; // Merged result is identity: no color transform, skip pass

			BeginPostprocessPass(*Dst);

			auto* NoBlend = TStaticBlendState<>::GetRHI();

			FGraphicsPipelineStateInitializer PSO;
			RHICmdList.ApplyCachedRenderTargets(PSO);
			PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
			PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
			PSO.BoundShaderState.PixelShaderRHI       = ColorMatrixPs.GetPixelShader();
			PSO.PrimitiveType     = PT_TriangleList;
			PSO.BlendState        = NoBlend;
			PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
			PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, PSO, 0);
			Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
			ColorMatrixPs->SetParameters(RHICmdList, ColorMatrixPs.GetPixelShader(), *Src, MergedMatrix);
			DrawFullscreenQuad(RHICmdList);

			RHICmdList.EndRenderPass();
			SwapBuffers();
			break;
		}

		case ERmlFilterType::MaskImage:
		{
			if (!Filter.MaskTexture.IsValid()) break;

			BeginPostprocessPass(*Dst);

			auto* NoBlend = TStaticBlendState<>::GetRHI();

			FGraphicsPipelineStateInitializer PSO;
			RHICmdList.ApplyCachedRenderTargets(PSO);
			PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
			PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
			PSO.BoundShaderState.PixelShaderRHI       = BlendMaskPs.GetPixelShader();
			PSO.PrimitiveType     = PT_TriangleList;
			PSO.BlendState        = NoBlend;
			PSO.RasterizerState   = TStaticRasterizerState<>::GetRHI();
			PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, PSO, 0);
			Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
			BlendMaskPs->SetParameters(RHICmdList, BlendMaskPs.GetPixelShader(), *Src, Filter.MaskTexture);
			DrawFullscreenQuad(RHICmdList);

			RHICmdList.EndRenderPass();
			SwapBuffers();
			break;
		}

		default:
			break;
		}
	}

	return Src;  // Result is in *Src after ping-pong (could be PostprocessA or PostprocessB)
}

// ============================================================================
// Blur — GL3-style downsample → blur → upsample
//   FRmlBlurKernel — CPU-precomputed linear-sampling Gaussian weights
// ============================================================================
// Pairs adjacent Gaussian taps via the hardware bilinear trick:
//   sample at sub-pixel offset (i + w[i+1]/(w[i]+w[i+1])) so the HW bilinear
//   filter returns w[i]*tex[i] + w[i+1]*tex[i+1] in one fetch.
// This halves texture fetches and eliminates per-pixel exp() in the shader.

struct FRmlBlurKernel
{
	float  ScalarOffsets[16]; // bilinear-interpolation positions (in texel units) for each pair
	float  PairWeights[16];   // combined normalized weight for each +/- pair
	float  CenterWeight;      // normalized weight for the center tap
	int32  NumSamples;        // number of pairs (≤ 16)
};

static FRmlBlurKernel ComputeRmlBlurKernel(float Sigma)
{
	// Gaussian weights at integer texel positions, radius = ceil(3*sigma), capped at 20
	int32 Radius = FMath::Min(FMath::CeilToInt(Sigma * 3.0f), 20);
	float W[21] = {};
	float Total = 0.f;
	for (int32 i = 0; i <= Radius; i++)
	{
		float x = (float)i;
		W[i] = FMath::Exp(-0.5f * x * x / (Sigma * Sigma));
		Total += (i == 0) ? W[i] : 2.f * W[i];
	}
	// Normalize
	for (int32 i = 0; i <= Radius; i++) W[i] /= Total;

	FRmlBlurKernel K;
	K.CenterWeight = W[0];
	K.NumSamples = 0;

	for (int32 i = 1; i <= Radius && K.NumSamples < 16; i += 2)
	{
		float w1 = W[i];
		float w2 = (i + 1 <= Radius) ? W[i + 1] : 0.f;
		float wSum = w1 + w2;
		// Bilinear trick offset: i + w2/(w1+w2) — causes HW bilinear to return w1*tex[i]+w2*tex[i+1]
		float Offset = (wSum > 1e-6f) ? ((float)i * w1 + (float)(i + 1) * w2) / wSum : (float)i;
		K.ScalarOffsets[K.NumSamples] = Offset;
		K.PairWeights[K.NumSamples]   = wSum;
		K.NumSamples++;
	}

	return K;
}

static void SigmaToParameters(float DesiredSigma, int32& OutPassLevel, float& OutSigma)
{
	constexpr int32 MaxNumPasses = 10;
	constexpr float MaxSinglePassSigma = 3.0f;
	OutPassLevel = FMath::Clamp(
		FMath::FloorLog2(FMath::Max((int32)(DesiredSigma * (2.0f / MaxSinglePassSigma)), 1)),
		0, MaxNumPasses);
	OutSigma = FMath::Clamp(DesiredSigma / (float)(1 << OutPassLevel), 0.0f, MaxSinglePassSigma);
}

void FRmlDrawer::RenderBlur(FRHICommandListImmediate& RHICmdList, float Sigma,
	FTextureRHIRef& SourceDest, FTextureRHIRef& Temp,
	TShaderMapRef<FRmlShaderVs>& Vs, TShaderMapRef<FRmlShaderPsBlur>& BlurPs,
	const FIntPoint& RTSize, const FIntRect& ScissorRect)
{
	int32 PassLevel = 0;
	float AdjSigma = Sigma;
	SigmaToParameters(Sigma, PassLevel, AdjSigma);

	auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FRmlShaderPsPassthrough> PassthroughPs(ShaderMap);
	auto* NoBlend = TStaticBlendState<>::GetRHI();
	auto* NoDS = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	auto* Rast = TStaticRasterizerState<>::GetRHI();
	const FVector2f TexelSize(1.0f / (float)RTSize.X, 1.0f / (float)RTSize.Y);
	const FVector2f FullUVScale(1.0f, 1.0f);

	// Helper to draw a fullscreen pass with the passthrough shader.
	// UVScale maps the fullscreen quad's UV 0-1 to the source content region.
	// OptScissor: if non-null, apply scissor rect to limit fill rate.
	auto DrawPassthrough = [&](FTextureRHIRef& ReadTex, FTextureRHIRef& WriteTex,
		int32 VpW, int32 VpH, const FVector2f& UVScale, const TCHAR* DebugName,
		const FIntRect* OptScissor = nullptr)
	{
		FRHIRenderPassInfo PassInfo(WriteTex, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(PassInfo, DebugName);
		RHICmdList.SetViewport(0, 0, 0.0f, (float)VpW, (float)VpH, 1.0f);
		if (OptScissor)
			RHICmdList.SetScissorRect(true, OptScissor->Min.X, OptScissor->Min.Y, OptScissor->Max.X, OptScissor->Max.Y);
		else
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		FGraphicsPipelineStateInitializer PSO;
		RHICmdList.ApplyCachedRenderTargets(PSO);
		PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSO.BoundShaderState.PixelShaderRHI       = PassthroughPs.GetPixelShader();
		PSO.PrimitiveType     = PT_TriangleList;
		PSO.BlendState        = NoBlend;
		PSO.RasterizerState   = Rast;
		PSO.DepthStencilState = NoDS;

		SetGraphicsPipelineState(RHICmdList, PSO, 0);
		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
		PassthroughPs->SetParameters(RHICmdList, PassthroughPs.GetPixelShader(), ReadTex, 1.0f, UVScale);
		DrawFullscreenQuad(RHICmdList);
		RHICmdList.EndRenderPass();
	};

	// Helper to draw a blur pass using CPU-precomputed linear-sampling weights.
	// Kernel: precomputed FRmlBlurKernel (scalar offsets + pair weights).
	// Direction: (1,0) for H pass, (0,1) for V pass.
	// UVScale maps the fullscreen quad's UV 0-1 to the source content region.
	// TexelSize stays at 1/RTSize — kernel offsets are in texel units and scaled here.
	// OptScissor: if non-null, apply scissor rect to limit fill rate.
	//   When scissoring is active, use Clear_Store instead of DontLoad_Store:
	//   the separable H→V blur chain requires the intermediate texture (Temp) to have
	//   zeros outside the scissored region.  If H writes only to ScissorRect (DontLoad
	//   leaves stale data outside), the V kernel at ScissorRect.Min.Y reads rows above
	//   the scissor = previous-frame garbage → horizontal flicker line at the shadow edge.
	//   Clear_Store is a hardware fast-clear (~free, WriteTex has ClearValue=Transparent).
	auto DrawBlurPass = [&](FTextureRHIRef& ReadTex, FTextureRHIRef& WriteTex,
		int32 VpW, int32 VpH, const FRmlBlurKernel& Kernel, FVector2f Direction,
		const FVector2f& UVScale, const TCHAR* DebugName,
		const FIntRect* OptScissor = nullptr)
	{
		ERenderTargetActions Actions = (OptScissor != nullptr)
			? ERenderTargetActions::Clear_Store
			: ERenderTargetActions::DontLoad_Store;
		FRHIRenderPassInfo PassInfo(WriteTex, Actions);
		RHICmdList.BeginRenderPass(PassInfo, DebugName);
		RHICmdList.SetViewport(0, 0, 0.0f, (float)VpW, (float)VpH, 1.0f);
		if (OptScissor)
			RHICmdList.SetScissorRect(true, OptScissor->Min.X, OptScissor->Min.Y, OptScissor->Max.X, OptScissor->Max.Y);
		else
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		FGraphicsPipelineStateInitializer PSO;
		RHICmdList.ApplyCachedRenderTargets(PSO);
		PSO.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSO.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSO.BoundShaderState.PixelShaderRHI       = BlurPs.GetPixelShader();
		PSO.PrimitiveType     = PT_TriangleList;
		PSO.BlendState        = NoBlend;
		PSO.RasterizerState   = Rast;
		PSO.DepthStencilState = NoDS;

		SetGraphicsPipelineState(RHICmdList, PSO, 0);
		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);

		// Build UV-space sample offsets for this pass direction.
		// ScalarOffset (texel units) → UV offset = scalar * Direction * TexelSize
		FVector4f BlurSamplesArr[16];
		FMemory::Memzero(BlurSamplesArr, sizeof(BlurSamplesArr));
		for (int32 i = 0; i < Kernel.NumSamples; i++)
		{
			BlurSamplesArr[i] = FVector4f(
				Direction.X * Kernel.ScalarOffsets[i] * TexelSize.X,
				Direction.Y * Kernel.ScalarOffsets[i] * TexelSize.Y,
				Kernel.PairWeights[i],
				0.f);
		}

		BlurPs->SetParameters(RHICmdList, BlurPs.GetPixelShader(), ReadTex,
			BlurSamplesArr, Kernel.NumSamples, Kernel.CenterWeight, TexelSize, UVScale);
		DrawFullscreenQuad(RHICmdList);
		RHICmdList.EndRenderPass();
	};

	// Precompute linear-sampling Gaussian kernel for the adjusted sigma.
	// Done once here — reused by all H/V blur passes (same sigma, direction varies).
	const FRmlBlurKernel BlurKernel = ComputeRmlBlurKernel(AdjSigma);

	// ------------------------------------------------------------------
	// PassLevel == 0: no downsampling, just blur at full resolution.
	// Scissored: the blur shader clamps UVs to [0, UVScale], and the
	// input texture was cleared+scissor-blitted so content outside the
	// scissor is transparent (zeros).  Scissoring limits pixel shader
	// invocations to the element region instead of the full 5120×1440.
	// ------------------------------------------------------------------
	if (PassLevel == 0)
	{
		// H: SourceDest → Temp
		DrawBlurPass(SourceDest, Temp, RTSize.X, RTSize.Y, BlurKernel, FVector2f(1.0f, 0.0f), FullUVScale, TEXT("RmlUI_BlurH"), &ScissorRect);
		// V: Temp → SourceDest
		DrawBlurPass(Temp, SourceDest, RTSize.X, RTSize.Y, BlurKernel, FVector2f(0.0f, 1.0f), FullUVScale, TEXT("RmlUI_BlurV"), &ScissorRect);
		return;
	}

	// ------------------------------------------------------------------
	// Step 1: Iterative 2x downsampling with bilinear filtering
	// ------------------------------------------------------------------
	// SrcUVScale tracks the source content extent as a fraction of the full texture.
	// For the first step, source content fills the entire texture (UVScale = 1,1).
	// After each step, the content occupies the top-left VpW x VpH of the destination.
	int32 VpW = RTSize.X;
	int32 VpH = RTSize.Y;
	FVector2f SrcUVScale = FullUVScale;

	for (int32 i = 0; i < PassLevel; i++)
	{
		int32 NewVpW = FMath::Max(VpW / 2, 1);
		int32 NewVpH = FMath::Max(VpH / 2, 1);

		const bool bFromSource = (i % 2 == 0);
		FTextureRHIRef& ReadTex  = bFromSource ? SourceDest : Temp;
		FTextureRHIRef& WriteTex = bFromSource ? Temp : SourceDest;
		DrawPassthrough(ReadTex, WriteTex, NewVpW, NewVpH, SrcUVScale, TEXT("RmlUI_BlurDown"));

		VpW = NewVpW;
		VpH = NewVpH;
		// After writing, the content is in the top-left VpW x VpH of the texture.
		SrcUVScale = FVector2f((float)VpW / (float)RTSize.X, (float)VpH / (float)RTSize.Y);
	}

	// After downsample: data is in Temp if PassLevel is odd, SourceDest if even.
	// SrcUVScale = content extent at reduced resolution (used for blur and upsample).
	const FVector2f ReducedUVScale = SrcUVScale;
	const bool bDataInSourceDest = (PassLevel % 2 == 0);

	// ------------------------------------------------------------------
	// Step 2: Separable Gaussian blur at reduced resolution
	// ------------------------------------------------------------------
	if (bDataInSourceDest)
	{
		// Data in SourceDest.
		// H: SourceDest → Temp
		DrawBlurPass(SourceDest, Temp, VpW, VpH, BlurKernel, FVector2f(1.0f, 0.0f), ReducedUVScale, TEXT("RmlUI_BlurH"));
		// V: Temp → SourceDest
		DrawBlurPass(Temp, SourceDest, VpW, VpH, BlurKernel, FVector2f(0.0f, 1.0f), ReducedUVScale, TEXT("RmlUI_BlurV"));
		// Result in SourceDest. Need it in Temp for upsample. Copy at reduced VP.
		DrawPassthrough(SourceDest, Temp, VpW, VpH, ReducedUVScale, TEXT("RmlUI_BlurXfer"));
	}
	else
	{
		// Data in Temp (PassLevel is odd).
		// H: Temp → SourceDest
		DrawBlurPass(Temp, SourceDest, VpW, VpH, BlurKernel, FVector2f(1.0f, 0.0f), ReducedUVScale, TEXT("RmlUI_BlurH"));
		// V: SourceDest → Temp
		DrawBlurPass(SourceDest, Temp, VpW, VpH, BlurKernel, FVector2f(0.0f, 1.0f), ReducedUVScale, TEXT("RmlUI_BlurV"));
		// Result in Temp. Ready for upsample.
	}

	// ------------------------------------------------------------------
	// Step 3: Upsample from Temp (reduced) to SourceDest (full resolution)
	// ------------------------------------------------------------------
	// The content in Temp occupies the top-left VpW x VpH texels.
	// UVScale maps the full viewport's UV 0-1 to that content region,
	// so bilinear filtering correctly upscales it.
	// Scissored: upsample runs at full resolution — scissor limits fill to element bounds.
	DrawPassthrough(Temp, SourceDest, RTSize.X, RTSize.Y, ReducedUVScale, TEXT("RmlUI_BlurUp"), &ScissorRect);
}

// ============================================================================
// BuildFrameGeometry — render-thread pre-pass for shared VB/IB
// ============================================================================

void FRmlDrawer::BuildFrameGeometry(FRHICommandListImmediate& RHICmdList)
{
	// Accumulate all mesh vertices and indices from every command that has a mesh
	// (DrawMesh, RenderToClipMask, DrawShader) into two flat arrays.
	// We don't modify the indices — the GPU baseVertexIndex parameter offsets them.

	TResourceArray<FRmlMesh::FVertexData> AllVerts;
	TResourceArray<uint16>               AllIdx;

	for (auto& Cmd : CommandList)
	{
		if (!Cmd.Mesh.IsValid()) continue;
		FRmlMesh* M = Cmd.Mesh.Get();
		if (M->NumVertices == 0 || M->NumTriangles == 0) continue;

		// Record the slice offsets in the command (pre-pass writes, execute loop reads)
		Cmd.BaseVertex = AllVerts.Num();
		Cmd.StartIndex = AllIdx.Num();

		// Append — no index rewriting; DrawIndexedPrimitive's baseVertexIndex handles the offset
		AllVerts.Append(M->Vertices.GetData(), M->Vertices.Num());
		AllIdx.Append(M->Indices.GetData(), M->Indices.Num());
	}

	if (AllVerts.Num() == 0)
	{
		FrameVB.SafeRelease();
		FrameIB.SafeRelease();
		return;
	}

	// Create ONE VB + IB for the entire frame. BUF_Volatile uses D3D12's upload-heap
	// ring allocator — effectively free allocation, zero copy of data from CPU to GPU.
	const int32 VBSize = sizeof(FRmlMesh::FVertexData) * AllVerts.Num();
	FRHIResourceCreateInfo VInfo(TEXT("RmlFrameVB"), &AllVerts);
	FrameVB = RHICreateVertexBuffer(VBSize, BUF_Volatile, VInfo);

	const int32 IBSize = sizeof(uint16) * AllIdx.Num();
	FRHIResourceCreateInfo IInfo(TEXT("RmlFrameIB"), &AllIdx);
	FrameIB = RHICreateIndexBuffer(sizeof(uint16), IBSize, BUF_Volatile, IInfo);
}

// DrawRenderThread — main entry point
// ============================================================================

void FRmlDrawer::DrawRenderThread(FRHICommandListImmediate& RHICmdList, const void* RenderTarget)
{
	SCOPE_CYCLE_COUNTER(STAT_RmlUI_DrawRenderThread);
	check(IsInRenderingThread() || IsInParallelRenderingThread());

	if (CommandList.Num() == 0)
	{
		MarkFree();
		return;
	}

	const FTextureRHIRef* RT = (const FTextureRHIRef*)RenderTarget;
	const FIntPoint RTSize((*RT)->GetSizeX(), (*RT)->GetSizeY());
	const EPixelFormat RTFormat = (*RT)->GetFormat();

	static bool bLoggedOnce = false;
	if (!bLoggedOnce)
	{
		auto FmtToStr = [](EPixelFormat F) -> const TCHAR* {
			switch (F)
			{
			case PF_B8G8R8A8:     return TEXT("B8G8R8A8");
			case PF_R8G8B8A8:     return TEXT("R8G8B8A8");
			case PF_FloatRGBA:    return TEXT("FloatRGBA");
			case PF_A2B10G10R10:  return TEXT("A2B10G10R10");
			default:              return TEXT("OTHER");
			}
		};
		UE_LOG(LogUERmlUI, Log, TEXT("DrawRenderThread: SlateRT=%dx%d Format=%s InternalFormat=B8G8R8A8 MSAA=%d Samples=%d Cmds=%d"),
			RTSize.X, RTSize.Y, FmtToStr(RTFormat), bUseMSAA ? 1 : 0, MSAASamples, CommandList.Num());
		bLoggedOnce = true;
	}

	EnsureRenderResources(RHICmdList, RTSize, RTFormat);

	// Pre-pass: accumulate all mesh geometry into one shared VB/IB for the frame.
	// This replaces N×(RHICreateVertexBuffer + RHICreateIndexBuffer) with 1+1 per frame.
	BuildFrameGeometry(RHICmdList);

	auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FRmlShaderVs>      Vs(ShaderMap);
	TShaderMapRef<FRmlShaderPs>      Ps(ShaderMap);
	TShaderMapRef<FRmlShaderPsNoTex> PsNoTex(ShaderMap);

	auto* PremulBlend = TStaticBlendState<CW_RGBA,
		BO_Add, BF_One, BF_InverseSourceAlpha,
		BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

	// Reset state
	bClipMaskActive = false;
	StencilRef = 0;
	bInRenderPass = false;
	LayerStack.Reset();

	// --- BeginFrame: push base layer ---
	int32 BaseLayerIndex = LayerStack.Push(RHICmdList);
	check(BaseLayerIndex == 0);

	// BeginFrame: copy Slate RT background into our internal layer 0 via shader blit.
	// MSAA: populates all samples; Non-MSAA: handles format conversion (A2B10G10R10→B8G8R8A8).
	// At EndFrame, we blit back (with MSAA resolve if needed).
	{
		FRHICopyTextureInfo CopyInfo;
		RHICmdList.CopyTexture(*RT, ResolveTarget, CopyInfo);

		BeginLayerRenderPass(RHICmdList, 0, RTSize, ERenderTargetLoadAction::EClear, /*bClearStencil=*/ true);

		TShaderMapRef<FRmlShaderPsPassthrough> BlitPs(ShaderMap);

		FGraphicsPipelineStateInitializer PSOBlit;
		RHICmdList.ApplyCachedRenderTargets(PSOBlit);
		PSOBlit.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSOBlit.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSOBlit.BoundShaderState.PixelShaderRHI       = BlitPs.GetPixelShader();
		PSOBlit.PrimitiveType     = PT_TriangleList;
		PSOBlit.BlendState        = TStaticBlendState<>::GetRHI();  // opaque overwrite
		PSOBlit.RasterizerState   = TStaticRasterizerState<>::GetRHI();
		PSOBlit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		SetGraphicsPipelineState(RHICmdList, PSOBlit, 0);
		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
		BlitPs->SetParameters(RHICmdList, BlitPs.GetPixelShader(), ResolveTarget, 1.0f);
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		DrawFullscreenQuad(RHICmdList);
	}

	// Build initial PSOs
	FGraphicsPipelineStateInitializer PSOTex, PSONoTex;
	BuildDrawPSOs(RHICmdList, PSOTex, PSONoTex, Vs, Ps, PsNoTex, PremulBlend);

	FRmlTextureEntry* CurTexture = nullptr;
	bool bPSODirty = true;	// need to set initial PSO on first draw

	// --- Process command buffer ---
	for (auto& Cmd : CommandList)
	{
		switch (Cmd.Type)
		{
		case ERmlCommand::DrawMesh:
		{
			if (bPSODirty)
			{
				BuildDrawPSOs(RHICmdList, PSOTex, PSONoTex, Vs, Ps, PsNoTex, PremulBlend);
				CurTexture = nullptr;	// force re-bind
				bPSODirty = false;
			}

			FRmlTextureEntry* NewTex = Cmd.Texture.Get();
			if (CurTexture == nullptr && NewTex == nullptr)
			{
				// First draw, no texture
				SetGraphicsPipelineState(RHICmdList, PSONoTex, 0);
			}
			else if (CurTexture == nullptr && NewTex != nullptr)
			{
				SetGraphicsPipelineState(RHICmdList, PSOTex, 0);
			}

			ExecuteDrawMesh(RHICmdList, Cmd, Vs, Ps, PsNoTex, PSOTex, PSONoTex, CurTexture);
			break;
		}

		case ERmlCommand::EnableClipMask:
			ExecuteEnableClipMask(RHICmdList, Cmd);
			bPSODirty = true;
			break;

		case ERmlCommand::RenderToClipMask:
			ExecuteRenderToClipMask(RHICmdList, Cmd, Vs, PsNoTex);
			bPSODirty = true;
			break;

		case ERmlCommand::PushLayer:
			ExecutePushLayer(RHICmdList, Cmd, RTSize);
			bPSODirty = true;
			CurTexture = nullptr;
			break;

		case ERmlCommand::CompositeLayers:
			ExecuteCompositeLayers(RHICmdList, Cmd, Vs, Ps, RTSize);
			bPSODirty = true;
			CurTexture = nullptr;
			break;

		case ERmlCommand::PopLayer:
			ExecutePopLayer(RHICmdList, RTSize);
			bPSODirty = true;
			CurTexture = nullptr;
			break;

		case ERmlCommand::DrawShader:
			ExecuteDrawShader(RHICmdList, Cmd, Vs, RTSize);
			bPSODirty = true;
			CurTexture = nullptr;
			break;

		case ERmlCommand::SaveLayerAsTexture:
			ExecuteSaveLayerAsTexture(RHICmdList, Cmd, RTSize);
			bPSODirty = true;
			CurTexture = nullptr;
			break;

		case ERmlCommand::SaveLayerAsMaskImage:
			ExecuteSaveLayerAsMaskImage(RHICmdList, Cmd, RTSize);
			bPSODirty = true;
			CurTexture = nullptr;
			break;
		}
	}

	// --- EndFrame ---
	EndCurrentRenderPass(RHICmdList);

	// EndFrame: blit layer 0 back to the Slate RT (handles format conversion).
	// MSAA: resolve MSAA layer → PostprocessA first, then blit PostprocessA → Slate RT.
	// Non-MSAA: blit layer 0 directly → Slate RT.
	FTextureRHIRef BlitSource = LayerStack.GetColorRT(0);

	if (bUseMSAA)
	{
		// Resolve MSAA B8G8R8A8 → PostprocessA (1x B8G8R8A8).
		// Cannot resolve directly to Slate RT — D3D12 requires matching formats for MSAA resolve.
		FRHIRenderPassInfo ResolvePassInfo;
		ResolvePassInfo.ColorRenderTargets[0].RenderTarget = BlitSource;
		ResolvePassInfo.ColorRenderTargets[0].ResolveTarget = LayerStack.PostprocessA;
		ResolvePassInfo.ColorRenderTargets[0].Action = MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EMultisampleResolve);
		RHICmdList.BeginRenderPass(ResolvePassInfo, TEXT("RmlUI_FinalResolve"));
		RHICmdList.EndRenderPass();

		BlitSource = LayerStack.PostprocessA;
	}

	// Shader blit internal RT → Slate RT (format conversion B8G8R8A8→SlateFormat)
	{
		TShaderMapRef<FRmlShaderPsPassthrough> BlitPs(ShaderMap);

		FRHIRenderPassInfo BlitPassInfo(*RT, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(BlitPassInfo, TEXT("RmlUI_FinalBlit"));
		RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		FGraphicsPipelineStateInitializer PSOBlit;
		RHICmdList.ApplyCachedRenderTargets(PSOBlit);
		PSOBlit.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
		PSOBlit.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
		PSOBlit.BoundShaderState.PixelShaderRHI       = BlitPs.GetPixelShader();
		PSOBlit.PrimitiveType     = PT_TriangleList;
		PSOBlit.BlendState        = TStaticBlendState<>::GetRHI();  // opaque overwrite
		PSOBlit.RasterizerState   = TStaticRasterizerState<>::GetRHI();
		PSOBlit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		SetGraphicsPipelineState(RHICmdList, PSOBlit, 0);
		Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
		BlitPs->SetParameters(RHICmdList, BlitPs.GetPixelShader(), BlitSource, 1.0f);
		DrawFullscreenQuad(RHICmdList);

		RHICmdList.EndRenderPass();
	}

	// Cleanup
	LayerStack.Pop();
	check(LayerStack.GetActiveCount() == 0);

	CommandList.Reset();
	MarkFree();
}
