#pragma once

#include "Sprite.h"
#include "../Primitives/Vector2.h"
#include "../Primitives/Color.h"

namespace nCine
{
	class Texture;

	/**
		@brief Renders a single particle
		
		Lightweight @ref Sprite owned and recycled by a @ref ParticleSystem. Carries the simulation state used
		by particle affectors (remaining and initial life, initial rotation, velocity) and overrides updating
		and transformation so the particle can move independently of its parent, optionally in local space.
	*/
	class Particle : public Sprite
	{
		friend class ParticleSystem;

	public:
		/** @brief Current particle remaining life in seconds */
		float life_;
		/** @brief Initial particle remaining life */
		float startingLife; // for affectors
		/** @brief Initial particle rotation */
		float startingRotation; // for affectors
		/** @brief Current particle velocity vector */
		Vector2f velocity_;
		/** @brief Whether particle transformations are in local space */
		bool inLocalSpace_;

		/** @brief Constructor for a particle with a parent and texture, positioned in the relative origin */
		Particle(SceneNode* parent, Texture* texture);

		Particle& operator=(const Particle&) = delete;
		Particle(Particle&&) = default;
		Particle& operator=(Particle&&) = default;

		/** @brief Returns `true` if the particle is still alive */
		inline bool isAlive() const {
			return life_ > 0.0f;
		}

	protected:
		/**
		 * @brief Returns a copy of this object
		 *
		 * @note This method is protected as it should only be called by a `ParticleSystem`.
		 */
		inline Particle clone() const {
			return Particle(*this);
		}

		/** @brief Protected copy constructor used to clone objects */
		Particle(const Particle& other);

	private:
		/** @brief Initializes a particle with initial life, position, velocity and rotation */
		void init(float life, Vector2f pos, Vector2f vel, float rot, bool inLocalSpace);

		void OnUpdate(float timeMult) override;
		void transform() override;
	};

}
