#pragma once
#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"
#include "DataDrivenShaderPlatformInfo.h"

// Forward declare for gradient shader params
struct FCompiledRmlShader;

// ============================================================================
// Vertex shader (shared by all passes)
// ============================================================================

class FRmlShaderVs : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderVs, Global)
public:
	FRmlShaderVs() {}
	FRmlShaderVs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTransform.Bind(Initializer.ParameterMap, TEXT("InTransform"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FMatrix44f& TransformValue)
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(Params, InTransform, TransformValue);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundVertexShader(), Params);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
private:
	LAYOUT_FIELD(FShaderParameter, InTransform)
};

// ============================================================================
// Pixel shader — textured (main geometry)
// ============================================================================

class FRmlShaderPs : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPs, Global)
public:
	FRmlShaderPs() {}
	FRmlShaderPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
		InPremulTexAlpha.Bind(Initializer.ParameterMap, TEXT("InPremulTexAlpha"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* SourceTexture,
		float PremulTexAlpha = 0.0f, bool bWrapSampler = false, FRHIShaderResourceView* OverrideSRV = nullptr)
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		// When an SRV override is provided (non-sRGB view of an sRGB texture),
		// bind it instead of the texture directly so the GPU skips sRGB decode.
		if (OverrideSRV)
			SetSRVParameter(Params, InTexture, OverrideSRV);
		else
			SetTextureParameter(Params, InTexture, SourceTexture);
		// File-loaded textures use AM_Wrap for repeat decorators (matches GL3 reference).
		// Generated textures (box-shadow, font atlas) use AM_Clamp to avoid edge bleeding.
		FRHISamplerState* Sampler = bWrapSampler
			? TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI()
			: TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SetSamplerParameter(Params, InTextureSampler, Sampler);
		SetShaderValue(Params, InPremulTexAlpha, PremulTexAlpha);
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
	LAYOUT_FIELD(FShaderParameter, InPremulTexAlpha)
};

// ============================================================================
// Pixel shader — no texture (solid color)
// ============================================================================

class FRmlShaderPsNoTex : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsNoTex, Global)
public:
	FRmlShaderPsNoTex() {}
	FRmlShaderPsNoTex(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer) {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

};

// ============================================================================
// Filter: Passthrough (opacity)
// ============================================================================

class FRmlShaderPsPassthrough : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsPassthrough, Global)
public:
	FRmlShaderPsPassthrough() {}
	FRmlShaderPsPassthrough(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
		InBlendFactor.Bind(Initializer.ParameterMap, TEXT("InBlendFactor"));
		InUVScale.Bind(Initializer.ParameterMap, TEXT("InUVScale"));
		InUVOffset.Bind(Initializer.ParameterMap, TEXT("InUVOffset"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* Texture, float BlendFactor,
		const FVector2f& UVScale = FVector2f(1.0f, 1.0f), const FVector2f& UVOffset = FVector2f(0.0f, 0.0f))
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(Params, InTexture, Texture);
		SetSamplerParameter(Params, InTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetShaderValue(Params, InBlendFactor, BlendFactor);
		SetShaderValue(Params, InUVScale, UVScale);
		SetShaderValue(Params, InUVOffset, UVOffset);
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
	LAYOUT_FIELD(FShaderParameter, InBlendFactor)
	LAYOUT_FIELD(FShaderParameter, InUVScale)
	LAYOUT_FIELD(FShaderParameter, InUVOffset)
};

// ============================================================================
// Filter: Gaussian Blur (linear-sampling, CPU-precomputed weights)
// ============================================================================

class FRmlShaderPsBlur : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsBlur, Global)
public:
	FRmlShaderPsBlur() {}
	FRmlShaderPsBlur(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
		InBlurSamples.Bind(Initializer.ParameterMap, TEXT("InBlurSamples"));
		InNumSamples.Bind(Initializer.ParameterMap, TEXT("InNumSamples"));
		InCenterWeight.Bind(Initializer.ParameterMap, TEXT("InCenterWeight"));
		InTexelSize.Bind(Initializer.ParameterMap, TEXT("InTexelSize"));
		InUVScale.Bind(Initializer.ParameterMap, TEXT("InUVScale"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// BlurSamplesArr: array of 16 FVector4f where .xy = UV offset, .z = combined pair weight, .w = 0
	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* Texture,
		const FVector4f* BlurSamplesArr, int32 NumSamples, float CenterWeight,
		const FVector2f& TexelSize, const FVector2f& UVScale = FVector2f(1.0f, 1.0f))
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(Params, InTexture, Texture);
		SetSamplerParameter(Params, InTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetShaderValueArray(Params, InBlurSamples, BlurSamplesArr, 16);
		SetShaderValue(Params, InNumSamples, NumSamples);
		SetShaderValue(Params, InCenterWeight, CenterWeight);
		SetShaderValue(Params, InTexelSize, TexelSize);
		SetShaderValue(Params, InUVScale, UVScale);
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
	LAYOUT_FIELD(FShaderParameter, InBlurSamples)
	LAYOUT_FIELD(FShaderParameter, InNumSamples)
	LAYOUT_FIELD(FShaderParameter, InCenterWeight)
	LAYOUT_FIELD(FShaderParameter, InTexelSize)
	LAYOUT_FIELD(FShaderParameter, InUVScale)
};

// ============================================================================
// Filter: Drop Shadow
// ============================================================================

class FRmlShaderPsDropShadow : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsDropShadow, Global)
public:
	FRmlShaderPsDropShadow() {}
	FRmlShaderPsDropShadow(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
		InShadowColor.Bind(Initializer.ParameterMap, TEXT("InShadowColor"));
		InShadowOffset.Bind(Initializer.ParameterMap, TEXT("InShadowOffset"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* Texture,
		const FLinearColor& Color, const FVector2f& Offset)
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(Params, InTexture, Texture);
		SetSamplerParameter(Params, InTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetShaderValue(Params, InShadowColor, FVector4f(Color.R, Color.G, Color.B, Color.A));
		SetShaderValue(Params, InShadowOffset, Offset);
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
	LAYOUT_FIELD(FShaderParameter, InShadowColor)
	LAYOUT_FIELD(FShaderParameter, InShadowOffset)
};

// ============================================================================
// Filter: Color Matrix
// ============================================================================

class FRmlShaderPsColorMatrix : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsColorMatrix, Global)
public:
	FRmlShaderPsColorMatrix() {}
	FRmlShaderPsColorMatrix(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
		InColorMatrix.Bind(Initializer.ParameterMap, TEXT("InColorMatrix"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* Texture, const FMatrix44f& Matrix)
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(Params, InTexture, Texture);
		SetSamplerParameter(Params, InTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetShaderValue(Params, InColorMatrix, Matrix);
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
	LAYOUT_FIELD(FShaderParameter, InColorMatrix)
};

// ============================================================================
// Filter: Blend Mask
// ============================================================================

class FRmlShaderPsBlendMask : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsBlendMask, Global)
public:
	FRmlShaderPsBlendMask() {}
	FRmlShaderPsBlendMask(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
		InMaskTexture.Bind(Initializer.ParameterMap, TEXT("InMaskTexture"));
		InMaskSampler.Bind(Initializer.ParameterMap, TEXT("InMaskSampler"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* Texture, FRHITexture* MaskTexture)
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(Params, InTexture, Texture);
		SetSamplerParameter(Params, InTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetTextureParameter(Params, InMaskTexture, MaskTexture);
		SetSamplerParameter(Params, InMaskSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
	LAYOUT_FIELD(FShaderResourceParameter, InMaskTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InMaskSampler)
};

// ============================================================================
// MSAA Resolve (manual, scissorable)
// ============================================================================

class FRmlShaderPsMsaaResolve : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsMsaaResolve, Global)
public:
	FRmlShaderPsMsaaResolve() {}
	FRmlShaderPsMsaaResolve(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InMsaaTexture.Bind(Initializer.ParameterMap, TEXT("InMsaaTexture"));
		InSampleCount.Bind(Initializer.ParameterMap, TEXT("InSampleCount"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* MsaaTexture, int32 NumSamples)
	{
		FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(Params, InMsaaTexture, MsaaTexture);
		SetShaderValue(Params, InSampleCount, NumSamples);
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InMsaaTexture)
	LAYOUT_FIELD(FShaderParameter, InSampleCount)
};

// ============================================================================
// Gradient shader (Phase 5)
// ============================================================================

class FRmlShaderPsGradient : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsGradient, Global)
public:
	FRmlShaderPsGradient() {}
	FRmlShaderPsGradient(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InGradientFunc.Bind(Initializer.ParameterMap, TEXT("InGradientFunc"));
		InNumStops.Bind(Initializer.ParameterMap, TEXT("InNumStops"));
		InP.Bind(Initializer.ParameterMap, TEXT("InP"));
		InV.Bind(Initializer.ParameterMap, TEXT("InV"));
		InStopPositions.Bind(Initializer.ParameterMap, TEXT("InStopPositions"));
		InStopColors.Bind(Initializer.ParameterMap, TEXT("InStopColors"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FCompiledRmlShader& Shader);

private:
	LAYOUT_FIELD(FShaderParameter, InGradientFunc)
	LAYOUT_FIELD(FShaderParameter, InNumStops)
	LAYOUT_FIELD(FShaderParameter, InP)
	LAYOUT_FIELD(FShaderParameter, InV)
	LAYOUT_FIELD(FShaderParameter, InStopPositions)
	LAYOUT_FIELD(FShaderParameter, InStopColors)
};
