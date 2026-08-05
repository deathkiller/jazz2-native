#include "MeshSprite.h"
#include "RenderCommand.h"
#include "RenderResources.h"

#include <cstring> // for memcpy()

namespace nCine
{
	MeshSprite::MeshSprite()
		: MeshSprite(nullptr, nullptr, 0.0f, 0.0f)
	{
	}

	MeshSprite::MeshSprite(SceneNode* parent, Texture* texture)
		: MeshSprite(parent, texture, 0.0f, 0.0f)
	{
	}

	MeshSprite::MeshSprite(Texture* texture)
		: MeshSprite(nullptr, texture, 0.0f, 0.0f)
	{
	}

	MeshSprite::MeshSprite(SceneNode* parent, Texture* texture, float xx, float yy)
		: BaseSprite(parent, texture, xx, yy), _vertices(16), _vertexDataPointer(nullptr), _bytesPerVertex(0),
			_numVertices(0), _indices(16), _indexDataPointer(nullptr), _numIndices(0)
	{
		init();
	}

	MeshSprite::MeshSprite(SceneNode* parent, Texture* texture, Vector2f position)
		: MeshSprite(parent, texture, position.X, position.Y)
	{
	}

	MeshSprite::MeshSprite(Texture* texture, float xx, float yy)
		: MeshSprite(nullptr, texture, xx, yy)
	{
	}

	MeshSprite::MeshSprite(Texture* texture, Vector2f position)
		: MeshSprite(nullptr, texture, position.X, position.Y)
	{
	}

	/**
	 * @note If used directly, it requires a custom shader that understands the specified data format.
	 */
	void MeshSprite::copyVertices(std::uint32_t numVertices, std::uint32_t bytesPerVertex, const void* vertexData)
	{
		std::uint32_t floatsPerVertex = (bytesPerVertex / sizeof(float));
		_vertices.resize(numVertices * floatsPerVertex);
		memcpy(_vertices.data(), vertexData, numVertices * bytesPerVertex);
		_bytesPerVertex = bytesPerVertex;

		_vertexDataPointer = _vertices.data();
		_numVertices = numVertices;
		_renderCommand.GetGeometry().SetVertexCount(numVertices);
		_renderCommand.GetGeometry().SetElementsPerVertex(floatsPerVertex);
		_renderCommand.GetGeometry().SetHostVertexPointer(_vertexDataPointer);
	}

	void MeshSprite::copyVertices(std::uint32_t numVertices, const Vertex* vertices)
	{
		DEATH_ASSERT(_texture != nullptr);
		copyVertices(numVertices, sizeof(Vertex), reinterpret_cast<const float*>(vertices));
	}

	void MeshSprite::copyVertices(std::uint32_t numVertices, const VertexNoTexture* vertices)
	{
		DEATH_ASSERT(_texture == nullptr);
		copyVertices(numVertices, sizeof(VertexNoTexture), reinterpret_cast<const float*>(vertices));
	}

	void MeshSprite::copyVertices(const MeshSprite& meshSprite)
	{
		copyVertices(meshSprite._numVertices, meshSprite._bytesPerVertex, meshSprite._vertexDataPointer);
		_width = meshSprite._width;
		_height = meshSprite._height;
		_texRect = meshSprite._texRect;

		_dirtyBits.set(DirtyBitPositions::SizeBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
		_dirtyBits.set(DirtyBitPositions::TextureBit);
	}

	/**
	 * @note If used directly, it requires a custom shader that understands the specified data format.
	 */
	void MeshSprite::setVertices(std::uint32_t numVertices, std::uint32_t bytesPerVertex, const void* vertexData)
	{
		std::uint32_t floatsPerVertex = (bytesPerVertex / sizeof(float));
		_vertices.clear();
		_bytesPerVertex = bytesPerVertex;

		_vertexDataPointer = reinterpret_cast<const float*>(vertexData);
		_numVertices = numVertices;
		_renderCommand.GetGeometry().SetVertexCount(numVertices);
		_renderCommand.GetGeometry().SetElementsPerVertex(floatsPerVertex);
		_renderCommand.GetGeometry().SetHostVertexPointer(_vertexDataPointer);
	}

	void MeshSprite::setVertices(std::uint32_t numVertices, const Vertex* vertices)
	{
		DEATH_ASSERT(_texture != nullptr);
		copyVertices(numVertices, sizeof(Vertex), reinterpret_cast<const void*>(vertices));
	}

	void MeshSprite::setVertices(std::uint32_t numVertices, const VertexNoTexture* vertices)
	{
		DEATH_ASSERT(_texture == nullptr);
		copyVertices(numVertices, sizeof(VertexNoTexture), reinterpret_cast<const void*>(vertices));
	}

	void MeshSprite::setVertices(const MeshSprite& meshSprite)
	{
		setVertices(meshSprite._numVertices, meshSprite._bytesPerVertex, meshSprite._vertexDataPointer);
		_width = meshSprite._width;
		_height = meshSprite._height;

		_dirtyBits.set(DirtyBitPositions::SizeBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	float* MeshSprite::emplaceVertices(std::uint32_t numElements, std::uint32_t bytesPerVertex)
	{
		if (numElements == 0 || bytesPerVertex == 0) {
			return nullptr;
		}

		const std::uint32_t floatsPerVertex = bytesPerVertex / sizeof(float);
		const std::uint32_t numVertices = (numElements / floatsPerVertex);
		_vertices.clear();
		_vertices.resize(numElements);
		_bytesPerVertex = bytesPerVertex;

		_vertexDataPointer = _vertices.data();
		_numVertices = numVertices;
		_renderCommand.GetGeometry().SetVertexCount(numVertices);
		_renderCommand.GetGeometry().SetElementsPerVertex(floatsPerVertex);
		_renderCommand.GetGeometry().SetHostVertexPointer(_vertexDataPointer);

		return _vertices.data();
	}

	float* MeshSprite::emplaceVertices(std::uint32_t numElements)
	{
		const std::uint32_t bytesPerVertex = (_texture != nullptr ? sizeof(Vertex) : sizeof(VertexNoTexture));
		return emplaceVertices(numElements, bytesPerVertex);
	}

	void MeshSprite::createVerticesFromTexels(std::uint32_t numVertices, const Vector2f* points, TextureCutMode cutMode)
	{
		FATAL_ASSERT(numVertices >= 3);

		const std::uint32_t numFloats = (_texture != nullptr ? VertexFloats : VertexNoTextureFloats);
		_vertices.resize(numVertices * numFloats);
		_bytesPerVertex = (_texture != nullptr ? sizeof(Vertex) : sizeof(VertexNoTexture));
		Vector2f min(0.0f, 0.0f);

		if (cutMode == TextureCutMode::Crop) {
			min = points[0];
			Vector2f max(min);
			for (std::uint32_t i = 1; i < numVertices; i++) {
				if (points[i].X > max.X) {
					max.X = points[i].X;
				} else if (points[i].X < min.X) {
					min.X = points[i].X;
				}
				if (points[i].Y > max.Y) {
					max.Y = points[i].Y;
				} else if (points[i].Y < min.Y) {
					min.Y = points[i].Y;
				}
			}

			_width = max.X - min.X;
			_height = max.Y - min.Y;
		} else if (_texRect.W > 0 && _texRect.H > 0) {
			_width = float(_texRect.W);
			_height = float(_texRect.H);
		}

		const float halfWidth = _width * 0.5f;
		const float halfHeight = _height * 0.5f;

		for (std::uint32_t i = 0; i < numVertices; i++) {
			if (_texture != nullptr) {
				Vertex& v = reinterpret_cast<Vertex&>(_vertices[i * VertexFloats]);
				v.x = (points[i].X - min.X - halfWidth) / _width; // from -0.5 to 0.5
				v.y = (points[i].Y - min.Y - halfHeight) / _height; // from -0.5 to 0.5
				v.u = points[i].X / (_texRect.W - _texRect.X);
				v.v = (_texRect.H - points[i].Y) / (_texRect.H - _texRect.Y); // flipped
			} else {
				VertexNoTexture& v = reinterpret_cast<VertexNoTexture&>(_vertices[i * VertexNoTextureFloats]);
				v.x = (points[i].X - min.X - halfWidth) / _width; // from -0.5 to 0.5
				v.y = (points[i].Y - min.Y - halfHeight) / _height; // from -0.5 to 0.5
			}
		}

		_vertexDataPointer = _vertices.data();
		_numVertices = numVertices;
		_renderCommand.GetGeometry().SetVertexCount(numVertices);
		_renderCommand.GetGeometry().SetElementsPerVertex(numFloats);
		_renderCommand.GetGeometry().SetHostVertexPointer(_vertexDataPointer);

		_dirtyBits.set(DirtyBitPositions::SizeBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	void MeshSprite::createVerticesFromTexels(std::uint32_t numVertices, const Vector2f* points)
	{
		createVerticesFromTexels(numVertices, points, TextureCutMode::Resize);
	}

	void MeshSprite::copyIndices(std::uint32_t numIndices, const std::uint16_t* indices)
	{
		_indices.reserve(numIndices);
		std::memcpy(_indices.data(), indices, numIndices * sizeof(std::uint16_t));

		_indexDataPointer = _indices.data();
		_numIndices = numIndices;
		_renderCommand.GetGeometry().SetIndexCount(_numIndices);
		_renderCommand.GetGeometry().SetHostIndexPointer(_indexDataPointer);
	}

	void MeshSprite::copyIndices(const MeshSprite& meshSprite)
	{
		copyIndices(meshSprite._numIndices, meshSprite._indexDataPointer);
	}

	void MeshSprite::setIndices(std::uint32_t numIndices, const std::uint16_t* indices)
	{
		_indices.clear();

		_indexDataPointer = indices;
		_numIndices = numIndices;
		_renderCommand.GetGeometry().SetIndexCount(_numIndices);
		_renderCommand.GetGeometry().SetHostIndexPointer(_indexDataPointer);
	}

	void MeshSprite::setIndices(const MeshSprite& meshSprite)
	{
		setIndices(meshSprite._numIndices, meshSprite._indexDataPointer);
	}

	unsigned short* MeshSprite::emplaceIndices(std::uint32_t numIndices)
	{
		if (numIndices == 0) {
			return nullptr;
		}

		_indices.clear();
		_indices.resize(numIndices);

		_indexDataPointer = _indices.data();
		_numIndices = numIndices;
		_renderCommand.GetGeometry().SetIndexCount(_numIndices);
		_renderCommand.GetGeometry().SetHostIndexPointer(_indexDataPointer);

		return _indices.data();
	}

	MeshSprite::MeshSprite(const MeshSprite& other)
		: BaseSprite(other)
	{
		init();
		setTexRect(other._texRect);
		copyVertices(other._numVertices, other._bytesPerVertex, other._vertices.data());
		copyIndices(other._numIndices, other._indices.data());
	}

	void MeshSprite::init()
	{
		// TODO
		/*if (_texture != nullptr && _texture->name() != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(_texture->name(), nctl::strnlen(_texture->name(), Object::MaxNameLength));
		}*/

		_type = ObjectType::MeshSprite;
		_renderCommand.SetType(RenderCommand::Type::MeshSprite);

		const Material::ShaderProgramType shaderProgramType = (_texture != nullptr ? Material::ShaderProgramType::MeshSprite : Material::ShaderProgramType::MeshSpriteNoTexture);
		_renderCommand.GetMaterial().SetShaderProgramType(shaderProgramType);

		shaderHasChanged();
		_renderCommand.GetGeometry().SetPrimitiveType(PrimitiveType::TriangleStrip);
		_renderCommand.GetGeometry().SetElementsPerVertex(_texture ? VertexFloats : VertexNoTextureFloats);
		_renderCommand.GetGeometry().SetHostVertexPointer(_vertexDataPointer);

		if (_texture != nullptr) {
			setTexRect(Recti(0, 0, _texture->GetWidth(), _texture->GetHeight()));
		}
	}

	void MeshSprite::shaderHasChanged()
	{
		BaseSprite::shaderHasChanged();
		_renderCommand.GetMaterial().SetDefaultAttributesParameters();
	}

	void MeshSprite::textureHasChanged(Texture* newTexture)
	{
		if (_renderCommand.GetMaterial().GetShaderProgramType() != Material::ShaderProgramType::Custom) {
			const Material::ShaderProgramType shaderProgramType = (newTexture != nullptr ? Material::ShaderProgramType::MeshSprite : Material::ShaderProgramType::MeshSpriteNoTexture);
			const bool hasChanged = _renderCommand.GetMaterial().SetShaderProgramType(shaderProgramType);
			if (hasChanged) {
				shaderHasChanged();
			}
			_renderCommand.GetGeometry().SetElementsPerVertex(newTexture ? VertexFloats : VertexNoTextureFloats);
		}

		if (_texture != nullptr && newTexture != nullptr && _texture != newTexture) {
			Recti texRect = _texRect;
			texRect.X = (texRect.X * newTexture->GetWidth() / _texture->GetWidth());
			texRect.Y = (texRect.Y * newTexture->GetHeight() / _texture->GetHeight());
			texRect.W = (texRect.W * newTexture->GetWidth() / _texture->GetWidth());
			texRect.H = (texRect.H * newTexture->GetHeight() / _texture->GetHeight());
			setTexRect(texRect); // it also sets _width and _height
		} else if (_texture == nullptr && newTexture != nullptr) {
			// Assigning a texture when there wasn't any
			setTexRect(Recti(0, 0, newTexture->GetWidth(), newTexture->GetHeight()));
		}
	}
}
