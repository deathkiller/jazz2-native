#pragma once

#include "BaseSprite.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief A scene node representing a mesh with vertices and UVs
		
		Drawable sprite whose geometry is an arbitrary set of indexed vertices instead of a single quad. The
		vertices carry positions interleaved with texture coordinates (or positions only when no texture is
		bound) and may be owned by the sprite or referenced from an external array. Used for tile layers and
		other custom meshes rendered through the `MeshSprite` shader programs.
	*/
	class MeshSprite : public BaseSprite
	{
	public:
		/**
		 * @brief Vertex data for the mesh
		 */
		struct Vertex
		{
			float x, y;
			float u, v;

			Vertex()
				: x(0.0f), y(0.0f), u(0.0f), v(0.0f) {}
			Vertex(float xx, float yy, float uu, float vv)
				: x(xx), y(yy), u(uu), v(vv) {}
		};
		static const std::uint32_t VertexBytes = sizeof(Vertex);
		static const std::uint32_t VertexFloats = VertexBytes / sizeof(float);

		/**
		 * @brief Vertex data for the mesh when no texture is specified
		 */
		struct VertexNoTexture
		{
			float x, y;

			VertexNoTexture()
				: x(0.0f), y(0.0f) {}
			VertexNoTexture(float xx, float yy)
				: x(xx), y(yy) {}
		};
		static const std::uint32_t VertexNoTextureBytes = sizeof(VertexNoTexture);
		static const std::uint32_t VertexNoTextureFloats = VertexNoTextureBytes / sizeof(float);

		/**
		 * @brief How vertices generated from texels map to the texture rectangle
		 */
		enum class TextureCutMode
		{
			RESIZE,									/**< Sizes the sprite from the texture rectangle */
			CROP									/**< Sizes the sprite from the bounding box of the supplied points */
		};

		/** @brief Default constructor for a sprite with no parent and no texture, positioned in the origin */
		MeshSprite();
		/** @brief Constructor for a sprite with a parent and texture, positioned in the relative origin */
		MeshSprite(SceneNode* parent, Texture* texture);
		/** @brief Constructor for a sprite with a texture but no parent, positioned in the origin */
		explicit MeshSprite(Texture* texture);
		/** @brief Constructor for a sprite with a parent, a texture and a specified relative position */
		MeshSprite(SceneNode* parent, Texture* texture, float xx, float yy);
		/** @brief Constructor for a sprite with a parent, a texture and a specified relative position as a vector */
		MeshSprite(SceneNode* parent, Texture* texture, Vector2f position);
		/** @brief Constructor for a sprite with a texture and a specified position but no parent */
		MeshSprite(Texture* texture, float xx, float yy);
		/** @brief Constructor for a sprite with a texture and a specified position as a vector but no parent */
		MeshSprite(Texture* texture, Vector2f position);

		MeshSprite& operator=(const MeshSprite&) = delete;
		MeshSprite(MeshSprite&&) = default;
		MeshSprite& operator=(MeshSprite&&) = default;

		/** @brief Returns a copy of this object */
		inline MeshSprite clone() const {
			return MeshSprite(*this);
		}

		/** @brief Returns the number of bytes used by each vertex */
		inline std::uint32_t bytesPerVertex() const {
			return bytesPerVertex_;
		}
		/** @brief Returns the number of vertices of the sprite mesh */
		inline std::uint32_t numVertices() const {
			return numVertices_;
		}
		/** @brief Returns the total number of bytes used by all sprite's vertices */
		inline std::uint32_t numBytes() const {
			return numVertices_ * bytesPerVertex_;
		}
		/** @brief Returns the vertices data of the sprite mesh */
		inline const float* vertices() const {
			return vertexDataPointer_;
		}
		/** @brief Returns `true` if the vertices belong to the sprite and are not stored externally */
		inline bool uniqueVertices() const {
			return vertexDataPointer_ == vertices_.data();
		}

		/**
		 * @brief Copies the vertices data with a custom format from a pointer into the sprite
		 *
		 * @note If used directly, it requires a custom shader that understands the specified data format.
		 */
		void copyVertices(std::uint32_t numVertices, std::uint32_t bytesPerVertex, const void* vertexData);
		/** @brief Copies the vertices data from a pointer into the sprite */
		void copyVertices(std::uint32_t numVertices, const Vertex* vertices);
		/** @brief Copies the vertices data from a pointer into the sprite (no texture version) */
		void copyVertices(std::uint32_t numVertices, const VertexNoTexture* vertices);
		/** @brief Copies the vertices data from another sprite and sets the same size */
		void copyVertices(const MeshSprite& meshSprite);

		/**
		 * @brief Sets the vertices data to point to an external array with a custom format
		 *
		 * @note If used directly, it requires a custom shader that understands the specified data format.
		 */
		void setVertices(std::uint32_t numVertices, std::uint32_t bytesPerVertex, const void* vertexData);
		/** @brief Sets the vertices data to point to an external array */
		void setVertices(std::uint32_t numVertices, const Vertex* vertices);
		/** @brief Sets the vertices data to point to an external array (no texture version) */
		void setVertices(std::uint32_t numVertices, const VertexNoTexture* vertices);
		/** @brief Sets the vertices data to the data used by another sprite and sets the same size */
		void setVertices(const MeshSprite& meshSprite);

		/** @brief Returns the internal vertices data, cleared and set to the required size (custom format version) */
		float* emplaceVertices(std::uint32_t numElements, std::uint32_t bytesPerVertex);
		/** @brief Returns the internal vertices data, cleared and set to the required size */
		float* emplaceVertices(std::uint32_t numElements);

		/** @brief Creates an internal set of vertices from an external array of points in texture space, with the specified texture cut mode */
		void createVerticesFromTexels(std::uint32_t numVertices, const Vector2f* points, TextureCutMode cutMode);
		/** @brief Creates an internal set of vertices from an external array of points in texture space */
		void createVerticesFromTexels(std::uint32_t numVertices, const Vector2f* points);

		/** @brief Returns the number of indices used to draw the sprite mesh */
		inline std::uint32_t numIndices() const {
			return numIndices_;
		}
		/** @brief Returns the indices used to draw the sprite mesh */
		inline const std::uint16_t* indices() const {
			return indexDataPointer_;
		}
		/** @brief Returns `true` if the indices belong to the sprite and are not stored externally */
		inline bool uniqueIndices() const {
			return indexDataPointer_ == indices_.data();
		}
		/** @brief Copies the indices from a pointer into the sprite */
		void copyIndices(std::uint32_t numIndices, const std::uint16_t* indices);
		/** @brief Copies the indices from another sprite */
		void copyIndices(const MeshSprite& meshSprite);
		/** @brief Sets the indices data to point to an external array */
		void setIndices(std::uint32_t numIndices, const std::uint16_t* indices);
		/** @brief Sets the indices data to the data used by another sprite */
		void setIndices(const MeshSprite& meshSprite);

		/** @brief Returns the internal indices data, cleared and set to the required size */
		std::uint16_t* emplaceIndices(std::uint32_t numIndices);

		inline static ObjectType sType() {
			return ObjectType::MeshSprite;
		}

	protected:
		/** @brief Protected copy constructor used to clone objects */
		MeshSprite(const MeshSprite& other);

	private:
		/** @brief The array of vertex positions, interleaved with texture coordinates when a texture is attached */
		SmallVector<float, 0> vertices_;
		/** @brief Pointer to vertex data, either from a shared array or unique to this sprite */
		const float* vertexDataPointer_;
		/** @brief The number of bytes used by each vertex */
		std::uint32_t bytesPerVertex_;
		/** @brief The number of vertices, either shared or not, that composes the mesh */
		std::uint32_t numVertices_;

		/** @brief The array of indices used to draw the sprite mesh */
		SmallVector<std::uint16_t, 0> indices_;
		/** @brief Pointer to index data, either from a shared array or unique to this sprite */
		const std::uint16_t* indexDataPointer_;
		/** @brief The number of indices, either shared or not, that composes the mesh */
		std::uint32_t numIndices_;

		/** @brief Initializer method for constructors and the copy constructor */
		void init();

		void shaderHasChanged() override;
		void textureHasChanged(Texture* newTexture) override;
	};

}
