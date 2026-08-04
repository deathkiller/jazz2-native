#include "ParticleAffectors.h"
#include "Particle.h"
#include "../../Main.h"

namespace nCine
{
	void ParticleAffector::affect(Particle* particle)
	{
		const float normalizedAge = 1.0f - particle->_life / particle->startingLife;
		affect(particle, normalizedAge);
	}

	void ColorAffector::addColorStep(float age, const Colorf& color)
	{
		if (_colorSteps.empty() || age > _colorSteps[_colorSteps.size() - 1].age) {
			_colorSteps.push_back(ColorStep(age, color));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void ColorAffector::affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_colorSteps.empty()) {
			return;
		}

		if (normalizedAge <= _colorSteps[0].age) {
			particle->setColor(_colorSteps[0].color);
			return;
		} else if (normalizedAge >= _colorSteps.back().age) {
			particle->setColor(_colorSteps.back().color);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _colorSteps.size() - 1; index++) {
			if (_colorSteps[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const ColorStep& prevStep = _colorSteps[index - 1];
		const ColorStep& nextStep = _colorSteps[index];

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
		if (_sizeSteps.empty() || age > _sizeSteps[_sizeSteps.size() - 1].age) {
			_sizeSteps.push_back(SizeStep(age, scaleX, scaleY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void SizeAffector::affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_sizeSteps.empty()) {
			// Applying base scale even with no steps
			particle->setScale(_baseScale);
			return;
		}

		if (normalizedAge <= _sizeSteps[0].age) {
			particle->setScale(_baseScale * _sizeSteps[0].scale);
			return;
		} else if (normalizedAge >= _sizeSteps.back().age) {
			particle->setScale(_baseScale * _sizeSteps.back().scale);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _sizeSteps.size() - 1; index++) {
			if (_sizeSteps[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const SizeStep& prevStep = _sizeSteps[index - 1];
		const SizeStep& nextStep = _sizeSteps[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const Vector2f newScale = prevStep.scale + (nextStep.scale - prevStep.scale) * factor;

		particle->setScale(_baseScale * newScale);
	}

	void RotationAffector::addRotationStep(float age, float angle)
	{
		if (_rotationSteps.empty() || age > _rotationSteps[_rotationSteps.size() - 1].age) {
			_rotationSteps.push_back(RotationStep(age, angle));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void RotationAffector::affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_rotationSteps.empty()) {
			return;
		}

		if (normalizedAge <= _rotationSteps[0].age) {
			particle->setRotation(particle->startingRotation + _rotationSteps[0].angle);
			return;
		} else if (normalizedAge >= _rotationSteps.back().age) {
			particle->setRotation(particle->startingRotation + _rotationSteps.back().angle);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _rotationSteps.size() - 1; index++) {
			if (_rotationSteps[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const RotationStep& prevStep = _rotationSteps[index - 1];
		const RotationStep& nextStep = _rotationSteps[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const float newAngle = prevStep.angle + (nextStep.angle - prevStep.angle) * factor;

		particle->setRotation(particle->startingRotation + newAngle);
	}

	void PositionAffector::addPositionStep(float age, float posX, float posY)
	{
		if (_positionSteps.empty() || age > _positionSteps[_positionSteps.size() - 1].age) {
			_positionSteps.push_back(PositionStep(age, posX, posY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void PositionAffector::affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_positionSteps.empty()) {
			return;
		}

		if (normalizedAge <= _positionSteps[0].age) {
			particle->move(_positionSteps[0].position);
			return;
		} else if (normalizedAge >= _positionSteps.back().age) {
			particle->move(_positionSteps.back().position);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _positionSteps.size() - 1; index++) {
			if (_positionSteps[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const PositionStep& prevStep = _positionSteps[index - 1];
		const PositionStep& nextStep = _positionSteps[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const Vector2f newPosition = prevStep.position + (nextStep.position - prevStep.position) * factor;

		particle->move(newPosition);
	}

	void VelocityAffector::addVelocityStep(float age, float velX, float velY)
	{
		if (_velocitySteps.empty() || age > _velocitySteps[_velocitySteps.size() - 1].age) {
			_velocitySteps.push_back(VelocityStep(age, velX, velY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void VelocityAffector::affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_velocitySteps.empty()) {
			return;
		}

		if (normalizedAge <= _velocitySteps[0].age) {
			particle->_velocity += _velocitySteps[0].velocity;
			return;
		} else if (normalizedAge >= _velocitySteps.back().age) {
			particle->_velocity += _velocitySteps.back().velocity;
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _velocitySteps.size() - 1; index++) {
			if (_velocitySteps[index].age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const VelocityStep& prevStep = _velocitySteps[index - 1];
		const VelocityStep& nextStep = _velocitySteps[index];

		const float factor = (normalizedAge - prevStep.age) / (nextStep.age - prevStep.age);
		const Vector2f newVelocity = prevStep.velocity + (nextStep.velocity - prevStep.velocity) * factor;

		particle->_velocity += newVelocity;
	}
}
