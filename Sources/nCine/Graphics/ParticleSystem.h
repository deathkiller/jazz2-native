#pragma once

#include <ctime>
#include <cstdlib>
#include "../Primitives/Rect.h"
#include "SceneNode.h"
#include "ParticleAffectors.h"
#include "BaseSprite.h"

namespace nCine
{
	class Texture;
	class Particle;
	struct ParticleInitializer;

	/**
		@brief Scene node that emits and simulates a pool of textured particles
		
		Owns a fixed pool of @ref Particle child nodes that are recycled between alive and dead states.
		@ref emitParticles() spawns particles from a @ref ParticleInitializer, and each frame the attached
		@ref ParticleAffector list animates every alive particle before it is drawn as part of the scene graph.
	*/
	class ParticleSystem : public SceneNode
	{
	public:
		/** @brief Constructs a particle system with the specified maximum amount of particles */
		ParticleSystem(SceneNode* parent, std::uint32_t count, Texture* texture);
		/** @brief Constructs a particle system with the specified maximum amount of particles and the specified texture rectangle */
		ParticleSystem(SceneNode* parent, std::uint32_t count, Texture* texture, Recti texRect);
		~ParticleSystem() override;

		ParticleSystem& operator=(const ParticleSystem&) = delete;
		ParticleSystem(ParticleSystem&&);
		ParticleSystem& operator=(ParticleSystem&&);

		/** @brief Returns a copy of this object */
		inline ParticleSystem clone() const {
			return ParticleSystem(*this);
		}

		/** @brief Adds a particle affector, taking ownership of it */
		inline void addAffector(std::unique_ptr<ParticleAffector> affector) {
			affectors_.push_back(affector.release());
		}
		/** @brief Deletes all particle affectors */
		void clearAffectors();
		/** @brief Emits particles with the specified initialization parameters */
		void emitParticles(const ParticleInitializer& init);
		/** @brief Kills all alive particles, returning them to the pool */
		void killParticles();

		/** @brief Returns whether the system is simulated in local space */
		inline bool inLocalSpace() const {
			return inLocalSpace_;
		}
		/** @brief Sets whether the system is simulated in local space */
		inline void setInLocalSpace(bool inLocalSpace) {
			inLocalSpace_ = inLocalSpace;
		}

		/** @brief Returns the total number of particles in the pool */
		inline std::uint32_t numParticles() const {
			return std::uint32_t(particleArray_.size());
		}
		/** @brief Returns the number of particles currently alive */
		inline std::uint32_t numAliveParticles() const {
			return std::uint32_t(particleArray_.size()) - poolTop_ - 1;
		}

		/** @brief Sets the texture object for every particle */
		void setTexture(Texture* texture);
		/** @brief Sets the texture source rectangle for every particle */
		void setTexRect(const Recti& rect);

		/** @brief Sets the transformation anchor point for every particle */
		void setAnchorPoint(float xx, float yy);
		/** @brief Sets the transformation anchor point for every particle with a `Vector2f` */
		void setAnchorPoint(Vector2f point);

		/** @brief Flips the texture rect horizontally for every particle */
		void setFlippedX(bool flippedX);
		/** @brief Flips the texture rect vertically for every particle */
		void setFlippedY(bool flippedY);

		/** @brief Sets the blending factors preset for every particle */
		void setBlendingPreset(DrawableNode::BlendingPreset blendingPreset);
		/** @brief Sets the source and destination blending factors for every particle */
		void setBlendingFactors(DrawableNode::BlendingFactor srcBlendingFactor, DrawableNode::BlendingFactor destBlendingFactor);

		/** @brief Sets the rendering layer for every particle */
		void setLayer(uint16_t layer);

		void OnUpdate(float timeMult) override;

		/** @brief Returns the static object type of the class */
		inline static ObjectType sType() {
			return ObjectType::ParticleSystem;
		}

	protected:
		/** @brief Protected copy constructor used to clone objects */
		ParticleSystem(const ParticleSystem& other);

	private:
		/** @brief Particle pool size */
		std::uint32_t poolSize_;
		/** @brief Index of the next free particle in the pool */
		std::int32_t poolTop_;
		/** @brief Pool containing available particles (only dead ones) */
		SmallVector<Particle*, 0> particlePool_;
		/** @brief Array containing every particle (dead or alive) */
		SmallVector<Particle*, 0> particleArray_;

		/** @brief Array of particle affectors */
		SmallVector<ParticleAffector*, 0> affectors_;

		/** @brief Whether the system is simulated in local space */
		bool inLocalSpace_;
	};

}
