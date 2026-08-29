#pragma once

#include "../Primitives/Matrix4x4.h"
#include "Material.h"
#include "Geometry.h"
#include "Texture.h"

namespace nCine
{

	/**
		@brief Holds all the state needed to issue a single draw call
		
		Bundles the material, geometry, model transformation and sort keys for one renderable. The render
		queue sorts commands by their material sort key to minimize state changes, optionally merges them
		via @ref RenderBatcher, and finally calls @ref Issue() to bind the state and draw.
	*/
	class RenderCommand
	{
	public:
		/**
		 * @brief Command type
		 *
		 * Its sole purpose is to allow separated profiling counters in the `RenderStatistics` class.
		 */
		enum class Type
		{
			Unspecified = 0,	/**< Unspecified or non-profiled command */
			Sprite,				/**< Sprite draw command */
			MeshSprite,			/**< Mesh sprite draw command */
			TileMap,			/**< Tile map draw command */
			Particle,			/**< Particle draw command */
			Lighting,			/**< Lighting draw command */
			Text,				/**< Text draw command */
			ImGui,				/**< ImGui draw command */

			Count				/**< Number of command types */
		};

		explicit RenderCommand(Type type);
		RenderCommand();

		/** @brief Returns the number of instances collected in the command or zero if instancing is not used */
		inline std::int32_t GetInstanceCount() const {
			return _numInstances;
		}
		/** @brief Sets the number of instances collected in the command */
		inline void SetInstanceCount(std::int32_t numInstances) {
			_numInstances = numInstances;
		}

		/** @brief Returns the number of elements collected by the command or zero if it's not a batch */
		inline std::int32_t GetBatchSize() const {
			return _batchSize;
		}
		/** @brief Sets the number of batch elements collected by the command */
		inline void SetBatchSize(std::int32_t batchSize) {
			_batchSize = batchSize;
		}

		/** @brief Returns the drawing layer for this command */
		inline std::uint16_t GetLayer() const {
			return _layer;
		}
		/** @brief Sets the drawing layer for this command */
		inline void SetLayer(std::uint16_t layer) {
			_layer = layer;
		}
		/** @brief Returns the visit order index for this command */
		inline std::uint16_t GetVisitOrder() const {
			return _visitOrder;
		}
		/** @brief Sets the visit order index for this command */
		inline void SetVisitOrder(std::uint16_t visitOrder) {
			_visitOrder = visitOrder;
		}

		/** @brief Returns the material sort key for the queue */
		inline std::uint64_t GetMaterialSortKey() const {
			return _materialSortKey;
		}
		/** @brief Returns the lower part of the material sort key, used for batch splitting logic */
		inline std::uint32_t GetLowerMaterialSortKey() const {
			return std::uint32_t(_materialSortKey);
		}
		/** @brief Calculates the material sort key for the queue */
		void CalculateMaterialSortKey();
		/** @brief Returns the id based secondary sort key for the queue */
		inline std::uint32_t GetIdSortKey() const {
			return _idSortKey;
		}
		/** @brief Sets the id based secondary sort key for the queue */
		inline void SetIdSortKey(std::uint32_t idSortKey) {
			_idSortKey = idSortKey;
		}
		/** @brief Sets an optional backend telemetry label for this draw */
		inline void SetTelemetryLabel(const char* label) {
			_telemetryLabel = label;
		}

		/** @brief Binds the command state and issues the draw call */
		void Issue();

		/** @brief Returns the command type (for profiling purposes) */
		inline Type GetType() const {
#if defined(NCINE_PROFILING)
			return _type;
#else
			return Type::Unspecified;
#endif
		}
		/** @brief Sets the command type (for profiling purposes) */
		inline void SetType(Type type) {
#if defined(NCINE_PROFILING)
			_type = type;
#endif
		}

		/** @brief Sets the scissor rectangle for this command */
		inline void SetScissor(Recti scissorRect) {
			_scissorRect = scissorRect;
		}
		/** @brief Sets the scissor rectangle for this command */
		void SetScissor(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);

		/** @brief Returns the model transformation matrix */
		inline const Matrix4x4f& GetTransformation() const {
			return _modelMatrix;
		}
		/** @brief Sets the model transformation matrix and marks it for re-committing */
		void SetTransformation(const Matrix4x4f& modelMatrix);

		/** @brief Returns the material (read-only) */
		inline const Material& GetMaterial() const {
			return _material;
		}
		/** @brief Returns the geometry (read-only) */
		inline const Geometry& GetGeometry() const {
			return _geometry;
		}
		/** @brief Returns the material */
		inline Material& GetMaterial() {
			return _material;
		}
		/** @brief Returns the geometry */
		inline Geometry& GetGeometry() {
			return _geometry;
		}

		/** @brief Returns the material's "InstanceBlock" uniform block cache, resolved once per shader change */
		RHI::UniformBlockCache* GetInstanceBlock();

		/** @brief Commits the model matrix uniform block */
		void CommitNodeTransformation();

		/** @brief Commits the projection and view matrix uniforms */
		void CommitCameraTransformation();

		/** @brief Calls all the commit methods except the camera uniforms commit */
		void CommitAll();

		/** @brief Calculates the Z-depth of a command layer using the specified near and far planes */
		static float CalculateDepth(std::uint16_t layer, float nearClip, float farClip);

	private:
		/** @brief Distance on the Z axis between adjacent layers */
		static constexpr float LayerStep = 1.0f / static_cast<float>(0xFFFF);

		/** @brief Material sort key that minimizes state changes when rendering commands */
		std::uint64_t _materialSortKey;
		// Cached model matrix uniform and "InstanceBlock" uniform block cache, avoiding name-based
		// lookups on every use. Valid as long as the cached shader change counter matches the material's one.
		RHI::UniformCache* _modelMatrixUniform;
		RHI::UniformBlockCache* _instanceBlock;
		/** @brief Id based secondary sort key that stabilizes render command sorting */
		std::uint32_t _idSortKey;
		const char* _telemetryLabel;
		// Value of the material's shader change counter when the cached uniforms were resolved
		std::uint32_t _cachedShaderChangeCounter;
		/** @brief Drawing layer for this command */
		std::uint16_t _layer;
		/** @brief Visit order index for this command */
		std::uint16_t _visitOrder;
		std::int32_t _numInstances;
		std::int32_t _batchSize;

		bool _transformationCommitted;
		// Whether the cached model matrix uniform belongs to a uniform block (as opposed to a loose uniform)
		bool _modelMatrixUniformInBlock;

#if defined(NCINE_PROFILING)
		Type _type;
#endif

		Recti _scissorRect;
		Matrix4x4f _modelMatrix;
		Material _material;
		Geometry _geometry;

		/** @brief Returns the final layer sort key for this command */
		inline std::uint32_t GetLayerSortKey() const {
			return std::uint32_t(_layer << 16) + _visitOrder;
		}

		// Re-resolves the cached uniform pointers if the material's shader program has changed
		void RefreshCachedUniforms();
	};
}
