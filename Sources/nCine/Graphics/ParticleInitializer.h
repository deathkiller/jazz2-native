#pragma once

#include "../Primitives/Vector2.h"

namespace nCine
{
	/// Initialization parameters for particles
	/*! The vectors define a range between a minimum and a maximum value */
	struct ParticleInitializer
	{
		Vector2i rndAmount = Vector2i(1, 1);
		Vector2f rndLife = Vector2f(1.0f, 1.0f);
		Vector2f rndPositionX = Vector2f::Zero;
		Vector2f rndPositionY = Vector2f::Zero;
		Vector2f rndVelocityX = Vector2f::Zero;
		Vector2f rndVelocityY = Vector2f::Zero;
		Vector2f rndRotation = Vector2f::Zero;
		bool emitterRotation = true;

		void setAmount(int amount);
		void setAmount(int minAmount, int maxAmount);

		void setLife(float life);
		void setLife(float minLife, float maxLife);

		void setPosition(float x, float y);
		void setPosition(float minX, float minY, float maxX, float maxY);
		void setPositionAndRadius(float x, float y, float radius);
		void setPosition(Vector2f pos);
		void setPosition(Vector2f minPos, Vector2f maxPos);
		void setPositionAndRadius(Vector2f pos, float radius);
		void setPositionInDisc(float radius);

		void setVelocity(float x, float y);
		void setVelocity(float minX, float minY, float maxX, float maxY);
		void setVelocityAndRadius(float x, float y, float radius);
		void setVelocityAndScale(float x, float y, float minScale, float maxScale);
		void setVelocityAndAngle(float x, float y, float angle);
		void setVelocity(Vector2f vel);
		void setVelocity(Vector2f minVel, Vector2f maxVel);
		void setVelocityAndRadius(Vector2f vel, float radius);
		void setVelocityAndScale(Vector2f vel, float minScale, float maxScale);
		void setVelocityAndAngle(Vector2f vel, float angle);

		void setRotation(float rot);
		void setRotation(float minRot, float maxRot);
	};

}
