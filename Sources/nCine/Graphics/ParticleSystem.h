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
		@ref EmitParticles() spawns particles from a @ref ParticleInitializer, and each frame the attached
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
		inline ParticleSystem Clone() const {
			return ParticleSystem(*this);
		}

		/** @brief Adds a particle affector, taking ownership of it */
		inline void AddAffector(std::unique_ptr<ParticleAffector> affector) {
			_affectors.push_back(affector.release());
		}
		/** @brief Deletes all particle affectors */
		void ClearAffectors();
		/** @brief Emits particles with the specified initialization parameters */
		void EmitParticles(const ParticleInitializer& init);
		/** @brief Kills all alive particles, returning them to the pool */
		void KillParticles();

		/** @brief Returns whether the system is simulated in local space */
		inline bool IsInLocalSpace() const {
			return _inLocalSpace;
		}
		/** @brief Sets whether the system is simulated in local space */
		inline void SetInLocalSpace(bool inLocalSpace) {
			_inLocalSpace = inLocalSpace;
		}

		/** @brief Returns the total number of particles in the pool */
		inline std::uint32_t GetParticleCount() const {
			return std::uint32_t(_particleArray.size());
		}
		/** @brief Returns the number of particles currently alive */
		inline std::uint32_t GetAliveParticleCount() const {
			return std::uint32_t(_particleArray.size()) - _poolTop - 1;
		}

		/** @brief Sets the texture object for every particle */
		void SetTexture(Texture* texture);
		/** @brief Sets the texture source rectangle for every particle */
		void SetTexRect(const Recti& rect);

		/** @brief Sets the transformation anchor point for every particle */
		void SetAnchorPoint(float xx, float yy);
		/** @brief Sets the transformation anchor point for every particle with a `Vector2f` */
		void SetAnchorPoint(Vector2f point);

		/** @brief Flips the texture rect horizontally for every particle */
		void SetFlippedX(bool flippedX);
		/** @brief Flips the texture rect vertically for every particle */
		void SetFlippedY(bool flippedY);

		/** @brief Sets the blending factors preset for every particle */
		void SetBlendingPreset(DrawableNode::BlendingPreset blendingPreset);
		/** @brief Sets the source and destination blending factors for every particle */
		void SetBlendingFactors(DrawableNode::BlendingFactor srcBlendingFactor, DrawableNode::BlendingFactor destBlendingFactor);

		/** @brief Sets the rendering layer for every particle */
		void SetLayer(uint16_t layer);

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
		std::uint32_t _poolSize;
		/** @brief Index of the next free particle in the pool */
		std::int32_t _poolTop;
		/** @brief Pool containing available particles (only dead ones) */
		SmallVector<Particle*, 0> _particlePool;
		/** @brief Array containing every particle (dead or alive) */
		SmallVector<Particle*, 0> _particleArray;

		/** @brief Array of particle affectors */
		SmallVector<ParticleAffector*, 0> _affectors;

		/** @brief Whether the system is simulated in local space */
		bool _inLocalSpace;
	};

}
