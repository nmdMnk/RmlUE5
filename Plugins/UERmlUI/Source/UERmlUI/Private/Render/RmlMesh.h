#pragma once

namespace Rml
{
	struct Vertex;
	template<typename T> class Span;
}

// CPU-only mesh container. RHI buffers are owned by FRmlDrawer's per-frame
// shared VB/IB; this struct just holds the vertex/index data for the pre-pass.
class FRmlMesh : public TSharedFromThis<FRmlMesh, ESPMode::ThreadSafe>
{
public:
	struct FVertexData
	{
		FVector2f	Position;
		FColor		Color;
		FVector2f	UV;
		FVertexData(const FVector2f& InPos, const FVector2f& InUV, const FColor& InColor)
			: Position(InPos), Color(InColor), UV(InUV)
		{}
	};

	// Copy RmlUI vertex/index data into CPU arrays and record counts.
	void Setup(Rml::Span<const Rml::Vertex> InVertices, Rml::Span<const int> InIndices);

	// Vertex declaration for PSO setup (shared, created once).
	static FVertexDeclarationRHIRef GetMeshDeclaration();

	TResourceArray<FVertexData>		Vertices;
	TResourceArray<uint16>			Indices;
	int32							NumVertices = 0;
	int32							NumTriangles = 0;
};
