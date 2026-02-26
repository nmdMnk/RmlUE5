#include "RmlInterface/UERmlRenderInterface.h"
#include "Rendering/DrawElements.h"
#include "Render/RmlDrawer.h"
#include "Render/RmlMesh.h"
#include "RmlUiSettings.h"
#include "RmlWarmer.h"
#include "RmlUi/Core.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlHelper.h"
#include "Logging.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"

DECLARE_STATS_GROUP(TEXT("RmlUI_Interface"), STATGROUP_RmlUI_Interface, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("RmlUI CompileGeometry"), STAT_RmlUI_CompileGeometry, STATGROUP_RmlUI_Interface);
DECLARE_CYCLE_STAT(TEXT("RmlUI ReleaseGeometry"), STAT_RmlUI_ReleaseGeometry, STATGROUP_RmlUI_Interface);
DECLARE_CYCLE_STAT(TEXT("RmlUI RenderGeometry"),  STAT_RmlUI_RenderGeometry,  STATGROUP_RmlUI_Interface);

FUERmlRenderInterface::FUERmlRenderInterface()
	: AdditionRenderMatrix(FMatrix44f::Identity)
	, bCustomMatrix(false)
	, bUseClipRect(false)
{
}

bool FUERmlRenderInterface::SetTexture(FString Path, UTexture* InTexture, bool bAddIfNotExist)
{
	if (auto* FoundTexture = AllTextures.Find(Path))
	{
		(*FoundTexture)->BoundTexture = InTexture;
		return true;
	}

	if (bAddIfNotExist)
	{
		AllTextures.Add(Path, MakeShared<FRmlTextureEntry, ESPMode::ThreadSafe>(InTexture));
		return true;
	}

	return false;
}

void FUERmlRenderInterface::EndRender(FSlateWindowElementList& InCurrentElementList, uint32 InCurrentLayer)
{
	FSlateDrawElement::MakeCustom(InCurrentElementList, InCurrentLayer, CurrentDrawer);
	CurrentDrawer.Reset();
	CurrentContext = nullptr;
}

// ============================================================================
// Helpers
// ============================================================================

FMatrix44f FUERmlRenderInterface::ComputeNDCMatrix(Rml::Vector2f translation) const
{
	FMatrix44f Matrix = FMatrix44f::Identity;
	Matrix.M[3][0] = translation.x;
	Matrix.M[3][1] = translation.y;

	if (bCustomMatrix)
		Matrix *= AdditionRenderMatrix;

	Matrix *= RmlRenderMatrix;
	return Matrix;
}

FIntRect FUERmlRenderInterface::ComputeScissorRect() const
{
	FSlateRect EffectiveRect = bUseClipRect
		? TransformRect(RmlWidgetRenderTransform, ClipRect).IntersectionWith(ViewportRect)
		: ViewportRect;

	FIntRect Result;
	Result.Min.X = FMath::FloorToInt(EffectiveRect.Left);
	Result.Min.Y = FMath::FloorToInt(EffectiveRect.Top);
	Result.Max.X = FMath::CeilToInt(EffectiveRect.Right);
	Result.Max.Y = FMath::CeilToInt(EffectiveRect.Bottom);
	return Result;
}

// ============================================================================
// Required: geometry
// ============================================================================

Rml::CompiledGeometryHandle FUERmlRenderInterface::CompileGeometry(
	Rml::Span<const Rml::Vertex> vertices,
	Rml::Span<const int> indices)
{
	SCOPE_CYCLE_COUNTER(STAT_RmlUI_CompileGeometry);
	TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> Mesh = MakeShared<FRmlMesh, ESPMode::ThreadSafe>();
	Mesh->Setup(vertices, indices);

	Meshes.Add(Mesh.Get(), Mesh);
	return reinterpret_cast<Rml::CompiledGeometryHandle>(Mesh.Get());
}

void FUERmlRenderInterface::RenderGeometry(
	Rml::CompiledGeometryHandle geometry,
	Rml::Vector2f translation,
	Rml::TextureHandle texture)
{
	SCOPE_CYCLE_COUNTER(STAT_RmlUI_RenderGeometry);
	if (!CurrentDrawer.IsValid()) return;	// prewarm mode — no drawer

	FRmlTextureEntryPtr TextureRef;
	if (texture)
		TextureRef = reinterpret_cast<FRmlTextureEntry*>(texture)->AsShared();

	// Saved-layer textures (box-shadow, drop-shadow) are cached at physical pixel
	// resolution. When rendered back as a quad, the logical translation × DPI scale
	// often lands at a fractional physical pixel position, causing bilinear sampling
	// to smear every texel.  Snap the translation to the nearest physical pixel so
	// the texture maps 1:1 without interpolation artifacts.
	if (TextureRef && TextureRef->bIsSavedLayer)
	{
		FVector2f PhysPos = RmlWidgetRenderTransform.TransformPoint(
			FVector2f(translation.x, translation.y));
		PhysPos.X = FMath::RoundToFloat(PhysPos.X);
		PhysPos.Y = FMath::RoundToFloat(PhysPos.Y);
		FVector2f Snapped = RmlWidgetRenderTransform.Inverse().TransformPoint(PhysPos);
		translation.x = Snapped.X;
		translation.y = Snapped.Y;
	}

	FMatrix44f Matrix = ComputeNDCMatrix(translation);
	FIntRect ScissorRect = ComputeScissorRect();

	CurrentDrawer->EmplaceMesh(
		reinterpret_cast<FRmlMesh*>(geometry)->AsShared(),
		MoveTemp(TextureRef),
		Matrix,
		ScissorRect);
}

void FUERmlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	SCOPE_CYCLE_COUNTER(STAT_RmlUI_ReleaseGeometry);
	if (!geometry) return;
	// O(1) hash map lookup — was O(N) linear search causing O(N²) per frame
	// when the benchmark destroys all ~1000 meshes each SetInnerRML call.
	Meshes.Remove(reinterpret_cast<FRmlMesh*>(geometry));
}

// ============================================================================
// Required: textures
// ============================================================================

Rml::TextureHandle FUERmlRenderInterface::LoadTexture(
	Rml::Vector2i& texture_dimensions,
	const Rml::String& source)
{
	FString SourcePath(source.c_str());
	FString ResolvedFilePath = SourcePath;
	if (!SourcePath.StartsWith(TEXT("/")) && FPaths::IsRelative(SourcePath))
	{
		ResolvedFilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / SourcePath);
	}
	// ResolvedFilePath == SourcePath for asset paths (/Game/...) — safe to use as cache key always.

	auto FoundTexture = AllTextures.Find(ResolvedFilePath);
	if (FoundTexture)
	{
		texture_dimensions.x = (*FoundTexture)->BoundTexture->GetSurfaceWidth();
		texture_dimensions.y = (*FoundTexture)->BoundTexture->GetSurfaceHeight();
		return reinterpret_cast<Rml::TextureHandle>((*FoundTexture).Get());
	}

	// Cache miss — determine load method (filesystem I/O only on first load).
	const bool bIsFilePath = FPlatformFileManager::Get().GetPlatformFile().FileExists(*ResolvedFilePath);

	UTexture2D* LoadedTexture = bIsFilePath
		? FRmlHelper::LoadTextureFromFile(ResolvedFilePath)
		: FRmlHelper::LoadTextureFromAsset(SourcePath);

	if (!LoadedTexture) return 0;

	auto& AddedTexture = AllTextures.Add(ResolvedFilePath, MakeShared<FRmlTextureEntry, ESPMode::ThreadSafe>(LoadedTexture, ResolvedFilePath));
	AddedTexture->bPremultiplied = false;
	AddedTexture->bWrapSampler = true;	// Both file and asset textures must support wrapped sampling in decorators.
	texture_dimensions.x = LoadedTexture->GetSurfaceWidth();
	texture_dimensions.y = LoadedTexture->GetSurfaceHeight();
	return reinterpret_cast<Rml::TextureHandle>(AddedTexture.Get());
}

Rml::TextureHandle FUERmlRenderInterface::GenerateTexture(
	Rml::Span<const Rml::byte> source,
	Rml::Vector2i source_dimensions)
{
#if !UE_BUILD_SHIPPING
	// Capture mode: record all loaded document URLs from the active context
	// whenever a live texture is generated (CurrentDrawer set = live, not warmup).
	if (CurrentDrawer.IsValid() && CurrentContext && FRmlWarmer::IsCaptureEnabled())
	{
		const int32 NumDocs = CurrentContext->GetNumDocuments();
		for (int32 i = 0; i < NumDocs; ++i)
		{
			if (Rml::ElementDocument* Doc = CurrentContext->GetDocument(i))
				CapturedDocumentUrls.Add(FString(Doc->GetSourceURL().c_str()));
		}
	}
#endif

	// Dimension-based texture pool: when RmlUI's UpdateLayersOnDirty releases and
	// re-creates font-effect textures, the new textures often have the same dimensions.
	// Reusing a pooled UTexture2D avoids the expensive CreateTransient + UpdateResource
	// path — only the pixel data upload (memcpy + RHI update) remains.
	const uint64 DimKey = (static_cast<uint64>(source_dimensions.x) << 32)
						| static_cast<uint64>(source_dimensions.y);

	if (auto* Pool = TextureDimensionPool.Find(DimKey))
	{
		if (Pool->Num() > 0)
		{
			auto Reused = Pool->Pop();
			UTexture2D* Tex = Cast<UTexture2D>(Reused->BoundTexture.Get());
			if (Tex)
			{
				// Upload new pixel data into the existing GPU resource (cheap).
				const int32 DataSize = source_dimensions.x * source_dimensions.y * 4;
				uint8* DataCopy = new uint8[DataSize];
				FMemory::Memcpy(DataCopy, source.data(), DataSize);
				auto* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, source_dimensions.x, source_dimensions.y);
				Tex->UpdateTextureRegions(0, 1u, Region, 4 * source_dimensions.x, 4, DataCopy,
					[](uint8* D, const FUpdateTextureRegion2D* R) { delete[] D; delete R; });
			}

			auto& Entry = AllCreatedTextures.Add_GetRef(Reused);

			if (CurrentDrawer.IsValid())
			{
				UE_LOG(LogUERmlUI, Log, TEXT("GenerateTexture %dx%d [POOL HIT - instant] cached=%d pool=%d"),
					source_dimensions.x, source_dimensions.y, AllCreatedTextures.Num(), Pool->Num());
			}
			else
			{
				++WarmupTextureCount;
			}
			return reinterpret_cast<Rml::TextureHandle>(Entry.Get());
		}
	}

	// Pool empty for this dimension — create a new UTexture2D.
	UTexture2D* Texture = FRmlHelper::LoadTextureFromRaw(
		(const uint8*)source.data(),
		FIntPoint(source_dimensions.x, source_dimensions.y));

	auto& Entry = AllCreatedTextures.Add_GetRef(MakeShared<FRmlTextureEntry, ESPMode::ThreadSafe>(Texture));
	Entry->bPremultiplied = true;

	if (CurrentDrawer.IsValid())
	{
		UE_LOG(LogUERmlUI, Log, TEXT("GenerateTexture %dx%d [LIVE - no pool] cached=%d"),
			source_dimensions.x, source_dimensions.y, AllCreatedTextures.Num());
	}
	else
	{
		++WarmupTextureCount;
		UE_LOG(LogUERmlUI, Verbose, TEXT("GenerateTexture %dx%d [warmup] cached=%d"),
			source_dimensions.x, source_dimensions.y, AllCreatedTextures.Num());
	}

	return reinterpret_cast<Rml::TextureHandle>(Entry.Get());
}

void FUERmlRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	if (!texture) return;

	FRmlTextureEntry* RawPtr = reinterpret_cast<FRmlTextureEntry*>(texture);

	// Check generated textures — pool them instead of destroying.
	for (int32 i = AllCreatedTextures.Num() - 1; i >= 0; --i)
	{
		if (AllCreatedTextures[i].Get() == RawPtr)
		{
			auto Entry = AllCreatedTextures[i];
			AllCreatedTextures.RemoveAtSwap(i);

			// Pool the entry for future reuse (FGCObject prevents UTexture2D from being GC'd).
			UTexture2D* Tex = Cast<UTexture2D>(Entry->BoundTexture.Get());
			if (Tex)
			{
				const uint64 Key = (static_cast<uint64>(Tex->GetSizeX()) << 32)
								 | static_cast<uint64>(Tex->GetSizeY());
				TextureDimensionPool.FindOrAdd(Key).Add(Entry);
				UE_LOG(LogUERmlUI, Verbose, TEXT("ReleaseTexture %dx%d → pooled  cached=%d pool=%d"),
					(int32)Tex->GetSizeX(), (int32)Tex->GetSizeY(),
					AllCreatedTextures.Num(), TextureDimensionPool[Key].Num());
			}
			return;
		}
	}

	// Validate against known loaded (file/asset) textures.
	for (auto It = AllTextures.CreateIterator(); It; ++It)
	{
		if (It->Value.Get() == RawPtr)
		{
			It.RemoveCurrent();
			return;
		}
	}

	UE_LOG(LogUERmlUI, Warning, TEXT("ReleaseTexture: handle 0x%llX not found — already released or stale"), (uint64)texture);
}

// ============================================================================
// Texture pre-allocation
// ============================================================================

void FUERmlRenderInterface::PreallocateTextureReserves()
{
	// Count textures per dimension from warmup results.
	TMap<uint64, int32> ObservedDims;
	for (const auto& Entry : AllCreatedTextures)
	{
		if (UTexture2D* Tex = Cast<UTexture2D>(Entry->BoundTexture.Get()))
		{
			const uint64 Key = (static_cast<uint64>(Tex->GetSizeX()) << 32)
							 | static_cast<uint64>(Tex->GetSizeY());
			ObservedDims.FindOrAdd(Key, 0)++;
		}
	}

	int32 TotalCreated = 0;
	for (auto& Pair : ObservedDims)
	{
		const int32 Width  = static_cast<int32>(Pair.Key >> 32);
		const int32 Height = static_cast<int32>(Pair.Key & 0xFFFFFFFF);

		auto& Pool = TextureDimensionPool.FindOrAdd(Pair.Key);
		const int32 ToAdd = FMath::Max(0, Pair.Value - Pool.Num());

		for (int32 i = 0; i < ToAdd; ++i)
		{
			// Match LoadTextureFromRaw settings exactly.
			UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, PF_R8G8B8A8);
			Tex->SRGB = false;
			Tex->Filter = TF_Bilinear;
			Tex->NeverStream = true;
			Tex->UpdateResource();

			auto NewEntry = MakeShared<FRmlTextureEntry, ESPMode::ThreadSafe>(Tex);
			NewEntry->bPremultiplied = true;
			Pool.Add(NewEntry);
			++TotalCreated;
		}

		UE_LOG(LogUERmlUI, Log, TEXT("PreallocateTextureReserves: %dx%d → %d reserves (observed %d active)"),
			Width, Height, Pool.Num(), Pair.Value);
	}

	if (TotalCreated > 0)
	{
		FlushRenderingCommands();
	}

	UE_LOG(LogUERmlUI, Log, TEXT("PreallocateTextureReserves: created %d reserve textures across %d dimensions"),
		TotalCreated, ObservedDims.Num());
}

// ============================================================================
// Required: scissor
// ============================================================================

void FUERmlRenderInterface::EnableScissorRegion(bool enable)
{
	bUseClipRect = enable;
}

void FUERmlRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	ClipRect = FSlateRect(
		(float)FMath::Max(region.Left(), 0),
		(float)FMath::Max(region.Top(), 0),
		(float)region.Right(),
		(float)region.Bottom());
}

// ============================================================================
// Optional: transform
// ============================================================================

void FUERmlRenderInterface::SetTransform(const Rml::Matrix4f* transform)
{
	if (transform)
	{
		FMemory::Memcpy(&AdditionRenderMatrix, transform->data(), sizeof(float) * 16);
		bCustomMatrix = true;
	}
	else
	{
		AdditionRenderMatrix = FMatrix44f::Identity;
		bCustomMatrix = false;
	}
}

// ============================================================================
// Optional: clip mask (Phase 2)
// ============================================================================

void FUERmlRenderInterface::EnableClipMask(bool enable)
{
	if (!CurrentDrawer.IsValid()) return;
	UE_LOG(LogUERmlUI, Verbose, TEXT("EnableClipMask(%s)"), enable ? TEXT("true") : TEXT("false"));
	CurrentDrawer->EmplaceEnableClipMask(enable);
}

void FUERmlRenderInterface::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation)
{
	if (!CurrentDrawer.IsValid()) return;

	const TCHAR* OpStr;
	switch (operation)
	{
	case Rml::ClipMaskOperation::Set:        OpStr = TEXT("Set"); break;
	case Rml::ClipMaskOperation::SetInverse: OpStr = TEXT("SetInverse"); break;
	default:                                 OpStr = TEXT("Intersect"); break;
	}
	UE_LOG(LogUERmlUI, Verbose, TEXT("RenderToClipMask(%s, geo=0x%p, trans=%.1f,%.1f)"), OpStr, (void*)geometry, translation.x, translation.y);

	FMatrix44f Matrix = ComputeNDCMatrix(translation);
	FIntRect ScissorRect = ComputeScissorRect();

	CurrentDrawer->EmplaceRenderToClipMask(
		operation,
		reinterpret_cast<FRmlMesh*>(geometry)->AsShared(),
		Matrix,
		ScissorRect);
}

// ============================================================================
// Optional: layers (Phase 3)
// ============================================================================

Rml::LayerHandle FUERmlRenderInterface::PushLayer()
{
	int32 NewLayerIndex = ++LayerCounter;
	if (!CurrentDrawer.IsValid()) return (Rml::LayerHandle)NewLayerIndex;	// prewarm
	UE_LOG(LogUERmlUI, Verbose, TEXT("PushLayer() -> %d"), NewLayerIndex);
	CurrentDrawer->EmplacePushLayer(NewLayerIndex);
	return (Rml::LayerHandle)NewLayerIndex;
}

void FUERmlRenderInterface::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode, Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	if (!CurrentDrawer.IsValid()) return;

	UE_LOG(LogUERmlUI, Verbose, TEXT("CompositeLayers(src=%d, dst=%d, blend=%d, filters=%d)"),
		(int32)source, (int32)destination, (int32)blend_mode, (int32)filters.size());

	// Resolve filter handles to shared pointers NOW (game thread) so the
	// command keeps the filter alive until the render thread processes it.
	// Without this, filters compiled and released inside a CallbackTexture
	// (e.g. box-shadow blur) would be removed from the map before the
	// render thread can look them up.
	TArray<TSharedPtr<FCompiledRmlFilter>> ResolvedFilters;
	ResolvedFilters.Reserve(filters.size());
	for (auto f : filters)
	{
		if (auto* Found = CompiledFilters.Find(f))
			ResolvedFilters.Add(*Found);
	}

	// Capture current scissor rect — RmlUI calls ApplyClippingRegion just before
	// CompositeLayers, so the active scissor represents the intended composite region.
	// GL3 reference keeps this scissor active during the fullscreen quad composite;
	// we pass it explicitly through the command buffer.
	const FIntRect ScissorRect = ComputeScissorRect();

	CurrentDrawer->EmplaceCompositeLayers(
		(int32)source,
		(int32)destination,
		blend_mode,
		MoveTemp(ResolvedFilters),
		ScissorRect);
}

void FUERmlRenderInterface::PopLayer()
{
	--LayerCounter;
	if (!CurrentDrawer.IsValid()) return;
	UE_LOG(LogUERmlUI, Verbose, TEXT("PopLayer() -> counter=%d"), LayerCounter);
	CurrentDrawer->EmplacePopLayer();
}

// ============================================================================
// Optional: layer snapshots (Phase 3)
// ============================================================================

Rml::TextureHandle FUERmlRenderInterface::SaveLayerAsTexture()
{
	// In prewarm mode (no active drawer) layer rendering has no effect, so we
	// cannot produce a valid GPU texture.  Return 0 so the CallbackTexture does
	// not cache a stale handle and retries the callback on the first live frame.
	if (!CurrentDrawer.IsValid())
		return 0;

	// Pre-allocate a texture entry on the game thread. The render thread will
	// fill in OverrideRHI with the actual GPU texture via ExecuteSaveLayerAsTexture.
	auto& Entry = AllCreatedTextures.Add_GetRef(MakeShared<FRmlTextureEntry, ESPMode::ThreadSafe>());
	Entry->bPremultiplied = true;	// layer content is already premultiplied
	Entry->bIsSavedLayer  = true;	// cached callback texture — needs pixel-aligned rendering

	FIntRect ScissorRect = ComputeScissorRect();

	// Compute the ideal texture dimensions that match the GPU rasterizer's pixel
	// counting when drawing the saved texture back as a quad.  ComputeScissorRect
	// uses Floor/Ceil which can be 1 pixel wider than what the rasterizer covers.
	// RoundToInt matches rasterizer behavior (pixel center at i+0.5 is inside if
	// center < right_edge), eliminating the sub-pixel mismatch that causes bilinear
	// interpolation artifacts at box-shadow / drop-shadow edges.
	FIntPoint IdealSize(ScissorRect.Width(), ScissorRect.Height());
	if (bUseClipRect)
	{
		auto TransRect = TransformRect(RmlWidgetRenderTransform, ClipRect);
		IdealSize.X = FMath::Max(1, FMath::RoundToInt(TransRect.Right - TransRect.Left));
		IdealSize.Y = FMath::Max(1, FMath::RoundToInt(TransRect.Bottom - TransRect.Top));
	}

	CurrentDrawer->EmplaceSaveLayerAsTexture(Entry, ScissorRect, IdealSize);

	UE_LOG(LogUERmlUI, Verbose, TEXT("SaveLayerAsTexture -> 0x%p  scissor=[%d,%d]-[%d,%d] ideal=%dx%d"),
		(void*)Entry.Get(), ScissorRect.Min.X, ScissorRect.Min.Y, ScissorRect.Max.X, ScissorRect.Max.Y,
		IdealSize.X, IdealSize.Y);

	return reinterpret_cast<Rml::TextureHandle>(Entry.Get());
}

Rml::CompiledFilterHandle FUERmlRenderInterface::SaveLayerAsMaskImage()
{
	// Create a MaskImage filter whose MaskTexture will be filled by the render
	// thread via ExecuteSaveLayerAsMaskImage.
	auto Filter = MakeShared<FCompiledRmlFilter>();
	Filter->Type = ERmlFilterType::MaskImage;

	Rml::CompiledFilterHandle Handle = NextFilterHandle++;
	CompiledFilters.Add(Handle, Filter);

	FCompiledRmlFilter* FilterPtr = &Filter.Get();
	FIntRect ScissorRect = ComputeScissorRect();

	if (CurrentDrawer.IsValid())
	{
		CurrentDrawer->EmplaceSaveLayerAsMaskImage(FilterPtr, ScissorRect);
	}

	UE_LOG(LogUERmlUI, Verbose, TEXT("SaveLayerAsMaskImage -> handle=%d  scissor=[%d,%d]-[%d,%d]"),
		(int32)Handle, ScissorRect.Min.X, ScissorRect.Min.Y, ScissorRect.Max.X, ScissorRect.Max.Y);

	return Handle;
}

// ============================================================================
// Optional: filters (Phase 4)
// ============================================================================

Rml::CompiledFilterHandle FUERmlRenderInterface::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto Filter = MakeShared<FCompiledRmlFilter>();

	if (name == "opacity")
	{
		// NOTE: Do NOT return 0 for opacity(1). It would suppress PushLayer, breaking:
		// 1. Layer isolation (stacking context) for elements like .boxshadow_trail that
		//    use filter:opacity(1) specifically for correct box-shadow ink overflow clipping.
		// 2. CSS animations that interpolate through opacity(1) at keyframe boundaries:
		//    mismatched handle counts between keyframes corrupt interpolation.
		// The render-time skip (BlendFactor >= 1.0 → break in ApplyFilters) is sufficient.
		auto it = parameters.find("value");
		Filter->Type = ERmlFilterType::Passthrough;
		Filter->BlendFactor = (it != parameters.end()) ? it->second.Get<float>(1.0f) : 1.0f;
	}
	else if (name == "blur")
	{
		// RmlUI passes the Gaussian sigma directly as "sigma" (not "radius").
		auto it = parameters.find("sigma");
		Filter->Type = ERmlFilterType::Blur;
		Filter->Sigma = (it != parameters.end()) ? it->second.Get<float>(0.0f) : 0.0f;
		// NOTE: Do NOT return 0 for blur(0). RmlUI logs "Could not compile filter" for
		// every 0 return, causing per-frame warning spam for data-bound filter strings
		// that include blur(0px) at default slider values. The render-time skip
		// (Sigma < 0.01 → break in ApplyFilters) suppresses the no-op pass instead.
	}
	else if (name == "drop-shadow")
	{
		Filter->Type = ERmlFilterType::DropShadow;
		auto itColor = parameters.find("color");
		if (itColor != parameters.end())
		{
			Rml::Colourb c = itColor->second.Get<Rml::Colourb>(Rml::Colourb(0, 0, 0, 255));
			Filter->Color = FLinearColor(c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f, c.alpha / 255.0f);
		}
		// RmlUI passes offset as a single Vector2f, not separate x/y.
		auto itOffset = parameters.find("offset");
		if (itOffset != parameters.end())
		{
			Rml::Vector2f off = itOffset->second.Get<Rml::Vector2f>(Rml::Vector2f(0, 0));
			Filter->Offset = FVector2f(off.x, off.y);
		}
		// Sigma is already the Gaussian standard deviation — no 0.5 multiplication.
		auto itSigma = parameters.find("sigma");
		if (itSigma != parameters.end())
			Filter->Sigma = itSigma->second.Get<float>(0.0f);
	}
	else if (name == "brightness" || name == "contrast" || name == "invert" ||
	         name == "grayscale" || name == "sepia" || name == "saturate" || name == "hue-rotate")
	{
		float value = 1.0f;
		auto it = parameters.find("value");
		if (it != parameters.end())
			value = it->second.Get<float>(1.0f);

		// NOTE: Do NOT return 0 for identity color matrix values (brightness(1), sepia(0),
		// etc.). RmlUI logs "Could not compile filter" for every 0 return — the data-bound
		// filter string on the outer div emits 8 identity color filters per frame at default
		// slider values, causing 8 warnings/frame of log spam. The render-time checks in
		// ApplyFilters (ColorMatrix == Identity → break) and ExecuteCompositeLayers
		// (bAllIdentity fast path) suppress the no-op passes without warnings.
		Filter->Type = ERmlFilterType::ColorMatrix;

		// Build 4x4 color matrix based on filter type
		FMatrix44f M = FMatrix44f::Identity;

		if (name == "brightness")
		{
			// Scale RGB by value
			M.M[0][0] = value; M.M[1][1] = value; M.M[2][2] = value;
		}
		else if (name == "contrast")
		{
			// Scale and offset: c = (c - 0.5) * value + 0.5
			// As matrix: scale RGB, add offset via last row
			float t = (1.0f - value) * 0.5f;
			M.M[0][0] = value; M.M[1][1] = value; M.M[2][2] = value;
			// Offset encoded in row 3 (treated as translation in color space)
			M.M[3][0] = t; M.M[3][1] = t; M.M[3][2] = t;
		}
		else if (name == "invert")
		{
			// Invert: c = 1 - c (with value as interpolation factor)
			float v = value;
			M.M[0][0] = 1.0f - 2.0f * v; M.M[1][1] = 1.0f - 2.0f * v; M.M[2][2] = 1.0f - 2.0f * v;
			M.M[3][0] = v; M.M[3][1] = v; M.M[3][2] = v;
		}
		else if (name == "grayscale")
		{
			// Luminance weights
			float r = 0.2126f, g = 0.7152f, b = 0.0722f;
			float inv = 1.0f - value;
			M.M[0][0] = inv + value * r; M.M[0][1] = value * r;       M.M[0][2] = value * r;
			M.M[1][0] = value * g;       M.M[1][1] = inv + value * g; M.M[1][2] = value * g;
			M.M[2][0] = value * b;       M.M[2][1] = value * b;       M.M[2][2] = inv + value * b;
		}
		else if (name == "sepia")
		{
			// Sepia tone matrix (mixed with identity by value)
			float inv = 1.0f - value;
			M.M[0][0] = inv + value * 0.393f; M.M[0][1] = value * 0.349f;       M.M[0][2] = value * 0.272f;
			M.M[1][0] = value * 0.769f;       M.M[1][1] = inv + value * 0.686f; M.M[1][2] = value * 0.534f;
			M.M[2][0] = value * 0.189f;       M.M[2][1] = value * 0.168f;       M.M[2][2] = inv + value * 0.131f;
		}
		else if (name == "saturate")
		{
			float r = 0.2126f, g = 0.7152f, b = 0.0722f;
			float s = value;
			M.M[0][0] = (1.0f - s) * r + s; M.M[0][1] = (1.0f - s) * r;       M.M[0][2] = (1.0f - s) * r;
			M.M[1][0] = (1.0f - s) * g;     M.M[1][1] = (1.0f - s) * g + s;   M.M[1][2] = (1.0f - s) * g;
			M.M[2][0] = (1.0f - s) * b;     M.M[2][1] = (1.0f - s) * b;       M.M[2][2] = (1.0f - s) * b + s;
		}
		else if (name == "hue-rotate")
		{
			// RmlUI already converts degrees to radians before passing to CompileFilter.
			float rad = value;
			float c = FMath::Cos(rad), s = FMath::Sin(rad);
			float r = 0.2126f, g = 0.7152f, b = 0.0722f;

			M.M[0][0] = r + c * (1 - r) + s * (-r);
			M.M[0][1] = r + c * (-r)    + s * (0.143f);
			M.M[0][2] = r + c * (-r)    + s * (-(1 - r));

			M.M[1][0] = g + c * (-g)    + s * (-g);
			M.M[1][1] = g + c * (1 - g) + s * (0.140f);
			M.M[1][2] = g + c * (-g)    + s * (g);

			M.M[2][0] = b + c * (-b)    + s * (1 - b);
			M.M[2][1] = b + c * (-b)    + s * (-0.283f);
			M.M[2][2] = b + c * (1 - b) + s * (b);
		}

		Filter->ColorMatrix = M;
	}
	else
	{
		UE_LOG(LogUERmlUI, Warning, TEXT("CompileFilter: unknown filter '%hs'"), name.c_str());
		return 0;
	}

	Rml::CompiledFilterHandle Handle = NextFilterHandle++;
	CompiledFilters.Add(Handle, Filter);
	return Handle;
}

void FUERmlRenderInterface::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	if (filter == 0) return;	// Identity filters return 0 from CompileFilter — nothing to release

	// Defer removal — the render thread may still reference this filter handle
	// in a CompositeLayers command recorded earlier this frame (e.g. box-shadow
	// compiles + uses + releases blur filters within a single callback).
	PendingFilterReleases.Add(filter);
}

// ============================================================================
// Optional: shaders (Phase 5)
// ============================================================================

Rml::CompiledShaderHandle FUERmlRenderInterface::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto Shader = MakeShared<FCompiledRmlShader>();

	auto GetVector2 = [&parameters](const char* Key, const Rml::Vector2f& DefaultValue) -> Rml::Vector2f
	{
		if (const auto It = parameters.find(Key); It != parameters.end())
			return It->second.Get<Rml::Vector2f>(DefaultValue);
		return DefaultValue;
	};

	auto GetFloat = [&parameters](const char* Key, float DefaultValue) -> float
	{
		if (const auto It = parameters.find(Key); It != parameters.end())
			return It->second.Get<float>(DefaultValue);
		return DefaultValue;
	};

	auto GetBool = [&parameters](const char* Key, bool DefaultValue) -> bool
	{
		if (const auto It = parameters.find(Key); It != parameters.end())
			return It->second.Get<bool>(DefaultValue);
		return DefaultValue;
	};

	auto ApplyColorStopList = [&parameters](FCompiledRmlShader& OutShader) -> bool
	{
		const auto It = parameters.find("color_stop_list");
		if (It == parameters.end() || It->second.GetType() != Rml::Variant::COLORSTOPLIST)
			return false;

		const Rml::ColorStopList& ColorStopList = It->second.GetReference<Rml::ColorStopList>();
		const int32 NumStops = FMath::Min(static_cast<int32>(ColorStopList.size()), 16);

		OutShader.StopPositions.Reset(NumStops);
		OutShader.StopColors.Reset(NumStops);

		for (int32 i = 0; i < NumStops; ++i)
		{
			const Rml::ColorStop& Stop = ColorStopList[i];
			const Rml::ColourbPremultiplied& Color = Stop.color;

			OutShader.StopPositions.Add(Stop.position.number);
			OutShader.StopColors.Add(FLinearColor(
				Color.red / 255.0f,
				Color.green / 255.0f,
				Color.blue / 255.0f,
				Color.alpha / 255.0f));
		}

		return OutShader.StopPositions.Num() > 0;
	};

	if (name == "shader")
	{
		const auto ItValue = parameters.find("value");
		const Rml::String Value = (ItValue != parameters.end())
			? ItValue->second.Get<Rml::String>(Rml::String())
			: Rml::String();

		if (Value != "creation")
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("CompileShader: unsupported shader value '%hs'"), Value.c_str());
			return 0;
		}

		Shader->Type = ERmlShaderType::Creation;
	}
	else
	{
		Shader->Type = ERmlShaderType::Gradient;
		const bool bNamedRepeatingLinear = (name == "repeating-linear-gradient");
		const bool bNamedRepeatingRadial = (name == "repeating-radial-gradient");
		const bool bNamedRepeatingConic = (name == "repeating-conic-gradient");

		const bool bIsLinearGradient = (name == "linear-gradient" || bNamedRepeatingLinear);
		const bool bIsRadialGradient = (name == "radial-gradient" || bNamedRepeatingRadial);
		const bool bIsConicGradient = (name == "conic-gradient" || bNamedRepeatingConic);

		if (!bIsLinearGradient && !bIsRadialGradient && !bIsConicGradient)
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("CompileShader: unknown shader '%hs'"), name.c_str());
			return 0;
		}

		const bool bRepeating = GetBool("repeating", false) || bNamedRepeatingLinear || bNamedRepeatingRadial || bNamedRepeatingConic;

		if (bIsLinearGradient)
		{
			Shader->GradientFunc = bRepeating ? ERmlGradientFunc::RepeatingLinear : ERmlGradientFunc::Linear;
			const Rml::Vector2f P0 = GetVector2("p0", Rml::Vector2f(0.0f, 0.0f));
			const Rml::Vector2f P1 = GetVector2("p1", Rml::Vector2f(0.0f, 0.0f));
			Shader->P = FVector2f(P0.x, P0.y);
			Shader->V = FVector2f(P1.x - P0.x, P1.y - P0.y);
		}
		else if (bIsRadialGradient)
		{
			Shader->GradientFunc = bRepeating ? ERmlGradientFunc::RepeatingRadial : ERmlGradientFunc::Radial;
			const Rml::Vector2f Center = GetVector2("center", Rml::Vector2f(0.0f, 0.0f));
			const Rml::Vector2f Radius = GetVector2("radius", Rml::Vector2f(1.0f, 1.0f));

			const float SafeRadiusX = FMath::Abs(Radius.x) > KINDA_SMALL_NUMBER ? Radius.x : 1.0f;
			const float SafeRadiusY = FMath::Abs(Radius.y) > KINDA_SMALL_NUMBER ? Radius.y : 1.0f;

			Shader->P = FVector2f(Center.x, Center.y);
			Shader->V = FVector2f(1.0f / SafeRadiusX, 1.0f / SafeRadiusY);
		}
		else
		{
			Shader->GradientFunc = bRepeating ? ERmlGradientFunc::RepeatingConic : ERmlGradientFunc::Conic;
			const Rml::Vector2f Center = GetVector2("center", Rml::Vector2f(0.0f, 0.0f));
			const float Angle = GetFloat("angle", 0.0f);

			Shader->P = FVector2f(Center.x, Center.y);
			Shader->V = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle));
		}

		if (!ApplyColorStopList(*Shader))
		{
			UE_LOG(LogUERmlUI, Warning, TEXT("CompileShader: missing or invalid color_stop_list for '%hs'"), name.c_str());
			return 0;
		}
	}

	Rml::CompiledShaderHandle Handle = NextShaderHandle++;
	CompiledShaders.Add(Handle, Shader);
	return Handle;
}

void FUERmlRenderInterface::RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	if (!CurrentDrawer.IsValid()) return;

	FMatrix44f Matrix = ComputeNDCMatrix(translation);
	FIntRect ScissorRect = ComputeScissorRect();

	FRmlTextureEntryPtr TextureRef;
	if (texture)
		TextureRef = reinterpret_cast<FRmlTextureEntry*>(texture)->AsShared();
	CurrentDrawer->EmplaceDrawShader(
		shader,
		reinterpret_cast<FRmlMesh*>(geometry)->AsShared(),
		MoveTemp(TextureRef),
		Matrix,
		ScissorRect);
}

void FUERmlRenderInterface::ReleaseShader(Rml::CompiledShaderHandle shader)
{
	CompiledShaders.Remove(shader);
}

// ============================================================================
// Drawer allocation
// ============================================================================

TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe> FUERmlRenderInterface::_AllocDrawer()
{
	const int32 SampleCount = URmlUiSettings::Get()->GetMSAASampleCount();

	// Find a free drawer or create one.
	TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe>* Found = nullptr;
	for (auto& Drawer : AllDrawers)
	{
		if (Drawer->IsFree())
		{
			Drawer->MarkUsing();
			Found = &Drawer;
			break;
		}
	}

	if (!Found)
	{
		Found = &AllDrawers.Add_GetRef(MakeShared<FRmlDrawer, ESPMode::ThreadSafe>(true));
	}

	(*Found)->SetMSAASamples(SampleCount);
	(*Found)->CompiledShaders = &CompiledShaders;
	return *Found;
}
