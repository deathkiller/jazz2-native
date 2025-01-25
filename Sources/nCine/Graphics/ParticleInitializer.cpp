#include "../CommonConstants.h"
#include "ParticleInitializer.h"
#include "../../Main.h"

namespace nCine
{
	void ParticleInitializer::setAmount(std::int32_t amount)
	{
		ASSERT(amount > 0);
		rndAmount.Set(amount, amount);
	}

	void ParticleInitializer::setAmount(std::int32_t minAmount, std::int32_t maxAmount)
	{
		ASSERT(minAmount <= maxAmount);
		rndAmount.Set(minAmount, maxAmount);
	}

	void ParticleInitializer::setLife(float life)
	{
		rndLife.Set(life, life);
	}

	void ParticleInitializer::setLife(float minLife, float maxLife)
	{
		ASSERT(minLife <= maxLife);
		rndLife.Set(minLife, maxLife);
	}

	void ParticleInitializer::setPosition(float x, float y)
	{
		rndPositionX.Set(x, x);
		rndPositionY.Set(y, y);
	}

	void ParticleInitializer::setPosition(float minX, float minY, float maxX, float maxY)
	{
		ASSERT(minX <= maxX && minY <= maxY);
		rndPositionX.Set(minX, maxX);
		rndPositionY.Set(minY, maxY);
	}

	void ParticleInitializer::setPositionAndRadius(float x, float y, float radius)
	{
		ASSERT(radius >= 0.0f);
		rndPositionX.Set(x - radius, x + radius);
		rndPositionY.Set(y - radius, y + radius);
	}

	void ParticleInitializer::setPosition(Vector2f pos)
	{
		setPosition(pos.X, pos.Y);
	}

	void ParticleInitializer::setPosition(Vector2f minPos, Vector2f maxPos)
	{
		setPosition(minPos.X, minPos.Y, maxPos.X, maxPos.Y);
	}

	void ParticleInitializer::setPositionAndRadius(Vector2f pos, float radius)
	{
		setPositionAndRadius(pos.X, pos.Y, radius);
	}

	void ParticleInitializer::setPositionInDisc(float radius)
	{
		setPositionAndRadius(0.0f, 0.0f, radius);
	}

	void ParticleInitializer::setVelocity(float x, float y)
	{
		rndVelocityX.Set(x, x);
		rndVelocityY.Set(y, y);
	}

	void ParticleInitializer::setVelocity(float minX, float minY, float maxX, float maxY)
	{
		ASSERT(minX <= maxX && minY <= maxY);
		rndVelocityX.Set(minX, maxX);
		rndVelocityY.Set(minY, maxY);
	}

	void ParticleInitializer::setVelocityAndRadius(float x, float y, float radius)
	{
		ASSERT(radius >= 0.0f);
		rndVelocityX.Set(x - radius, x + radius);
		rndVelocityY.Set(y - radius, y + radius);
	}

	void ParticleInitializer::setVelocityAndScale(float x, float y, float minScale, float maxScale)
	{
		ASSERT(minScale <= maxScale);
		rndVelocityX.Set(x * minScale, x * maxScale);
		rndVelocityY.Set(y * minScale, y * maxScale);

		if (rndVelocityX.X > rndVelocityX.Y) {
			float tmp = rndVelocityX.X;
			rndVelocityX.X = rndVelocityX.Y;
			rndVelocityX.Y = tmp;
		}
		if (rndVelocityY.X > rndVelocityY.Y) {
			float tmp = rndVelocityY.X;
			rndVelocityY.X = rndVelocityY.Y;
			rndVelocityY.Y = tmp;
		}
	}

	void ParticleInitializer::setVelocityAndAngle(float x, float y, float angle)
	{
		const float sinAngle = sinf(angle * 0.5f);
		const float cosAngle = cosf(angle * 0.5f);

		rndVelocityX.X = x * cosAngle - y * sinAngle;
		rndVelocityX.Y = x * cosAngle - y * -sinAngle;
		rndVelocityY.X = x * sinAngle + y * cosAngle;
		rndVelocityY.Y = x * -sinAngle + y * cosAngle;

		if (rndVelocityX.X > rndVelocityX.Y) {
			float tmp = rndVelocityX.X;
			rndVelocityX.X = rndVelocityX.Y;
			rndVelocityX.Y = tmp;
		}
		if (rndVelocityY.X > rndVelocityY.Y) {
			float tmp = rndVelocityY.X;
			rndVelocityY.X = rndVelocityY.Y;
			rndVelocityY.Y = tmp;
		}
	}

	void ParticleInitializer::setVelocity(Vector2f vel)
	{
		setVelocity(vel.X, vel.Y);
	}

	void ParticleInitializer::setVelocity(Vector2f minVel, Vector2f maxVel)
	{
		setVelocity(minVel.X, minVel.Y, maxVel.X, maxVel.Y);
	}

	void ParticleInitializer::setVelocityAndRadius(Vector2f vel, float radius)
	{
		setVelocityAndRadius(vel.X, vel.Y, radius);
	}

	void ParticleInitializer::setVelocityAndScale(Vector2f vel, float minScale, float maxScale)
	{
		setVelocityAndScale(vel.X, vel.Y, minScale, maxScale);
	}

	void ParticleInitializer::setVelocityAndAngle(Vector2f vel, float angle)
	{
		setVelocityAndAngle(vel.X, vel.Y, angle);
	}

	void ParticleInitializer::setRotation(float rot)
	{
		rndRotation.Set(rot, rot);
	}

	void ParticleInitializer::setRotation(float minRot, float maxRot)
	{
		ASSERT(minRot <= maxRot);
		rndRotation.Set(minRot, maxRot);
	}
}
