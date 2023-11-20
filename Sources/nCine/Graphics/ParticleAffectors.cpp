#include "ParticleAffectors.h"
#include "Particle.h"
#include "../../Common.h"

namespace nCine
{
	void ParticleAffector::affect(Particle* particle)
	{
		const float normalizedAge = 1.0f - particle->life_ / particle->startingLife;
		affect(particle, normalizedAge);
	}

	void ColorAffector::addColorStep(float age, const Colorf& color)
	{
		if (colorSteps_.empty() || age > colorSteps_[colorSteps_.size() - 1].age) {
			colorSteps_.push_back(ColorStep(age, color));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void ColorAffector::affect(Particle* particle, float normalizedAge)
	{
		ASSERT(particle);

		// Zero steps in the affector
		if (colorSteps_.empty()) {
			return;
		}

		if (normalizedAge <= colorSteps_[0].age) {
			particle->setColor(colorSteps_[0].color);
			return;
		} else if (normalizedAge >= colorSteps_.back().age) {
			particle->setColor(colorSteps_.back().color);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < colorSteps_.size() - 1; index++) {
			if (colorSteps_[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const ColorStep& prevStep = colorSteps_[index - 1];
		const ColorStep& nextStep = colorSteps_[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const float red = prevStep.color.R + (nextStep.color.R - prevStep.color.R) * factor;
		const float green = prevStep.color.G + (nextStep.color.G - prevStep.color.G) * factor;
		const float blue = prevStep.color.B + (nextStep.color.B - prevStep.color.B) * factor;
		const float alpha = prevStep.color.A + (nextStep.color.A - prevStep.color.A) * factor;
		const Colorf color(red, green, blue, alpha);

		particle->setColor(color);
	}

	void SizeAffector::addSizeStep(float age, float scaleX, float scaleY)
	{
		if (sizeSteps_.empty() || age > sizeSteps_[sizeSteps_.size() - 1].age) {
			sizeSteps_.push_back(SizeStep(age, scaleX, scaleY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void SizeAffector::affect(Particle* particle, float normalizedAge)
	{
		ASSERT(particle);

		// Zero steps in the affector
		if (sizeSteps_.empty()) {
			// Applying base scale even with no steps
			particle->setScale(baseScale_);
			return;
		}

		if (normalizedAge <= sizeSteps_[0].age) {
			particle->setScale(baseScale_ * sizeSteps_[0].scale);
			return;
		} else if (normalizedAge >= sizeSteps_.back().age) {
			particle->setScale(baseScale_ * sizeSteps_.back().scale);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < sizeSteps_.size() - 1; index++) {
			if (sizeSteps_[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const SizeStep& prevStep = sizeSteps_[index - 1];
		const SizeStep& nextStep = sizeSteps_[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const Vector2f newScale = prevStep.scale + (nextStep.scale - prevStep.scale) * factor;

		particle->setScale(baseScale_ * newScale);
	}

	void RotationAffector::addRotationStep(float age, float angle)
	{
		if (rotationSteps_.empty() || age > rotationSteps_[rotationSteps_.size() - 1].age) {
			rotationSteps_.push_back(RotationStep(age, angle));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void RotationAffector::affect(Particle* particle, float normalizedAge)
	{
		ASSERT(particle);

		// Zero steps in the affector
		if (rotationSteps_.empty()) {
			return;
		}

		if (normalizedAge <= rotationSteps_[0].age) {
			particle->setRotation(particle->startingRotation + rotationSteps_[0].angle);
			return;
		} else if (normalizedAge >= rotationSteps_.back().age) {
			particle->setRotation(particle->startingRotation + rotationSteps_.back().angle);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < rotationSteps_.size() - 1; index++) {
			if (rotationSteps_[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const RotationStep& prevStep = rotationSteps_[index - 1];
		const RotationStep& nextStep = rotationSteps_[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const float newAngle = prevStep.angle + (nextStep.angle - prevStep.angle) * factor;

		particle->setRotation(particle->startingRotation + newAngle);
	}

	void PositionAffector::addPositionStep(float age, float posX, float posY)
	{
		if (positionSteps_.empty() || age > positionSteps_[positionSteps_.size() - 1].age) {
			positionSteps_.push_back(PositionStep(age, posX, posY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void PositionAffector::affect(Particle* particle, float normalizedAge)
	{
		ASSERT(particle);

		// Zero steps in the affector
		if (positionSteps_.empty()) {
			return;
		}

		if (normalizedAge <= positionSteps_[0].age) {
			particle->move(positionSteps_[0].position);
			return;
		} else if (normalizedAge >= positionSteps_.back().age) {
			particle->move(positionSteps_.back().position);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < positionSteps_.size() - 1; index++) {
			if (positionSteps_[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const PositionStep& prevStep = positionSteps_[index - 1];
		const PositionStep& nextStep = positionSteps_[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const Vector2f newPosition = prevStep.position + (nextStep.position - prevStep.position) * factor;

		particle->move(newPosition);
	}

	void VelocityAffector::addVelocityStep(float age, float velX, float velY)
	{
		if (velocitySteps_.empty() || age > velocitySteps_[velocitySteps_.size() - 1].age) {
			velocitySteps_.push_back(VelocityStep(age, velX, velY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void VelocityAffector::affect(Particle* particle, float normalizedAge)
	{
		ASSERT(particle);

		// Zero steps in the affector
		if (velocitySteps_.empty()) {
			return;
		}

		if (normalizedAge <= velocitySteps_[0].age) {
			particle->velocity_ += velocitySteps_[0].velocity;
			return;
		} else if (normalizedAge >= velocitySteps_.back().age) {
			particle->velocity_ += velocitySteps_.back().velocity;
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < velocitySteps_.size() - 1; index++) {
			if (velocitySteps_[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const VelocityStep& prevStep = velocitySteps_[index - 1];
		const VelocityStep& nextStep = velocitySteps_[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const Vector2f newVelocity = prevStep.velocity + (nextStep.velocity - prevStep.velocity) * factor;

		particle->velocity_ += newVelocity;
	}
}
