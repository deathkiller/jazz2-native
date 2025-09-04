#pragma once

#include "../Primitives/Matrix4x4.h"
#include "Material.h"
#include "Geometry.h"
#include "Texture.h"

namespace nCine
{
	/// Wraps all the information needed for issuing a draw command
	class RenderCommand
	{
	public:
		/// Command types
		/*! Its sole purpose is to allow separated profiling counters in the `RenderStatistics` class. */
		enum class Type
		{
			Unspecified = 0,
			Sprite,
			MeshSprite,
			TileMap,
			Particle,
			Lighting,
			Text,
			ImGui,

			Count
		};

		explicit RenderCommand(Type type);
		RenderCommand();

		/// Returns the number of instances collected in the command or zero if instancing is not used
		inline std::int32_t GetInstanceCount() const {
			return numInstances_;
		}
		/// Sets the number of instances collected in the command
		inline void SetInstanceCount(std::int32_t numInstances) {
			numInstances_ = numInstances;
		}

		/// Returns the number of elements collected by the command or zero if it's not a batch
		inline std::int32_t GetBatchSize() const {
			return batchSize_;
		}
		/// Sets the number of batch elements collected by the command
		inline void SetBatchSize(std::int32_t batchSize) {
			batchSize_ = batchSize;
		}

		/// Returns the drawing layer for this command
		inline std::uint16_t GetLayer() const {
			return layer_;
		}
		/// Sets the drawing layer for this command
		inline void SetLayer(std::uint16_t layer) {
			layer_ = layer;
		}
		/// Returns the visit order index for this command
		inline std::uint16_t GetVisitOrder() const {
			return visitOrder_;
		}
		/// Sets the visit order index for this command
		inline void SetVisitOrder(std::uint16_t visitOrder) {
			visitOrder_ = visitOrder;
		}

		/// Returns the material sort key for the queue
		inline std::uint64_t GetMaterialSortKey() const {
			return materialSortKey_;
		}
		/// Returns the lower part of the material sort key, used for batch splitting logic
		inline std::uint32_t GetLowerMaterialSortKey() const {
			return std::uint32_t(materialSortKey_);
		}
		/// Calculates a material sort key for the queue
		void CalculateMaterialSortKey();
		/// Returns the id based secondary sort key for the queue
		inline std::uint32_t GetIdSortKey() const {
			return idSortKey_;
		}
		/// Sets the id based secondary sort key for the queue
		inline void SetIdSortKey(std::uint32_t idSortKey) {
			idSortKey_ = idSortKey;
		}

		/// Issues the render command
		void Issue();

		/// Gets the command type (for profiling purposes)
		inline Type GetType() const {
#if defined(NCINE_PROFILING)
			return type_;
#else
			return Type::Unspecified;
#endif
		}
		/// Sets the command type (for profiling purposes)
		inline void SetType(Type type) {
#if defined(NCINE_PROFILING)
			type_ = type;
#endif
		}

		inline void SetScissor(Recti scissorRect) {
			scissorRect_ = scissorRect;
		}
		void SetScissor(GLint x, GLint y, GLsizei width, GLsizei height);

		inline const Matrix4x4f& GetTransformation() const {
			return modelMatrix_;
		}
		void SetTransformation(const Matrix4x4f& modelMatrix);

		inline const Material& GetMaterial() const {
			return material_;
		}
		inline const Geometry& GetGeometry() const {
			return geometry_;
		}
		inline Material& GetMaterial() {
			return material_;
		}
		inline Geometry& GetGeometry() {
			return geometry_;
		}

		/// Commits the model matrix uniform block
		void CommitNodeTransformation();

		/// Commits the projection and view matrix uniforms
		void CommitCameraTransformation();

		/// Calls all the commit methods except the camera uniforms commit
		void CommitAll();

		/// Calculates the Z-depth of command layer using the specified near and far planes
		static float CalculateDepth(std::uint16_t layer, float nearClip, float farClip);

	private:
		/// The distance on the Z axis between adjacent layers
		static constexpr float LayerStep = 1.0f / static_cast<float>(0xFFFF);

		/// The material sort key minimizes state changes when rendering commands
		std::uint64_t materialSortKey_;
		/// The id based secondary sort key stabilizes render commands sorting
		std::uint32_t idSortKey_;
		/// The drawing layer for this command
		std::uint16_t layer_;
		/// The visit order index for this command
		std::uint16_t visitOrder_;
		std::int32_t numInstances_;
		std::int32_t batchSize_;

		bool transformationCommitted_;

#if defined(NCINE_PROFILING)
		Type type_;
#endif

		Recti scissorRect_;
		Matrix4x4f modelMatrix_;
		Material material_;
		Geometry geometry_;

		/// Returns the final layer sort key for this command
		inline std::uint32_t GetLayerSortKey() const {
			return std::uint32_t(layer_ << 16) + visitOrder_;
		}
	};
}
