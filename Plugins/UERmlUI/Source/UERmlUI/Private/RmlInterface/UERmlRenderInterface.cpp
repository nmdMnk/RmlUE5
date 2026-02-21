#include "RmlInterface/UERmlRenderInterface.h"
#include "Rendering/DrawElements.h"
#include "Render/RmlDrawer.h"
#include "Render/RmlMesh.h"
#include "RmlUiSettings.h"
#include "RmlUi/Core.h"
#include "RmlHelper.h"

FUERmlRenderInterface::FUERmlRenderInterface()
	: AdditionRenderMatrix(FMatrix44f::Identity)
	, bCustomMatrix(false)
	, bUseClipRect(false)
{
}

bool FUERmlRenderInterface::SetTexture(FString Path, UTexture* InTexture, bool bAddIfNotExist)
{
	auto FoundTexture = AllTextures.Find(Path);
	if (FoundTexture)
	{
		(*FoundTexture)->BoundTexture = InTexture;
		return true;
	}
	else if (bAddIfNotExist)
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
}

Rml::CompiledGeometryHandle FUERmlRenderInterface::CompileGeometry(
	Rml::Span<const Rml::Vertex> vertices,
	Rml::Span<const int> indices)
{
	TSharedPtr<FRmlMesh, ESPMode::ThreadSafe> Mesh = MakeShared<FRmlMesh, ESPMode::ThreadSafe>();
	Mesh->Setup(vertices, indices);
	Mesh->BuildMesh();
	
	// add to array
	Meshes.Add(Mesh);

	// as handle 
	return reinterpret_cast<Rml::CompiledGeometryHandle>(Mesh.Get());
}

void FUERmlRenderInterface::RenderGeometry(
	Rml::CompiledGeometryHandle geometry,
	Rml::Vector2f translation,
	Rml::TextureHandle texture)
{
	check(CurrentDrawer.IsValid());

	// local space -> Rml space (apply translation)
	FMatrix44f Matrix = FMatrix44f::Identity;
	Matrix.M[3][0] = translation.x;
	Matrix.M[3][1] = translation.y;

	// apply optional CSS transform
	if (bCustomMatrix) { Matrix *= AdditionRenderMatrix; }

	// Rml space -> NDC
	Matrix *= RmlRenderMatrix;

	// ClipRect is in RmlUI context coordinates (logical pixels).
	// AccumRT maps logical -> screen render space.
	FIntRect ResultClipRect;
	if (bUseClipRect)
	{
		// transform rect to slate render space 
		auto ClipRectAfterTrans = TransformRect(RmlWidgetRenderTransform, ClipRect);

		// get intersection rect 
		ClipRectAfterTrans = ClipRectAfterTrans.IntersectionWith(ViewportRect);

		// set up rect 
		ResultClipRect.Min.X = FMath::RoundToInt(ClipRectAfterTrans.Left);
		ResultClipRect.Min.Y = FMath::RoundToInt(ClipRectAfterTrans.Top);
		ResultClipRect.Max.X = FMath::RoundToInt(ClipRectAfterTrans.Right);
		ResultClipRect.Max.Y = FMath::RoundToInt(ClipRectAfterTrans.Bottom);
	}
	else
	{
		ResultClipRect.Min.X = FMath::RoundToInt(ViewportRect.Left);
		ResultClipRect.Min.Y = FMath::RoundToInt(ViewportRect.Top);
		ResultClipRect.Max.X = FMath::RoundToInt(ViewportRect.Right);
		ResultClipRect.Max.Y = FMath::RoundToInt(ViewportRect.Bottom);
	}

	FRmlTextureEntry* TextureEntry = texture ? reinterpret_cast<FRmlTextureEntry*>(texture) : nullptr;
	CurrentDrawer->EmplaceMesh(
		reinterpret_cast<FRmlMesh*>(geometry)->AsShared(),
		TextureEntry,
		Matrix,
		ResultClipRect);
}

void FUERmlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	// remove geometry 
	Meshes.RemoveSwap(reinterpret_cast<FRmlMesh*>(geometry)->AsShared());
}

Rml::TextureHandle FUERmlRenderInterface::LoadTexture(
	Rml::Vector2i& texture_dimensions,
	const Rml::String& source)
{
	FString Path(source.c_str());
	auto FoundTexture = AllTextures.Find(Path);
	if (FoundTexture)
	{
		texture_dimensions.x = (*FoundTexture)->BoundTexture->GetSurfaceWidth();
		texture_dimensions.y = (*FoundTexture)->BoundTexture->GetSurfaceHeight();
		return reinterpret_cast<Rml::TextureHandle>((*FoundTexture).Get());
	}

	// load from file or asset
	UTexture2D* LoadedTexture = nullptr;
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path))
	{
		LoadedTexture = FRmlHelper::LoadTextureFromFile(Path);
	}
	else
	{
		LoadedTexture = FRmlHelper::LoadTextureFromAsset(Path);
	}

	if (!LoadedTexture) return 0;

	auto& AddedTexture = AllTextures.Add(Path, MakeShared<FRmlTextureEntry, ESPMode::ThreadSafe>(LoadedTexture, Path));
	AddedTexture->bPremultiplied = false;
	texture_dimensions.x = LoadedTexture->GetSurfaceWidth();
	texture_dimensions.y = LoadedTexture->GetSurfaceHeight();
	return reinterpret_cast<Rml::TextureHandle>(AddedTexture.Get());
}

Rml::TextureHandle FUERmlRenderInterface::GenerateTexture(
	Rml::Span<const Rml::byte> source,
	Rml::Vector2i source_dimensions)
{
	// load texture 
	UTexture2D* Texture = FRmlHelper::LoadTextureFromRaw(
		(const uint8*)source.data(),
		FIntPoint(source_dimensions.x, source_dimensions.y));

	auto& Entry = AllCreatedTextures.Add_GetRef(MakeShared<FRmlTextureEntry, ESPMode::ThreadSafe>(Texture));
	Entry->bPremultiplied = true;
	return reinterpret_cast<Rml::TextureHandle>(Entry.Get());
}

void FUERmlRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	// to entry 
	FRmlTextureEntry* Entry = reinterpret_cast<FRmlTextureEntry*>(texture);

	// release 
	if (Entry->TexturePath.IsEmpty())
	{
		AllCreatedTextures.RemoveSwap(Entry->AsShared());
	}
	else
	{
		AllTextures.Remove(Entry->TexturePath);
	}
}

void FUERmlRenderInterface::EnableScissorRegion(bool enable)
{
	bUseClipRect = enable;
}

void FUERmlRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	ClipRect.Left   = (float)FMath::Max(region.Left(),   0);
	ClipRect.Top    = (float)FMath::Max(region.Top(),    0);
	ClipRect.Right  = (float)region.Right();
	ClipRect.Bottom = (float)region.Bottom();
}

void FUERmlRenderInterface::SetTransform(const Rml::Matrix4f* transform)
{
	if (transform)
	{
		// Rml::Matrix4f is column-major; FMatrix44f is row-major.
		// Raw memcpy effectively transposes (columns become rows).
		// This is correct because HLSL mul(row_vector, row_major_matrix)
		// produces the same result as mul(column_major_matrix, column_vector).
		FMemory::Memcpy(&AdditionRenderMatrix, transform->data(), sizeof(float) * 16);
		bCustomMatrix = true;
	}
	else
	{
		AdditionRenderMatrix = FMatrix44f::Identity;
		bCustomMatrix = false;
	}
}


TSharedPtr<FRmlDrawer, ESPMode::ThreadSafe> FUERmlRenderInterface::_AllocDrawer()
{
	const bool bMSAA = URmlUiSettings::Get()->bEnableMSAA;

	// search free drawer 
	for (auto& Drawer : AllDrawers)
	{
		if (Drawer->IsFree())
		{
			Drawer->MarkUsing();
			Drawer->SetMSAA(bMSAA);
			return Drawer;
		}
	}

	// create new drawer 
	AllDrawers.Add(MakeShared<FRmlDrawer, ESPMode::ThreadSafe>(true));
	AllDrawers.Top()->SetMSAA(bMSAA);
	return AllDrawers.Top();
}
