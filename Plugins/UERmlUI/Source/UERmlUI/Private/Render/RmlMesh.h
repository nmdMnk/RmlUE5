#pragma once

namespace Rml
{
	struct Vertex;
	template<typename T> class Span;
}

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

	void Setup(Rml::Span<const Rml::Vertex> InVertices, Rml::Span<const int> InIndices);
	void BuildMesh();
	void ReleaseMesh();
	void DrawMesh(FRHICommandList& RHICmdList);

	static FVertexDeclarationRHIRef GetMeshDeclaration();
public:
	TResourceArray<FVertexData>		Vertices;
	FBufferRHIRef					VertexBufferRHI;

	TResourceArray<uint16>			Indices;
	FBufferRHIRef					IndexBufferRHI;

	int32							NumVertices;
	int32							NumTriangles;
};
