#include "RmlShader.h"
#include "RmlDrawer.h"		// for FCompiledRmlShader

// Base geometry shaders
IMPLEMENT_SHADER_TYPE(, FRmlShaderVs, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlShader_VS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(, FRmlShaderPs, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlShader_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsNoTex, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlShader_PSNoTex"), SF_Pixel);

// Filter shaders
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsPassthrough, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlFilter_Passthrough"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsBlur, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlFilter_Blur"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsDropShadow, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlFilter_DropShadow"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsColorMatrix, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlFilter_ColorMatrix"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsBlendMask, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlFilter_BlendMask"), SF_Pixel);

// MSAA resolve shader
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsMsaaResolve, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlFilter_MsaaResolve"), SF_Pixel);

// Gradient shader
IMPLEMENT_SHADER_TYPE(, FRmlShaderPsGradient, TEXT("/Plugin/UERmlUI/Private/RmlShader.usf"), TEXT("RmlShader_Gradient"), SF_Pixel);
// ============================================================================
// FRmlShaderPsGradient::SetParameters
// ============================================================================

void FRmlShaderPsGradient::SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FCompiledRmlShader& Shader)
{
	FRHIBatchedShaderParameters& Params = RHICmdList.GetScratchShaderParameters();

	int32 Func = (int32)Shader.GradientFunc;
	SetShaderValue(Params, InGradientFunc, Func);

	int32 NumStops = FMath::Min(Shader.StopPositions.Num(), 16);
	SetShaderValue(Params, InNumStops, NumStops);

	SetShaderValue(Params, InP, Shader.P);
	SetShaderValue(Params, InV, Shader.V);

	// HLSL float[16] arrays: each element occupies a full float4 register (16 bytes).
	// We must send FVector4f per element (position in .x) so the data aligns correctly.
	FVector4f Positions[16];
	FVector4f Colors[16];
	FMemory::Memzero(Positions, sizeof(Positions));
	FMemory::Memzero(Colors, sizeof(Colors));
	for (int32 i = 0; i < NumStops; ++i)
	{
		Positions[i] = FVector4f(Shader.StopPositions[i], 0.0f, 0.0f, 0.0f);
		Colors[i] = FVector4f(Shader.StopColors[i].R, Shader.StopColors[i].G, Shader.StopColors[i].B, Shader.StopColors[i].A);
	}

	SetShaderValueArray(Params, InStopPositions, Positions, 16);
	SetShaderValueArray(Params, InStopColors, Colors, 16);

	RHICmdList.SetBatchedShaderParameters(ShaderRHI, Params);
}
