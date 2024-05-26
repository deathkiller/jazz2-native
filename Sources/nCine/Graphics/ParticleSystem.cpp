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
	ParticleSystem::ParticleSystem(SceneNode* parent, unsigned int count, Texture* texture)
		: ParticleSystem(parent, count, texture, Recti(0, 0, texture->width(), texture->height()))
	{
	}

	ParticleSystem::ParticleSystem(SceneNode* parent, unsigned int count, Texture* texture, Recti texRect)
		: SceneNode(parent, 0, 0), poolSize_(count), poolTop_(count - 1), particlePool_(poolSize_),
			particleArray_(poolSize_), affectors_(4), inLocalSpace_(false)
	{
		/*if (texture && texture->name() != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(texture->name(), strnlen(texture->name(), Object::MaxNameLength));
		}*/

		_type = ObjectType::ParticleSystem;

		children_.reserve(poolSize_);
		for (unsigned int i = 0; i < poolSize_; i++) {
			Particle* particle = new Particle(nullptr, texture);
			particle->setTexRect(texRect);
			particlePool_.push_back(particle);
			particleArray_.push_back(particle);
		}
	}

	ParticleSystem::~ParticleSystem()
	{
		// Empty the children list before the mass deletion
		children_.clear();

		for (auto& affector : affectors_) {
			delete affector;
		}

		for (auto& particle : particleArray_) {
			delete particle;
		}
	}

	ParticleSystem::ParticleSystem(ParticleSystem&&) = default;

	ParticleSystem& ParticleSystem::operator=(ParticleSystem&&) = default;

	void ParticleSystem::clearAffectors()
	{
		for (auto& affector : affectors_) {
			delete affector;
		}
		affectors_.clear();
	}

	void ParticleSystem::emitParticles(const ParticleInitializer& init)
	{
		if (!updateEnabled_) {
			return;
		}

		const unsigned int amount = static_cast<unsigned int>(Random().Next(init.rndAmount.X, init.rndAmount.Y));
#if defined(WITH_TRACY)
		// TODO: Tracy
		//tracyInfoString.format("Count: %d", amount);
		//ZoneText(tracyInfoString.data(), tracyInfoString.length());
#endif
		Vector2f position(0.0f, 0.0f);
		Vector2f velocity(0.0f, 0.0f);

		for (unsigned int i = 0; i < amount; i++) {
			// No more unused particles in the pool
			if (poolTop_ < 0) {
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
				rotation = (atan2f(velocity.X, velocity.X) - atan2f(1.0f, 0.0f)) * 180.0f / fPi;
				if (rotation < 0.0f) {
					rotation += 360.0f;
				}
			} else {
				rotation = Random().NextFloat(init.rndRotation.X, init.rndRotation.Y);
			}

			if (!inLocalSpace_) {
				position += absPosition();
			}

			// Acquiring a particle from the pool
			particlePool_[poolTop_]->init(life, position, velocity, rotation, inLocalSpace_);
			addChildNode(particlePool_[poolTop_]);
			poolTop_--;
		}
	}

	void ParticleSystem::killParticles()
	{
		for (int i = (int)children_.size() - 1; i >= 0; i--) {
			Particle* particle = static_cast<Particle*>(children_[i]);

			if (particle->isAlive()) {
				particle->life_ = 0.0f;
				particle->setEnabled(false);

				poolTop_++;
				particlePool_[poolTop_] = particle;
				removeChildNodeAt(i);
			}
		}
	}

	void ParticleSystem::setTexture(Texture* texture)
	{
		for (auto& particle : particleArray_) {
			particle->setTexture(texture);
		}
	}

	void ParticleSystem::setTexRect(const Recti& rect)
	{
		for (auto& particle : particleArray_) {
			particle->setTexRect(rect);
		}
	}

	void ParticleSystem::setAnchorPoint(float xx, float yy)
	{
		for (auto& particle : particleArray_) {
			particle->setAnchorPoint(xx, yy);
		}
	}

	void ParticleSystem::setAnchorPoint(const Vector2f& point)
	{
		for (auto& particle : particleArray_) {
			particle->setAnchorPoint(point);
		}
	}

	void ParticleSystem::setFlippedX(bool flippedX)
	{
		for (auto& particle : particleArray_) {
			particle->setFlippedX(flippedX);
		}
	}

	void ParticleSystem::setFlippedY(bool flippedY)
	{
		for (auto& particle : particleArray_) {
			particle->setFlippedY(flippedY);
		}
	}

	void ParticleSystem::setBlendingPreset(DrawableNode::BlendingPreset blendingPreset)
	{
		for (auto& particle : particleArray_) {
			particle->setBlendingPreset(blendingPreset);
		}
	}

	void ParticleSystem::setBlendingFactors(DrawableNode::BlendingFactor srcBlendingFactor, DrawableNode::BlendingFactor destBlendingFactor)
	{
		for (auto& particle : particleArray_) {
			particle->setBlendingFactors(srcBlendingFactor, destBlendingFactor);
		}
	}

	void ParticleSystem::setLayer(uint16_t layer)
	{
		for (auto& particle : particleArray_) {
			particle->setLayer(layer);
		}
	}

	void ParticleSystem::OnUpdate(float timeMult)
	{
		if (!updateEnabled_) {
			return;
		}

		// Overridden `update()` method should call `transform()` like `SceneNode::update()` does
		SceneNode::transform();

		for (int i = (int)children_.size() - 1; i >= 0; i--) {
			Particle* particle = static_cast<Particle*>(children_[i]);

			// Update the particle if it's alive
			if (particle->isAlive()) {
				// Calculating the normalized age only once per particle
				const float normalizedAge = 1.0f - particle->life_ / particle->startingLife;
				for (auto& affector : affectors_) {
					affector->affect(particle, normalizedAge);
				}

				particle->OnUpdate(timeMult);

				// Releasing the particle if it has just died
				if (!particle->isAlive()) {
					poolTop_++;
					particlePool_[poolTop_] = particle;
					removeChildNodeAt(i);
					continue;
				}

				// Transforming the particle only if it's still alive
				particle->transform();
			}
		}

		lastFrameUpdated_ = theApplication().GetFrameCount();

#if defined(WITH_TRACY)
		// TODO: Tracy
		//tracyInfoString.format("Alive: %d", numAliveParticles());
		//ZoneText(tracyInfoString.data(), tracyInfoString.length());
#endif
	}

	ParticleSystem::ParticleSystem(const ParticleSystem& other)
		: SceneNode(other), poolSize_(other.poolSize_), poolTop_(other.poolSize_ - 1),
		particlePool_(other.poolSize_),
		particleArray_(other.poolSize_),
		affectors_(4), inLocalSpace_(other.inLocalSpace_)
	{

		_type = ObjectType::ParticleSystem;

		for (unsigned int i = 0; i < other.affectors_.size(); i++) {
			const ParticleAffector& affector = *other.affectors_[i];
			switch (affector.type()) {
				case ParticleAffector::Type::COLOR:
					affectors_.push_back(new ColorAffector(static_cast<const ColorAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::SIZE:
					affectors_.push_back(new SizeAffector(static_cast<const SizeAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::ROTATION:
					affectors_.push_back(new RotationAffector(static_cast<const RotationAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::POSITION:
					affectors_.push_back(new PositionAffector(static_cast<const PositionAffector&>(affector).clone()));
					break;
				case ParticleAffector::Type::VELOCITY:
					affectors_.push_back(new VelocityAffector(static_cast<const VelocityAffector&>(affector).clone()));
					break;
			}
		}

		children_.reserve(poolSize_);
		if (poolSize_ > 0) {
			const Particle& otherParticle = *other.particlePool_.front();
			// TODO: Tracy
			/*if (otherParticle.texture() && otherParticle.texture()->name() != nullptr) {
				// When Tracy is disabled the statement body is empty and braces are needed
				ZoneText(otherParticle.texture()->name(), strnlen(otherParticle.texture()->name(), Object::MaxNameLength));
			}*/

			for (unsigned int i = 0; i < poolSize_; i++) {
				Particle* particle = new Particle(otherParticle.clone());
				particlePool_.push_back(particle);
				particleArray_.push_back(particle);
			}
		}
	}
}
