#include "ParticleSystem.h"
#include "../Base/Algorithms.h"
#include "../Base/Random.h"
#include "../Primitives/Vector2.h"
#include "Particle.h"
#include "ParticleInitializer.h"
#include "Texture.h"
#include "../Application.h"
#include "../tracy.h"

namespace nCine
{
	ParticleSystem::ParticleSystem(SceneNode* parent, std::uint32_t count, Texture* texture)
		: ParticleSystem(parent, count, texture, Recti(0, 0, texture->GetWidth(), texture->GetHeight()))
	{
	}

	ParticleSystem::ParticleSystem(SceneNode* parent, std::uint32_t count, Texture* texture, Recti texRect)
		: SceneNode(parent, 0, 0), _poolSize(count), _poolTop(count - 1), _particlePool(_poolSize),
			_particleArray(_poolSize), _affectors(4), _inLocalSpace(false)
	{
		/*if (texture && texture->name() != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(texture->name(), strnlen(texture->name(), Object::MaxNameLength));
		}*/

		_type = ObjectType::ParticleSystem;

		_children.reserve(_poolSize);
		for (std::uint32_t i = 0; i < _poolSize; i++) {
			Particle* particle = new Particle(nullptr, texture);
			particle->SetTexRect(texRect);
			_particlePool.push_back(particle);
			_particleArray.push_back(particle);
		}
	}

	ParticleSystem::~ParticleSystem()
	{
		// Empty the children list before the mass deletion
		_children.clear();

		for (auto& affector : _affectors) {
			delete affector;
		}

		for (auto& particle : _particleArray) {
			delete particle;
		}
	}

	ParticleSystem::ParticleSystem(ParticleSystem&&) = default;

	ParticleSystem& ParticleSystem::operator=(ParticleSystem&&) = default;

	void ParticleSystem::ClearAffectors()
	{
		for (auto& affector : _affectors) {
			delete affector;
		}
		_affectors.clear();
	}

	void ParticleSystem::EmitParticles(const ParticleInitializer& init)
	{
		if (!_updateEnabled) {
			return;
		}

		std::uint32_t amount = std::uint32_t(Random().Next(init.RndAmount.X, init.RndAmount.Y));
#if defined(WITH_TRACY)
		// TODO: Tracy
		//tracyInfoString.format("Count: %d", amount);
		//ZoneText(tracyInfoString.data(), tracyInfoString.length());
#endif
		Vector2f position(0.0f, 0.0f);
		Vector2f velocity(0.0f, 0.0f);

		for (std::uint32_t i = 0; i < amount; i++) {
			// No more unused particles in the pool
			if (_poolTop < 0) {
				break;
			}

			const float life = Random().NextFloat(init.RndLife.X, init.RndLife.Y);
			position.X = Random().NextFloat(init.RndPositionX.X, init.RndPositionX.Y);
			position.Y = Random().NextFloat(init.RndPositionY.X, init.RndPositionY.Y);
			velocity.X = Random().NextFloat(init.RndVelocityX.X, init.RndVelocityX.Y);
			velocity.Y = Random().NextFloat(init.RndVelocityY.X, init.RndVelocityY.Y);

			float rotation = 0.0f;
			if (init.EmitterRotation) {
				// Particles are rotated towards the emission vector
				rotation = (atan2Approx(velocity.Y, velocity.X) - atan2Approx(1.0f, 0.0f)) * 180.0f / fPi;
				if (rotation < 0.0f) {
					rotation += 360.0f;
				}
			} else {
				rotation = Random().NextFloat(init.RndRotation.X, init.RndRotation.Y);
			}

			if (!_inLocalSpace) {
				position += GetAbsPosition();
			}

			// Acquiring a particle from the pool
			_particlePool[_poolTop]->Init(life, position, velocity, rotation, _inLocalSpace);
			AddChildNode(_particlePool[_poolTop]);
			_poolTop--;
		}
	}

	void ParticleSystem::KillParticles()
	{
		for (std::int32_t i = std::int32_t(_children.size()) - 1; i >= 0; i--) {
			Particle* particle = static_cast<Particle*>(_children[i]);

			if (particle->IsAlive()) {
				particle->_life = 0.0f;
				particle->SetEnabled(false);

				_poolTop++;
				_particlePool[_poolTop] = particle;
				RemoveChildNodeAt(i);
			}
		}
	}

	void ParticleSystem::SetTexture(Texture* texture)
	{
		for (auto& particle : _particleArray) {
			particle->SetTexture(texture);
		}
	}

	void ParticleSystem::SetTexRect(const Recti& rect)
	{
		for (auto& particle : _particleArray) {
			particle->SetTexRect(rect);
		}
	}

	void ParticleSystem::SetAnchorPoint(float xx, float yy)
	{
		for (auto& particle : _particleArray) {
			particle->SetAnchorPoint(xx, yy);
		}
	}

	void ParticleSystem::SetAnchorPoint(Vector2f point)
	{
		for (auto& particle : _particleArray) {
			particle->SetAnchorPoint(point);
		}
	}

	void ParticleSystem::SetFlippedX(bool flippedX)
	{
		for (auto& particle : _particleArray) {
			particle->SetFlippedX(flippedX);
		}
	}

	void ParticleSystem::SetFlippedY(bool flippedY)
	{
		for (auto& particle : _particleArray) {
			particle->SetFlippedY(flippedY);
		}
	}

	void ParticleSystem::SetBlendingPreset(DrawableNode::BlendingPreset blendingPreset)
	{
		for (auto& particle : _particleArray) {
			particle->SetBlendingPreset(blendingPreset);
		}
	}

	void ParticleSystem::SetBlendingFactors(DrawableNode::BlendingFactor srcBlendingFactor, DrawableNode::BlendingFactor destBlendingFactor)
	{
		for (auto& particle : _particleArray) {
			particle->SetBlendingFactors(srcBlendingFactor, destBlendingFactor);
		}
	}

	void ParticleSystem::SetLayer(std::uint16_t layer)
	{
		for (auto& particle : _particleArray) {
			particle->SetLayer(layer);
		}
	}

	void ParticleSystem::OnUpdate(float timeMult)
	{
		if (!_updateEnabled) {
			return;
		}

		// Overridden `update()` method should call `Transform()` like `SceneNode::Update()` does
		SceneNode::Transform();

		for (std::int32_t i = std::int32_t(_children.size()) - 1; i >= 0; i--) {
			Particle* particle = static_cast<Particle*>(_children[i]);

			// Update the particle if it's alive
			if (particle->IsAlive()) {
				// Calculating the normalized age only once per particle
				const float normalizedAge = 1.0f - particle->_life / particle->startingLife;
				for (auto& affector : _affectors) {
					affector->Affect(particle, normalizedAge);
				}

				particle->OnUpdate(timeMult);

				// Releasing the particle if it has just died
				if (!particle->IsAlive()) {
					_poolTop++;
					_particlePool[_poolTop] = particle;
					RemoveChildNodeAt(i);
					continue;
				}

				// Transforming the particle only if it's still alive
				particle->Transform();
			}
		}

		_lastFrameUpdated = theApplication().GetFrameCount();

#if defined(WITH_TRACY)
		// TODO: Tracy
		//tracyInfoString.format("Alive: %d", numAliveParticles());
		//ZoneText(tracyInfoString.data(), tracyInfoString.length());
#endif
	}

	ParticleSystem::ParticleSystem(const ParticleSystem& other)
		: SceneNode(other), _poolSize(other._poolSize), _poolTop(other._poolSize - 1), _particlePool(other._poolSize),
			_particleArray(other._poolSize), _affectors(4), _inLocalSpace(other._inLocalSpace)
	{

		_type = ObjectType::ParticleSystem;

		for (std::uint32_t i = 0; i < other._affectors.size(); i++) {
			const ParticleAffector& affector = *other._affectors[i];
			switch (affector.GetType()) {
				case ParticleAffector::Type::Color:
					_affectors.push_back(new ColorAffector(static_cast<const ColorAffector&>(affector).Clone()));
					break;
				case ParticleAffector::Type::Size:
					_affectors.push_back(new SizeAffector(static_cast<const SizeAffector&>(affector).Clone()));
					break;
				case ParticleAffector::Type::Rotation:
					_affectors.push_back(new RotationAffector(static_cast<const RotationAffector&>(affector).Clone()));
					break;
				case ParticleAffector::Type::Position:
					_affectors.push_back(new PositionAffector(static_cast<const PositionAffector&>(affector).Clone()));
					break;
				case ParticleAffector::Type::Velocity:
					_affectors.push_back(new VelocityAffector(static_cast<const VelocityAffector&>(affector).Clone()));
					break;
			}
		}

		_children.reserve(_poolSize);
		if (_poolSize > 0) {
			const Particle& otherParticle = *other._particlePool.front();
			// TODO: Tracy
			/*if (otherParticle.texture() && otherParticle.texture()->name() != nullptr) {
				// When Tracy is disabled the statement body is empty and braces are needed
				ZoneText(otherParticle.texture()->name(), strnlen(otherParticle.texture()->name(), Object::MaxNameLength));
			}*/

			for (std::uint32_t i = 0; i < _poolSize; i++) {
				Particle* particle = new Particle(otherParticle.Clone());
				_particlePool.push_back(particle);
				_particleArray.push_back(particle);
			}
		}
	}
}
