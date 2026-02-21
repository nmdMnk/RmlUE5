#include "RmlDrawer.h"
#include "RmlMesh.h"
#include "RmlShader.h"
#include "Render/TextureEntries.h"

FRmlDrawer::FRmlDrawer(bool bUsing)
	: bIsFree(!bUsing)
	, CachedRTSize(0, 0)
{
}

void FRmlDrawer::EnsureMSAAResources(FRHICommandListImmediate& RHICmdList, const FIntPoint& RTSize, EPixelFormat RTFormat)
{
	if (CachedRTSize == RTSize && MSAATarget.IsValid())
		return;

	CachedRTSize = RTSize;

	// 4x MSAA render target
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RmlUI_MSAA"), RTSize.X, RTSize.Y, RTFormat);
		Desc.NumSamples = 4;
		Desc.Flags = ETextureCreateFlags::RenderTargetable;
		Desc.ClearValue = FClearValueBinding::Transparent;
		MSAATarget = RHICmdList.CreateTexture(Desc);
	}

	// Resolve target (1x, shader-readable for composite pass)
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("RmlUI_Resolve"), RTSize.X, RTSize.Y, RTFormat);
		Desc.Flags = ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource;
		Desc.ClearValue = FClearValueBinding::Transparent;
		ResolveTarget = RHICmdList.CreateTexture(Desc);
	}

	// Fullscreen quad VB/IB (only once, size-independent)
	if (!QuadVB.IsValid())
	{
		// Must match FVertexData layout: Position(2f), Color(4b), UV(2f) = 20 bytes
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

void FRmlDrawer::DrawRenderThread(FRHICommandListImmediate& RHICmdList, const void* RenderTarget)
{
	// check thread 
	check(IsInRenderingThread() || IsInParallelRenderingThread());
	
	// early out
	if (DrawList.Num() == 0)
	{
		MarkFree();
		return;
	}

	// Slate passes FTextureRHIRef*
	const FTextureRHIRef* RT = (const FTextureRHIRef*)RenderTarget;
	const FIntPoint RTSize((*RT)->GetSizeX(), (*RT)->GetSizeY());

	auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FRmlShaderVs>      Vs(ShaderMap);
	TShaderMapRef<FRmlShaderPs>      Ps(ShaderMap);
	TShaderMapRef<FRmlShaderPsNoTex> PsNoTex(ShaderMap);

	// Premultiplied alpha blend (color + alpha)
	auto* PremulBlend = TStaticBlendState<CW_RGBA,
		BO_Add, BF_One, BF_InverseSourceAlpha,
		BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

	if (bUseMSAA)
	{
		EnsureMSAAResources(RHICmdList, RTSize, (*RT)->GetFormat());

		// --- Pass 1: Render all RmlUi geometry to 4x MSAA target ---
		{
			FRHIRenderPassInfo MSAAPassInfo(
				MSAATarget,
				MakeRenderTargetActions(ERenderTargetLoadAction::EClear, ERenderTargetStoreAction::EMultisampleResolve),
				ResolveTarget);
			RHICmdList.BeginRenderPass(MSAAPassInfo, TEXT("RmlUI_MSAA"));
			RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);

			DrawGeometry(RHICmdList, Vs, Ps, PsNoTex, PremulBlend);

			RHICmdList.EndRenderPass();
		}

		// --- Pass 2: Composite resolved texture onto Slate backbuffer ---
		{
			FRHIRenderPassInfo CompPassInfo(*RT, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(CompPassInfo, TEXT("RmlUI_Comp"));
			RHICmdList.SetViewport(0, 0, 0.0f, (float)RTSize.X, (float)RTSize.Y, 1.0f);

			FGraphicsPipelineStateInitializer PSOComp;
			RHICmdList.ApplyCachedRenderTargets(PSOComp);
			PSOComp.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
			PSOComp.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
			PSOComp.BoundShaderState.PixelShaderRHI       = Ps.GetPixelShader();
			PSOComp.PrimitiveType     = PT_TriangleList;
			PSOComp.BlendState        = PremulBlend;
			PSOComp.RasterizerState   = TStaticRasterizerState<>::GetRHI();
			PSOComp.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, PSOComp, 0);

			Vs->SetParameters(RHICmdList, FMatrix44f::Identity);
			Ps->SetParameters(RHICmdList, Ps.GetPixelShader(), ResolveTarget, 0.0f);

			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			RHICmdList.SetStreamSource(0, QuadVB, 0);
			RHICmdList.DrawIndexedPrimitive(QuadIB, 0, 0, 4, 0, 2, 1);

			RHICmdList.EndRenderPass();
		}
	}
	else
	{
		// --- Single pass: render directly to Slate backbuffer ---
		// Do NOT call SetViewport — inherit Slate's viewport to stay coherent
		// with the game-thread NDC matrix.
		FRHIRenderPassInfo PassInfo(*RT, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(PassInfo, TEXT("RmlUI_Direct"));

		DrawGeometry(RHICmdList, Vs, Ps, PsNoTex, PremulBlend);

		RHICmdList.EndRenderPass();
	}

	DrawList.Reset();
	MarkFree();
}

void FRmlDrawer::DrawGeometry(
	FRHICommandListImmediate& RHICmdList,
	TShaderMapRef<FRmlShaderVs>& Vs,
	TShaderMapRef<FRmlShaderPs>& Ps,
	TShaderMapRef<FRmlShaderPsNoTex>& PsNoTex,
	FRHIBlendState* BlendState)
{
	FGraphicsPipelineStateInitializer PSOTex;
	RHICmdList.ApplyCachedRenderTargets(PSOTex);
	PSOTex.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
	PSOTex.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
	PSOTex.BoundShaderState.PixelShaderRHI       = Ps.GetPixelShader();
	PSOTex.PrimitiveType     = PT_TriangleList;
	PSOTex.BlendState        = BlendState;
	PSOTex.RasterizerState   = TStaticRasterizerState<>::GetRHI();
	PSOTex.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	FGraphicsPipelineStateInitializer PSONoTex;
	RHICmdList.ApplyCachedRenderTargets(PSONoTex);
	PSONoTex.BoundShaderState.VertexDeclarationRHI = FRmlMesh::GetMeshDeclaration();
	PSONoTex.BoundShaderState.VertexShaderRHI      = Vs.GetVertexShader();
	PSONoTex.BoundShaderState.PixelShaderRHI       = PsNoTex.GetPixelShader();
	PSONoTex.PrimitiveType     = PT_TriangleList;
	PSONoTex.BlendState        = BlendState;
	PSONoTex.RasterizerState   = TStaticRasterizerState<>::GetRHI();
	PSONoTex.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	FRmlTextureEntry* CurTexture = DrawList[0].BoundTexture;
	SetGraphicsPipelineState(RHICmdList, CurTexture ? PSOTex : PSONoTex, 0);
	if (CurTexture)
		Ps->SetParameters(RHICmdList, Ps.GetPixelShader(), CurTexture->GetTextureRHI(),
			CurTexture->bPremultiplied ? 0.0f : 1.0f);

	for (auto& DrawInfo : DrawList)
	{
		FRmlTextureEntry* NewTexture = DrawInfo.BoundTexture;

		if (NewTexture != CurTexture)
		{
			if ((CurTexture != nullptr) != (NewTexture != nullptr))
				SetGraphicsPipelineState(RHICmdList, NewTexture ? PSOTex : PSONoTex, 0);

			if (NewTexture)
				Ps->SetParameters(RHICmdList, Ps.GetPixelShader(), NewTexture->GetTextureRHI(),
					NewTexture->bPremultiplied ? 0.0f : 1.0f);

			CurTexture = NewTexture;
		}

		// Set transform (NDC already baked in on game thread)
		Vs->SetParameters(RHICmdList, DrawInfo.RenderTransform);

		// Set scissor rect
		RHICmdList.SetScissorRect(
			true,
			DrawInfo.ScissorRect.Min.X,
			DrawInfo.ScissorRect.Min.Y,
			DrawInfo.ScissorRect.Max.X,
			DrawInfo.ScissorRect.Max.Y);

		// Render mesh
		DrawInfo.BoundMesh->DrawMesh(RHICmdList);
	}
}
