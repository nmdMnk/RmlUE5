#pragma once
#include "Render/TextureEntries.h"
#include "RmlUi/Core/RenderInterface.h"

class FRmlMesh;
class FRmlDrawer;

class UERMLUI_API FUERmlRenderInterface : public Rml::RenderInterface
{
public:
	FUERmlRenderInterface();

	bool SetTexture(FString Path, UTexture* InTexture, bool bAddIfNotExist = true);
	TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe> GetTexture() { return AllTextures.begin().Value(); }
	const TArray<TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>>& GetCreatedTextures() { return AllCreatedTextures; }

	void BeginRender(
		    const FSlateRenderTransform&	InRmlWidgetRenderTransform,
		    const FMatrix44f&			InRmlRenderMatrix,
		    const FSlateRect&			InViewportRect)
	{
		RmlWidgetRenderTransform = InRmlWidgetRenderTransform;
		RmlRenderMatrix          = InRmlRenderMatrix;
		ViewportRect             = InViewportRect;
		CurrentDrawer            = _AllocDrawer();
	}
	void EndRender(FSlateWindowElementList& InCurrentElementList, uint32 InCurrentLayer);

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

	// ~End Rml::RenderInterface API

	TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe> _AllocDrawer();

protected:
	// render state
	FSlateRenderTransform						RmlWidgetRenderTransform;
	FMatrix44f									RmlRenderMatrix;
	FSlateRect									ViewportRect;
	TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe>	CurrentDrawer;

	// internal state
	FMatrix44f									AdditionRenderMatrix;
	bool										bCustomMatrix;
	bool										bUseClipRect;
	FSlateRect									ClipRect;

	// textures
	TMap<FString, TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>>	AllTextures;
	TArray<TSharedPtr<FRmlTextureEntry, ESPMode::ThreadSafe>>			AllCreatedTextures;

	// meshes
	TArray<TSharedPtr<FRmlMesh, ESPMode::ThreadSafe>>					Meshes;

	// drawers
	TArray<TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe>>					AllDrawers;
};
