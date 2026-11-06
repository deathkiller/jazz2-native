#include "../CommonConstants.h"
#include "ParticleInitializer.h"
#include "../Base/Algorithms.h"
#include "../../Main.h"

namespace nCine
{
	void ParticleInitializer::SetAmount(std::int32_t amount)
	{
		DEATH_ASSERT(amount > 0);
		RndAmount.Set(amount, amount);
	}

	void ParticleInitializer::SetAmount(std::int32_t minAmount, std::int32_t maxAmount)
	{
		DEATH_ASSERT(minAmount <= maxAmount);
		RndAmount.Set(minAmount, maxAmount);
	}

	void ParticleInitializer::SetLife(float life)
	{
		RndLife.Set(life, life);
	}

	void ParticleInitializer::SetLife(float minLife, float maxLife)
	{
		DEATH_ASSERT(minLife <= maxLife);
		RndLife.Set(minLife, maxLife);
	}

	void ParticleInitializer::SetPosition(float x, float y)
	{
		RndPositionX.Set(x, x);
		RndPositionY.Set(y, y);
	}

	void ParticleInitializer::SetPosition(float minX, float minY, float maxX, float maxY)
	{
		DEATH_ASSERT(minX <= maxX && minY <= maxY);
		RndPositionX.Set(minX, maxX);
		RndPositionY.Set(minY, maxY);
	}

	void ParticleInitializer::SetPositionAndRadius(float x, float y, float radius)
	{
		DEATH_ASSERT(radius >= 0.0f);
		RndPositionX.Set(x - radius, x + radius);
		RndPositionY.Set(y - radius, y + radius);
	}

	void ParticleInitializer::SetPosition(Vector2f pos)
	{
		SetPosition(pos.X, pos.Y);
	}

	void ParticleInitializer::SetPosition(Vector2f minPos, Vector2f maxPos)
	{
		SetPosition(minPos.X, minPos.Y, maxPos.X, maxPos.Y);
	}

	void ParticleInitializer::SetPositionAndRadius(Vector2f pos, float radius)
	{
		SetPositionAndRadius(pos.X, pos.Y, radius);
	}

	void ParticleInitializer::SetPositionInDisc(float radius)
	{
		SetPositionAndRadius(0.0f, 0.0f, radius);
	}

	void ParticleInitializer::SetVelocity(float x, float y)
	{
		RndVelocityX.Set(x, x);
		RndVelocityY.Set(y, y);
	}

	void ParticleInitializer::SetVelocity(float minX, float minY, float maxX, float maxY)
	{
		DEATH_ASSERT(minX <= maxX && minY <= maxY);
		RndVelocityX.Set(minX, maxX);
		RndVelocityY.Set(minY, maxY);
	}

	void ParticleInitializer::SetVelocityAndRadius(float x, float y, float radius)
	{
		DEATH_ASSERT(radius >= 0.0f);
		RndVelocityX.Set(x - radius, x + radius);
		RndVelocityY.Set(y - radius, y + radius);
	}

	void ParticleInitializer::SetVelocityAndScale(float x, float y, float minScale, float maxScale)
	{
		DEATH_ASSERT(minScale <= maxScale);
		RndVelocityX.Set(x * minScale, x * maxScale);
		RndVelocityY.Set(y * minScale, y * maxScale);

		if (RndVelocityX.X > RndVelocityX.Y) {
			float tmp = RndVelocityX.X;
			RndVelocityX.X = RndVelocityX.Y;
			RndVelocityX.Y = tmp;
		}
		if (RndVelocityY.X > RndVelocityY.Y) {
			float tmp = RndVelocityY.X;
			RndVelocityY.X = RndVelocityY.Y;
			RndVelocityY.Y = tmp;
		}
	}

	void ParticleInitializer::SetVelocityAndAngle(float x, float y, float angle)
	{
		float sinAngle, cosAngle;
		sincosApprox(angle * 0.5f, sinAngle, cosAngle);

		RndVelocityX.X = x * cosAngle - y * sinAngle;
		RndVelocityX.Y = x * cosAngle - y * -sinAngle;
		RndVelocityY.X = x * sinAngle + y * cosAngle;
		RndVelocityY.Y = x * -sinAngle + y * cosAngle;

		if (RndVelocityX.X > RndVelocityX.Y) {
			float tmp = RndVelocityX.X;
			RndVelocityX.X = RndVelocityX.Y;
			RndVelocityX.Y = tmp;
		}
		if (RndVelocityY.X > RndVelocityY.Y) {
			float tmp = RndVelocityY.X;
			RndVelocityY.X = RndVelocityY.Y;
			RndVelocityY.Y = tmp;
		}
	}

	void ParticleInitializer::SetVelocity(Vector2f vel)
	{
		SetVelocity(vel.X, vel.Y);
	}

	void ParticleInitializer::SetVelocity(Vector2f minVel, Vector2f maxVel)
	{
		SetVelocity(minVel.X, minVel.Y, maxVel.X, maxVel.Y);
	}

	void ParticleInitializer::SetVelocityAndRadius(Vector2f vel, float radius)
	{
		SetVelocityAndRadius(vel.X, vel.Y, radius);
	}

	void ParticleInitializer::SetVelocityAndScale(Vector2f vel, float minScale, float maxScale)
	{
		SetVelocityAndScale(vel.X, vel.Y, minScale, maxScale);
	}

	void ParticleInitializer::SetVelocityAndAngle(Vector2f vel, float angle)
	{
		SetVelocityAndAngle(vel.X, vel.Y, angle);
	}

	void ParticleInitializer::SetRotation(float rot)
	{
		RndRotation.Set(rot, rot);
	}

	void ParticleInitializer::SetRotation(float minRot, float maxRot)
	{
		DEATH_ASSERT(minRot <= maxRot);
		RndRotation.Set(minRot, maxRot);
	}
}
