#pragma once
#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"
#include "DataDrivenShaderPlatformInfo.h"

class FRmlShaderVs : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderVs, Global)
public:
	FRmlShaderVs()
	{ }

	FRmlShaderVs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FGlobalShader(Initializer)
	{
		InTransform.Bind(Initializer.ParameterMap, TEXT("InTransform"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FMatrix44f& TransformValue)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, InTransform, TransformValue);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundVertexShader(), BatchedParameters);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
	LAYOUT_FIELD(FShaderParameter, InTransform)
};

class FRmlShaderPs : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPs, Global)
public:
	FRmlShaderPs()
	{ }

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
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, FRHITexture* SourceTexture, float PremulTexAlpha = 0.0f)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, InTexture, SourceTexture);
		SetSamplerParameter(BatchedParameters, InTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI());
		SetShaderValue(BatchedParameters, InPremulTexAlpha, PremulTexAlpha);
		RHICmdList.SetBatchedShaderParameters(ShaderRHI, BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler)
	LAYOUT_FIELD(FShaderParameter, InPremulTexAlpha)
};

class FRmlShaderPsNoTex : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRmlShaderPsNoTex, Global)
public:
	FRmlShaderPsNoTex()
	{ }

	FRmlShaderPsNoTex(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
