#include "ParticleAffectors.h"
#include "Particle.h"
#include "../../Main.h"

namespace nCine
{
	void ParticleAffector::Affect(Particle* particle)
	{
		const float normalizedAge = 1.0f - particle->_life / particle->startingLife;
		Affect(particle, normalizedAge);
	}

	void ColorAffector::AddColorStep(float age, const Colorf& color)
	{
		if (_colorSteps.empty() || age > _colorSteps[_colorSteps.size() - 1].Age) {
			_colorSteps.push_back(ColorStep(age, color));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void ColorAffector::Affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_colorSteps.empty()) {
			return;
		}

		if (normalizedAge <= _colorSteps[0].Age) {
			particle->SetColor(_colorSteps[0].Color);
			return;
		} else if (normalizedAge >= _colorSteps.back().Age) {
			particle->SetColor(_colorSteps.back().Color);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _colorSteps.size() - 1; index++) {
			if (_colorSteps[index].Age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const ColorStep& prevStep = _colorSteps[index - 1];
		const ColorStep& nextStep = _colorSteps[index];

		const float factor = (normalizedAge - prevStep.Age) / (nextStep.Age - prevStep.Age);
		const float red = prevStep.Color.R + (nextStep.Color.R - prevStep.Color.R) * factor;
		const float green = prevStep.Color.G + (nextStep.Color.G - prevStep.Color.G) * factor;
		const float blue = prevStep.Color.B + (nextStep.Color.B - prevStep.Color.B) * factor;
		const float alpha = prevStep.Color.A + (nextStep.Color.A - prevStep.Color.A) * factor;
		const Colorf color(red, green, blue, alpha);

		particle->SetColor(color);
	}

	void SizeAffector::AddSizeStep(float age, float scaleX, float scaleY)
	{
		if (_sizeSteps.empty() || age > _sizeSteps[_sizeSteps.size() - 1].Age) {
			_sizeSteps.push_back(SizeStep(age, scaleX, scaleY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void SizeAffector::Affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_sizeSteps.empty()) {
			// Applying base scale even with no steps
			particle->SetScale(_baseScale);
			return;
		}

		if (normalizedAge <= _sizeSteps[0].Age) {
			particle->SetScale(_baseScale * _sizeSteps[0].Scale);
			return;
		} else if (normalizedAge >= _sizeSteps.back().Age) {
			particle->SetScale(_baseScale * _sizeSteps.back().Scale);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _sizeSteps.size() - 1; index++) {
			if (_sizeSteps[index].Age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const SizeStep& prevStep = _sizeSteps[index - 1];
		const SizeStep& nextStep = _sizeSteps[index];

		const float factor = (normalizedAge - prevStep.Age) / (nextStep.Age - prevStep.Age);
		const Vector2f newScale = prevStep.Scale + (nextStep.Scale - prevStep.Scale) * factor;

		particle->SetScale(_baseScale * newScale);
	}

	void RotationAffector::AddRotationStep(float age, float angle)
	{
		if (_rotationSteps.empty() || age > _rotationSteps[_rotationSteps.size() - 1].Age) {
			_rotationSteps.push_back(RotationStep(age, angle));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void RotationAffector::Affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_rotationSteps.empty()) {
			return;
		}

		if (normalizedAge <= _rotationSteps[0].Age) {
			particle->SetRotation(particle->startingRotation + _rotationSteps[0].Angle);
			return;
		} else if (normalizedAge >= _rotationSteps.back().Age) {
			particle->SetRotation(particle->startingRotation + _rotationSteps.back().Angle);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _rotationSteps.size() - 1; index++) {
			if (_rotationSteps[index].Age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const RotationStep& prevStep = _rotationSteps[index - 1];
		const RotationStep& nextStep = _rotationSteps[index];

		const float factor = (normalizedAge - prevStep.Age) / (nextStep.Age - prevStep.Age);
		const float newAngle = prevStep.Angle + (nextStep.Angle - prevStep.Angle) * factor;

		particle->SetRotation(particle->startingRotation + newAngle);
	}

	void PositionAffector::AddPositionStep(float age, float posX, float posY)
	{
		if (_positionSteps.empty() || age > _positionSteps[_positionSteps.size() - 1].Age) {
			_positionSteps.push_back(PositionStep(age, posX, posY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void PositionAffector::Affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_positionSteps.empty()) {
			return;
		}

		if (normalizedAge <= _positionSteps[0].Age) {
			particle->Move(_positionSteps[0].Position);
			return;
		} else if (normalizedAge >= _positionSteps.back().Age) {
			particle->Move(_positionSteps.back().Position);
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _positionSteps.size() - 1; index++) {
			if (_positionSteps[index].Age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const PositionStep& prevStep = _positionSteps[index - 1];
		const PositionStep& nextStep = _positionSteps[index];

		const float factor = (normalizedAge - prevStep.Age) / (nextStep.Age - prevStep.Age);
		const Vector2f newPosition = prevStep.Position + (nextStep.Position - prevStep.Position) * factor;

		particle->Move(newPosition);
	}

	void VelocityAffector::AddVelocityStep(float age, float velX, float velY)
	{
		if (_velocitySteps.empty() || age > _velocitySteps[_velocitySteps.size() - 1].Age) {
			_velocitySteps.push_back(VelocityStep(age, velX, velY));
		} else {
			LOGW("Out of order step not added");
		}
	}

	void VelocityAffector::Affect(Particle* particle, float normalizedAge)
	{
		DEATH_ASSERT(particle);

		// Zero steps in the affector
		if (_velocitySteps.empty()) {
			return;
		}

		if (normalizedAge <= _velocitySteps[0].Age) {
			particle->_velocity += _velocitySteps[0].Velocity;
			return;
		} else if (normalizedAge >= _velocitySteps.back().Age) {
			particle->_velocity += _velocitySteps.back().Velocity;
			return;
		}

		unsigned int index = 0;
		for (index = 0; index < _velocitySteps.size() - 1; index++) {
			if (_velocitySteps[index].Age > normalizedAge) {
				break;
			}
		}

		FATAL_ASSERT(index > 0);
		const VelocityStep& prevStep = _velocitySteps[index - 1];
		const VelocityStep& nextStep = _velocitySteps[index];

		const float factor = (normalizedAge - prevStep.Age) / (nextStep.Age - prevStep.Age);
		const Vector2f newVelocity = prevStep.Velocity + (nextStep.Velocity - prevStep.Velocity) * factor;

		particle->_velocity += newVelocity;
	}
}
