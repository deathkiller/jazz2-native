#pragma once

#include "../Primitives/Matrix4x4.h"
#include "Material.h"
#include "Geometry.h"
#include "Texture.h"

namespace nCine
{
	class Color;

	/// The class wrapping all the information needed for issuing a draw command
	class RenderCommand
	{
	public:
		/// Command types
		/*! Its sole purpose is to allow separated profiling counters in the `RenderStatistics` class. */
		enum class CommandTypes
		{
			Unspecified = 0,
			Sprite,
			MeshSprite,
			TileMap,
			Particle,
			Text,
			ImGui,

			Count
		};

		explicit RenderCommand(CommandTypes profilingType);
		RenderCommand();

		/// Returns the number of instances collected in the command or zero if instancing is not used
		inline int numInstances() const {
			return numInstances_;
		}
		/// Sets the number of instances collected in the command
		inline void setNumInstances(int numInstances) {
			numInstances_ = numInstances;
		}

		/// Returns the number of elements collected by the command or zero if it's not a batch
		inline int batchSize() const {
			return batchSize_;
		}
		/// Sets the number of batch elements collected by the command
		inline void setBatchSize(int batchSize) {
			batchSize_ = batchSize;
		}

		/// Returns the drawing layer for this command
		inline uint16_t layer() const {
			return layer_;
		}
		/// Sets the drawing layer for this command
		inline void setLayer(uint16_t layer) {
			layer_ = layer;
		}
		/// Returns the visit order index for this command
		inline uint16_t visitOrder() const {
			return visitOrder_;
		}
		/// Sets the visit order index for this command
		inline void setVisitOrder(uint16_t visitOrder) {
			visitOrder_ = visitOrder;
		}

		/// Returns the material sort key for the queue
		inline uint64_t materialSortKey() const {
			return materialSortKey_;
		}
		/// Returns the lower part of the material sort key, used for batch splitting logic
		inline uint32_t lowerMaterialSortKey() const {
			return static_cast<uint32_t>(materialSortKey_);
		}
		/// Calculates a material sort key for the queue
		void calculateMaterialSortKey();
		/// Returns the id based secondary sort key for the queue
		inline unsigned int idSortKey() const {
			return idSortKey_;
		}
		/// Sets the id based secondary sort key for the queue
		inline void setIdSortKey(unsigned int idSortKey) {
			idSortKey_ = idSortKey;
		}

		/// Issues the render command
		void issue();

		/// Gets the command type (for profiling purposes)
		inline CommandTypes type() const {
#if defined(NCINE_PROFILING)
			return profilingType_;
#else
			return CommandTypes::Unspecified;
#endif
		}
		/// Sets the command type (for profiling purposes)
		inline void setType(CommandTypes type) {
#if defined(NCINE_PROFILING)
			profilingType_ = type;
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
		static float calculateDepth(uint16_t layer, float nearClip, float farClip);

	private:
		/// The distance on the Z axis between adjacent layers
		static constexpr float LayerStep = 1.0f / static_cast<float>(0xFFFF);

		/// The material sort key minimizes state changes when rendering commands
		uint64_t materialSortKey_;
		/// The id based secondary sort key stabilizes render commands sorting
		uint32_t idSortKey_;
		/// The drawing layer for this command
		uint16_t layer_;
		/// The visit order index for this command
		uint16_t visitOrder_;
		int numInstances_;
		int batchSize_;

		bool transformationCommitted_;

#if defined(NCINE_PROFILING)
		CommandTypes profilingType_;
#endif

		Recti scissorRect_;
		Matrix4x4f modelMatrix_;
		Material material_;
		Geometry geometry_;

		/// Returns the final layer sort key for this command
		inline uint32_t layerSortKey() const {
			return static_cast<uint32_t>(layer_ << 16) + visitOrder_;
		}
	};
}
