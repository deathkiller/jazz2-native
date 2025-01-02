#pragma once

#include "../Primitives/Vector2.h"
#include "../Primitives/Colorf.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class Particle;

	const unsigned int StepsInitialSize = 4;

	/// Base class for particle affectors
	/*! Affectors modify particle properties depending on their remaining life */
	class ParticleAffector
	{
	public:
		enum class Type
		{
			COLOR,
			SIZE,
			ROTATION,
			POSITION,
			VELOCITY
		};

		ParticleAffector(Type type)
			: type_(type) {}
		virtual ~ParticleAffector() {}

		/// Affects a property of the specified particle
		void affect(Particle* particle);
		/// Affects a property of the specified particle, without calculating the normalized age
		virtual void affect(Particle* particle, float normalizedAge) = 0;

		/// Returns the object type (RTTI)
		inline Type type() const {
			return type_;
		}

	protected:
		/// Affector type
		Type type_;

		/// Protected default copy constructor used to clone objects
		ParticleAffector(const ParticleAffector& other) = default;
	};

	/// Particle color affector
	class ColorAffector : public ParticleAffector
	{
	public:
		struct ColorStep
		{
			float age;
			Colorf color;

			ColorStep()
				: age(0.0f) {}
			ColorStep(float newAge, const Colorf& newColor)
				: age(newAge), color(newColor) {}
		};

		ColorAffector()
			: ParticleAffector(Type::COLOR), colorSteps_(StepsInitialSize) {}

		/// Default move constructor
		ColorAffector(ColorAffector&&) = default;
		/// Default move assignment operator
		ColorAffector& operator=(ColorAffector&&) = default;

		/// Returns a copy of this object
		inline ColorAffector clone() const {
			return ColorAffector(*this);
		}

		/// Affects the color of the specified particle
		void affect(Particle* particle, float normalizedAge) override;
		void addColorStep(float age, const Colorf& color);

		inline SmallVectorImpl<ColorStep>& steps() {
			return colorSteps_;
		}
		inline const SmallVectorImpl<ColorStep>& steps() const {
			return colorSteps_;
		}

	protected:
		/// Protected default copy constructor used to clone objects
		ColorAffector(const ColorAffector& other) = default;

	private:
		SmallVector<ColorStep, 0> colorSteps_;
	};

	/// Particle size affector
	class SizeAffector : public ParticleAffector
	{
	public:
		struct SizeStep
		{
			float age;
			Vector2f scale;

			SizeStep()
				: age(0.0f), scale(1.0f, 1.0f) {}
			SizeStep(float newAge, float newScale)
				: age(newAge), scale(newScale, newScale) {}
			SizeStep(float newAge, float newScaleX, float newScaleY)
				: age(newAge), scale(newScaleX, newScaleY) {}
			SizeStep(float newAge, Vector2f newScale)
				: age(newAge), scale(newScale) {}
		};

		/// Constructs a size affector with a default base scale factor
		SizeAffector()
			: SizeAffector(1.0f, 1.0f) {}
		/// Constructs a size affector with a base scale factor as a reference
		explicit SizeAffector(float baseScale)
			: SizeAffector(baseScale, baseScale) {}
		/// Constructs a size affector with a horizontal and a vertical base scale factor as a reference
		SizeAffector(float baseScaleX, float baseScaleY)
			: ParticleAffector(Type::SIZE), sizeSteps_(StepsInitialSize), baseScale_(baseScaleX, baseScaleY) {}
		/// Constructs a size affector with a vector base scale factor as a reference
		explicit SizeAffector(Vector2f baseScale)
			: SizeAffector(baseScale.X, baseScale.Y) {}

		/// Default move constructor
		SizeAffector(SizeAffector&&) = default;
		/// Default move assignment operator
		SizeAffector& operator=(SizeAffector&&) = default;

		/// Returns a copy of this object
		inline SizeAffector clone() const {
			return SizeAffector(*this);
		}

		/// Affects the size of the specified particle
		void affect(Particle* particle, float normalizedAge) override;
		inline void addSizeStep(float age, float scale) {
			addSizeStep(age, scale, scale);
		}
		void addSizeStep(float age, float scaleX, float scaleY);
		inline void addSizeStep(float age, Vector2f scale) {
			addSizeStep(age, scale.X, scale.Y);
		}

		inline SmallVectorImpl<SizeStep>& steps() {
			return sizeSteps_;
		}
		inline const SmallVectorImpl<SizeStep>& steps() const {
			return sizeSteps_;
		}

		inline float baseScaleX() const {
			return baseScale_.X;
		}
		inline void setBaseScaleX(float baseScaleX) {
			baseScale_.X = baseScaleX;
		}
		inline float baseScaleY() const {
			return baseScale_.Y;
		}
		inline void setBaseScaleY(float baseScaleY) {
			baseScale_.Y = baseScaleY;
		}

		inline Vector2f baseScale() const {
			return baseScale_;
		}
		inline void setBaseScale(float baseScale) {
			baseScale_.Set(baseScale, baseScale);
		}
		inline void setBaseScale(Vector2f baseScale) {
			baseScale_ = baseScale;
		}

	protected:
		/// Protected default copy constructor used to clone objects
		SizeAffector(const SizeAffector& other) = default;

	private:
		SmallVector<SizeStep, 0> sizeSteps_;
		Vector2f baseScale_;
	};

	/// Particle rotation affector
	class RotationAffector : public ParticleAffector
	{
	public:
		struct RotationStep
		{
			float age;
			float angle;

			RotationStep()
				: age(0.0f), angle(0.0f) {}
			RotationStep(float newAge, float newAngle)
				: age(newAge), angle(newAngle) {}
		};

		RotationAffector()
			: ParticleAffector(Type::ROTATION), rotationSteps_(StepsInitialSize) {}

		/// Default move constructor
		RotationAffector(RotationAffector&&) = default;
		/// Default move assignment operator
		RotationAffector& operator=(RotationAffector&&) = default;

		/// Returns a copy of this object
		inline RotationAffector clone() const {
			return RotationAffector(*this);
		}

		/// Affects the rotation of the specified particle
		void affect(Particle* particle, float normalizedAge) override;
		void addRotationStep(float age, float angle);

		inline SmallVectorImpl<RotationStep>& steps() {
			return rotationSteps_;
		}
		inline const SmallVectorImpl<RotationStep>& steps() const {
			return rotationSteps_;
		}

	protected:
		/// Protected default copy constructor used to clone objects
		RotationAffector(const RotationAffector& other) = default;

	private:
		SmallVector<RotationStep, 0> rotationSteps_;
	};

	/// Particle position affector
	class PositionAffector : public ParticleAffector
	{
	public:
		struct PositionStep
		{
			float age;
			Vector2f position;

			PositionStep()
				: age(0.0f), position(0.0f, 0.0f) {}
			PositionStep(float newAge, float newPositionX, float newPositionY)
				: age(newAge), position(newPositionX, newPositionY) {}
		};

		PositionAffector()
			: ParticleAffector(Type::POSITION), positionSteps_(StepsInitialSize) {}

		/// Default move constructor
		PositionAffector(PositionAffector&&) = default;
		/// Default move assignment operator
		PositionAffector& operator=(PositionAffector&&) = default;

		/// Returns a copy of this object
		inline PositionAffector clone() const {
			return PositionAffector(*this);
		}

		/// Affects the position of the specified particle
		void affect(Particle* particle, float normalizedAge) override;
		void addPositionStep(float age, float posX, float posY);
		inline void addPositionStep(float age, Vector2f position) {
			addPositionStep(age, position.X, position.Y);
		}

		inline SmallVectorImpl<PositionStep>& steps() {
			return positionSteps_;
		}
		inline const SmallVectorImpl<PositionStep>& steps() const {
			return positionSteps_;
		}

	protected:
		/// Protected default copy constructor used to clone objects
		PositionAffector(const PositionAffector& other) = default;

	private:
		SmallVector<PositionStep, 0> positionSteps_;
	};

	/// Particle velocity affector
	class VelocityAffector : public ParticleAffector
	{
	public:
		struct VelocityStep
		{
			float age;
			Vector2f velocity;

			VelocityStep()
				: age(0.0f), velocity(0.0f, 0.0f) {}
			VelocityStep(float newAge, float newVelocityX, float newVelocityY)
				: age(newAge), velocity(newVelocityX, newVelocityY) {}
		};

		VelocityAffector()
			: ParticleAffector(Type::VELOCITY), velocitySteps_(StepsInitialSize) {}

		/// Default move constructor
		VelocityAffector(VelocityAffector&&) = default;
		/// Default move assignment operator
		VelocityAffector& operator=(VelocityAffector&&) = default;

		/// Returns a copy of this object
		inline VelocityAffector clone() const {
			return VelocityAffector(*this);
		}

		/// Affects the velocity of the specified particle
		void affect(Particle* particle, float normalizedAge) override;
		void addVelocityStep(float age, float velX, float velY);
		inline void addVelocityStep(float age, Vector2f velocity) {
			addVelocityStep(age, velocity.X, velocity.Y);
		}

		inline SmallVectorImpl<VelocityStep>& steps() {
			return velocitySteps_;
		}
		inline const SmallVectorImpl<VelocityStep>& steps() const {
			return velocitySteps_;
		}

	protected:
		/// Protected default copy constructor used to clone objects
		VelocityAffector(const VelocityAffector& other) = default;

	private:
		SmallVector<VelocityStep, 0> velocitySteps_;
	};

}
