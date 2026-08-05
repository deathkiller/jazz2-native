#include "ParticleSystem.h"
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
			particle->setTexRect(texRect);
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

	void ParticleSystem::clearAffectors()
	{
		for (auto& affector : _affectors) {
			delete affector;
		}
		_affectors.clear();
	}

	void ParticleSystem::emitParticles(const ParticleInitializer& init)
	{
		if (!_updateEnabled) {
			return;
		}

		std::uint32_t amount = std::uint32_t(Random().Next(init.rndAmount.X, init.rndAmount.Y));
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

			const float life = Random().NextFloat(init.rndLife.X, init.rndLife.Y);
			position.X = Random().NextFloat(init.rndPositionX.X, init.rndPositionX.Y);
			position.Y = Random().NextFloat(init.rndPositionY.X, init.rndPositionY.Y);
			velocity.X = Random().NextFloat(init.rndVelocityX.X, init.rndVelocityX.Y);
			velocity.Y = Random().NextFloat(init.rndVelocityY.X, init.rndVelocityY.Y);

			float rotation = 0.0f;
			if (init.emitterRotation) {
				// Particles are rotated towards the emission vector
				rotation = (atan2f(velocity.Y, velocity.X) - atan2f(1.0f, 0.0f)) * 180.0f / fPi;
				if (rotation < 0.0f) {
					rotation += 360.0f;
				}
			} else {
				rotation = Random().NextFloat(init.rndRotation.X, init.rndRotation.Y);
			}

			if (!_inLocalSpace) {
				position += absPosition();
			}

			// Acquiring a particle from the pool
			_particlePool[_poolTop]->init(life, position, velocity, rotation, _inLocalSpace);
			addChildNode(_particlePool[_poolTop]);
			_poolTop--;
		}
	}

	void ParticleSystem::killParticles()
	{
		for (std::int32_t i = std::int32_t(_children.size()) - 1; i >= 0; i--) {
			Particle* particle = static_cast<Particle*>(_children[i]);

			if (particle->isAlive()) {
				particle->_life = 0.0f;
				particle->setEnabled(false);

				_poolTop++;
				_particlePool[_poolTop] = particle;
				removeChildNodeAt(i);
			}
		}
	}

	void ParticleSystem::setTexture(Texture* texture)
	{
		for (auto& particle : _particleArray) {
			particle->setTexture(texture);
		}
	}

	void ParticleSystem::setTexRect(const Recti& rect)
	{
		for (auto& particle : _particleArray) {
			particle->setTexRect(rect);
		}
	}

	void ParticleSystem::setAnchorPoint(float xx, float yy)
	{
		for (auto& particle : _particleArray) {
			particle->setAnchorPoint(xx, yy);
		}
	}

	void ParticleSystem::setAnchorPoint(Vector2f point)
	{
		for (auto& particle : _particleArray) {
			particle->setAnchorPoint(point);
		}
	}

	void ParticleSystem::setFlippedX(bool flippedX)
	{
		for (auto& particle : _particleArray) {
			particle->setFlippedX(flippedX);
		}
	}

	void ParticleSystem::setFlippedY(bool flippedY)
	{
		for (auto& particle : _particleArray) {
			particle->setFlippedY(flippedY);
		}
	}

	void ParticleSystem::setBlendingPreset(DrawableNode::BlendingPreset blendingPreset)
	{
		for (auto& particle : _particleArray) {
			particle->setBlendingPreset(blendingPreset);
		}
	}

	void ParticleSystem::setBlendingFactors(DrawableNode::BlendingFactor srcBlendingFactor, DrawableNode::BlendingFactor destBlendingFactor)
	{
		for (auto& particle : _particleArray) {
			particle->setBlendingFactors(srcBlendingFactor, destBlendingFactor);
		}
	}

	void ParticleSystem::setLayer(std::uint16_t layer)
	{
		for (auto& particle : _particleArray) {
			particle->setLayer(layer);
		}
	}

	void ParticleSystem::OnUpdate(float timeMult)
	{
		if (!_updateEnabled) {
			return;
		}

		// Overridden `update()` method should call `transform()` like `SceneNode::update()` does
		SceneNode::transform();

		for (std::int32_t i = std::int32_t(_children.size()) - 1; i >= 0; i--) {
			Particle* particle = static_cast<Particle*>(_children[i]);

			// Update the particle if it's alive
			if (particle->isAlive()) {
				// Calculating the normalized age only once per particle
				const float normalizedAge = 1.0f - particle->_life / particle->startingLife;
				for (auto& affector : _affectors) {
					affector->affect(particle, normalizedAge);
				}

				particle->OnUpdate(timeMult);

				// Releasing the particle if it has just died
				if (!particle->isAlive()) {
					_poolTop++;
					_particlePool[_poolTop] = particle;
					removeChildNodeAt(i);
					continue;
				}

				// Transforming the particle only if it's still alive
				particle->transform();
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
			switch (affector.type()) {
				case ParticleAffector::Type::Color:
					_affectors.push_back(new ColorAffector(static_cast<const ColorAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::Size:
					_affectors.push_back(new SizeAffector(static_cast<const SizeAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::Rotation:
					_affectors.push_back(new RotationAffector(static_cast<const RotationAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::Position:
					_affectors.push_back(new PositionAffector(static_cast<const PositionAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::Velocity:
					_affectors.push_back(new VelocityAffector(static_cast<const VelocityAffector&>(affector).clone()));
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
				Particle* particle = new Particle(otherParticle.clone());
				_particlePool.push_back(particle);
				_particleArray.push_back(particle);
			}
		}
	}
}
