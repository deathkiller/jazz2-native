#pragma once

#include "../Primitives/Vector2.h"

namespace nCine
{
	/**
		@brief Initialization parameters for a batch of emitted particles
		
		Each field is a `[min, max]` range from which a random value is drawn per particle when
		@ref ParticleSystem::EmitParticles() is called. The setter helpers fill these ranges from various
		argument forms, such as a single value, an explicit interval, a radius or an emission angle.
	*/
	struct ParticleInitializer
	{
		/** @brief Range of the number of particles to emit */
		Vector2i RndAmount = Vector2i(1, 1);
		/** @brief Range of the particle life in seconds */
		Vector2f RndLife = Vector2f(1.0f, 1.0f);
		/** @brief Range of the initial horizontal position offset */
		Vector2f RndPositionX = Vector2f::Zero;
		/** @brief Range of the initial vertical position offset */
		Vector2f RndPositionY = Vector2f::Zero;
		/** @brief Range of the initial horizontal velocity */
		Vector2f RndVelocityX = Vector2f::Zero;
		/** @brief Range of the initial vertical velocity */
		Vector2f RndVelocityY = Vector2f::Zero;
		/** @brief Range of the initial rotation in degrees, used only when @ref emitterRotation is `false` */
		Vector2f RndRotation = Vector2f::Zero;
		/** @brief Whether particles are rotated to face their emission velocity instead of using @ref rndRotation */
		bool EmitterRotation = true;

		/** @brief Sets a fixed number of particles to emit */
		void SetAmount(std::int32_t amount);
		/** @brief Sets the range of the number of particles to emit */
		void SetAmount(std::int32_t minAmount, std::int32_t maxAmount);

		/** @brief Sets a fixed particle life in seconds */
		void SetLife(float life);
		/** @brief Sets the range of the particle life in seconds */
		void SetLife(float minLife, float maxLife);

		/** @brief Sets a fixed initial position offset */
		void SetPosition(float x, float y);
		/** @brief Sets the range of the initial position offset */
		void SetPosition(float minX, float minY, float maxX, float maxY);
		/** @brief Sets a square position range centered on the specified point */
		void SetPositionAndRadius(float x, float y, float radius);
		/** @brief Sets a fixed initial position offset */
		void SetPosition(Vector2f pos);
		/** @brief Sets the range of the initial position offset */
		void SetPosition(Vector2f minPos, Vector2f maxPos);
		/** @brief Sets a square position range centered on the specified point */
		void SetPositionAndRadius(Vector2f pos, float radius);
		/** @brief Sets a square position range centered on the origin */
		void SetPositionInDisc(float radius);

		/** @brief Sets a fixed initial velocity */
		void SetVelocity(float x, float y);
		/** @brief Sets the range of the initial velocity */
		void SetVelocity(float minX, float minY, float maxX, float maxY);
		/** @brief Sets a square velocity range centered on the specified vector */
		void SetVelocityAndRadius(float x, float y, float radius);
		/** @brief Sets a velocity range by scaling the specified vector between two factors */
		void SetVelocityAndScale(float x, float y, float minScale, float maxScale);
		/** @brief Sets a velocity range by rotating the specified vector by half the given angle in both directions */
		void SetVelocityAndAngle(float x, float y, float angle);
		/** @brief Sets a fixed initial velocity */
		void SetVelocity(Vector2f vel);
		/** @brief Sets the range of the initial velocity */
		void SetVelocity(Vector2f minVel, Vector2f maxVel);
		/** @brief Sets a square velocity range centered on the specified vector */
		void SetVelocityAndRadius(Vector2f vel, float radius);
		/** @brief Sets a velocity range by scaling the specified vector between two factors */
		void SetVelocityAndScale(Vector2f vel, float minScale, float maxScale);
		/** @brief Sets a velocity range by rotating the specified vector by half the given angle in both directions */
		void SetVelocityAndAngle(Vector2f vel, float angle);

		/** @brief Sets a fixed initial rotation in degrees */
		void SetRotation(float rot);
		/** @brief Sets the range of the initial rotation in degrees */
		void SetRotation(float minRot, float maxRot);
	};

}
