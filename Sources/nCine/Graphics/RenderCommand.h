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
		inline std::int32_t numInstances() const {
			return numInstances_;
		}
		/// Sets the number of instances collected in the command
		inline void setNumInstances(std::int32_t numInstances) {
			numInstances_ = numInstances;
		}

		/// Returns the number of elements collected by the command or zero if it's not a batch
		inline std::int32_t batchSize() const {
			return batchSize_;
		}
		/// Sets the number of batch elements collected by the command
		inline void setBatchSize(std::int32_t batchSize) {
			batchSize_ = batchSize;
		}

		/// Returns the drawing layer for this command
		inline std::uint16_t layer() const {
			return layer_;
		}
		/// Sets the drawing layer for this command
		inline void setLayer(std::uint16_t layer) {
			layer_ = layer;
		}
		/// Returns the visit order index for this command
		inline std::uint16_t visitOrder() const {
			return visitOrder_;
		}
		/// Sets the visit order index for this command
		inline void setVisitOrder(std::uint16_t visitOrder) {
			visitOrder_ = visitOrder;
		}

		/// Returns the material sort key for the queue
		inline std::uint64_t materialSortKey() const {
			return materialSortKey_;
		}
		/// Returns the lower part of the material sort key, used for batch splitting logic
		inline std::uint32_t lowerMaterialSortKey() const {
			return std::uint32_t(materialSortKey_);
		}
		/// Calculates a material sort key for the queue
		void calculateMaterialSortKey();
		/// Returns the id based secondary sort key for the queue
		inline std::uint32_t idSortKey() const {
			return idSortKey_;
		}
		/// Sets the id based secondary sort key for the queue
		inline void setIdSortKey(std::uint32_t idSortKey) {
			idSortKey_ = idSortKey;
		}

		/// Issues the render command
		void issue();

		/// Gets the command type (for profiling purposes)
		inline Type type() const {
#if defined(NCINE_PROFILING)
			return type_;
#else
			return Type::Unspecified;
#endif
		}
		/// Sets the command type (for profiling purposes)
		inline void setType(Type type) {
#if defined(NCINE_PROFILING)
			type_ = type;
#endif
		}

		inline void setScissor(Recti scissorRect) {
			scissorRect_ = scissorRect;
		}
		void setScissor(GLint x, GLint y, GLsizei width, GLsizei height);

		inline const Matrix4x4f& transformation() const {
			return modelMatrix_;
		}
		void setTransformation(const Matrix4x4f& modelMatrix);
		inline const Material& material() const {
			return material_;
		}
		inline const Geometry& geometry() const {
			return geometry_;
		}
		inline Material& material() {
			return material_;
		}
		inline Geometry& geometry() {
			return geometry_;
		}

		/// Commits the model matrix uniform block
		void commitNodeTransformation();

		/// Commits the projection and view matrix uniforms
		void commitCameraTransformation();

		/// Calls all the commit methods except the camera uniforms commit
		void commitAll();

		/// Calculates the Z-depth of command layer using the specified near and far planes
		static float calculateDepth(std::uint16_t layer, float nearClip, float farClip);

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
		inline std::uint32_t layerSortKey() const {
			return std::uint32_t(layer_ << 16) + visitOrder_;
		}
	};
}
