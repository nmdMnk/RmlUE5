#pragma once
#include "RmlUi/Core/RenderInterface.h"
#include "Render/TextureEntries.h"

namespace Rml { class Context; class ElementDocument; }
class FRmlMesh;
class FRmlDrawer;
struct FCompiledRmlFilter;
struct FCompiledRmlShader;

class UERMLUI_API FUERmlRenderInterface : public Rml::RenderInterface
{
public:
	FUERmlRenderInterface();

	bool SetTexture(FString Path, UTexture* InTexture, bool bAddIfNotExist = true);
	TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe> GetTexture() { return AllTextures.begin().Value(); }
	const TArray<TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>>& GetCreatedTextures() const { return AllCreatedTextures; }

	void BeginRender(
		    Rml::Context*				InContext,
		    const FSlateRenderTransform&	InRmlWidgetRenderTransform,
		    const FMatrix44f&			InRmlRenderMatrix,
		    const FSlateRect&			InViewportRect)
	{
		// Flush deferred filter releases — the previous frame's render thread
		// has finished with these handles by now.
		for (Rml::CompiledFilterHandle H : PendingFilterReleases)
			CompiledFilters.Remove(H);
		PendingFilterReleases.Reset();

		CurrentContext           = InContext;
		RmlWidgetRenderTransform = InRmlWidgetRenderTransform;
		RmlRenderMatrix          = InRmlRenderMatrix;
		ViewportRect             = InViewportRect;
		CurrentDrawer            = _AllocDrawer();
		LayerCounter             = 0;
	}
	void EndRender(FSlateWindowElementList& InCurrentElementList, uint32 InCurrentLayer);

	// Warmup cycle tracking — call ResetWarmupCounter() before each Render(),
	// then GetWarmupTextureCount() after to check if new textures were generated.
	void ResetWarmupCounter() { WarmupTextureCount = 0; }
	int32 GetWarmupTextureCount() const { return WarmupTextureCount; }

	// Pre-allocate spare UTexture2D objects matching the count per dimension
	// observed in AllCreatedTextures. Call after warmup completes.
	void PreallocateTextureReserves();

protected:
	// ~Begin Rml::RenderInterface API (6.2)

	// Required: geometry
	virtual Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	virtual void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	virtual void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	// Required: textures
	virtual Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	virtual Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
	virtual void ReleaseTexture(Rml::TextureHandle texture) override;

	// Required: scissor
	virtual void EnableScissorRegion(bool enable) override;
	virtual void SetScissorRegion(Rml::Rectanglei region) override;

	// Optional: transform
	virtual void SetTransform(const Rml::Matrix4f* transform) override;

	// Optional: clip mask (Phase 2)
	virtual void EnableClipMask(bool enable) override;
	virtual void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

	// Optional: layers (Phase 3)
	virtual Rml::LayerHandle PushLayer() override;
	virtual void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode, Rml::Span<const Rml::CompiledFilterHandle> filters) override;
	virtual void PopLayer() override;

	// Optional: layer snapshots (Phase 3)
	virtual Rml::TextureHandle SaveLayerAsTexture() override;
	virtual Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

	// Optional: filters (Phase 4)
	virtual Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	virtual void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

	// Optional: shaders (Phase 5)
	virtual Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	virtual void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	virtual void ReleaseShader(Rml::CompiledShaderHandle shader) override;

	// ~End Rml::RenderInterface API

	TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe> _AllocDrawer();

	// Helpers
	FMatrix44f ComputeNDCMatrix(Rml::Vector2f translation) const;
	FIntRect ComputeScissorRect() const;

	// render state
	Rml::Context*								CurrentContext = nullptr;
	FSlateRenderTransform						RmlWidgetRenderTransform;
	FMatrix44f									RmlRenderMatrix;
	FSlateRect									ViewportRect;
	TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe>	CurrentDrawer;

#if !UE_BUILD_SHIPPING
	// Capture mode: document URLs recorded when live textures are generated.
	TSet<FString>								CapturedDocumentUrls;
public:
	const TSet<FString>& GetCapturedDocumentUrls() const { return CapturedDocumentUrls; }
protected:
#endif

	// internal state
	FMatrix44f									AdditionRenderMatrix;
	bool										bCustomMatrix;
	bool										bUseClipRect;
	FSlateRect									ClipRect;

	// layer counter (game-thread side)
	int32										LayerCounter = 0;

	// warmup texture counter (encapsulated — use ResetWarmupCounter / GetWarmupTextureCount)
	int32										WarmupTextureCount = 0;

	// textures
	TMap<FString, TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>>	AllTextures;
	TArray<TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>>			AllCreatedTextures;

	// meshes — keyed by raw pointer for O(1) ReleaseGeometry lookup
	// (linear TArray search was O(N²) when benchmark destroys 1000+ meshes/frame)
	TMap<FRmlMesh*, TSharedPtr<FRmlMesh, ESPMode::ThreadSafe>>			Meshes;

	// drawers
	TArray<TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe>>					AllDrawers;

	// compiled filters (owned by this interface, shared with drawers)
	TMap<Rml::CompiledFilterHandle, TSharedPtr<FCompiledRmlFilter>>		CompiledFilters;
	Rml::CompiledFilterHandle											NextFilterHandle = 1;

	// Deferred filter releases — flushed in BeginRender after the render thread
	// has processed all commands referencing these handles.
	TArray<Rml::CompiledFilterHandle>									PendingFilterReleases;

	// compiled shaders
	TMap<Rml::CompiledShaderHandle, TSharedPtr<FCompiledRmlShader>>		CompiledShaders;
	Rml::CompiledShaderHandle											NextShaderHandle = 1;

	// Dimension-based texture pool. When RmlUI releases a generated texture, the
	// FRmlTextureEntry (and its UTexture2D) is pooled here instead of destroyed.
	// On the next GenerateTexture with matching dimensions, the pooled UTexture2D is
	// reused with just UpdateTextureRegions (memcpy), skipping the expensive
	// UTexture2D::CreateTransient + UpdateResource path.
	// Key: (Width << 32) | Height
	TMap<uint64, TArray<TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>>>	TextureDimensionPool;
};
