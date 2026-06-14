#pragma once

#include "../Primitives/Vector2.h"
#include "../Primitives/Colorf.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class Particle;

	/** @brief Initial capacity reserved for the step array of each affector */
	const unsigned int StepsInitialSize = 4;

	/**
		@brief Base class for all particle affectors
		
		An affector mutates one property of a particle (color, size, rotation, position or velocity) each
		frame, interpolating between user-defined steps according to the particle's normalized age. A
		@ref ParticleSystem applies every attached affector to each alive particle before it is drawn.
	*/
	class ParticleAffector
	{
	public:
		/**
		 * @brief Affector type
		 *
		 * Identifies which particle property the affector mutates and selects the matching subclass when cloning.
		 */
		enum class Type
		{
			COLOR,		/**< Affects the particle color */
			SIZE,		/**< Affects the particle scale */
			ROTATION,	/**< Affects the particle rotation */
			POSITION,	/**< Affects the particle position */
			VELOCITY	/**< Affects the particle velocity */
		};

		ParticleAffector(Type type)
			: type_(type) {}
		virtual ~ParticleAffector() {}

		/**
		 * @brief Affects a property of the specified particle
		 *
		 * Computes the particle's normalized age from its remaining life and forwards to the overloaded method.
		 */
		void affect(Particle* particle);
		/**
		 * @brief Affects a property of the specified particle using a precomputed normalized age
		 *
		 * @param particle      Particle to mutate
		 * @param normalizedAge Particle age mapped to the `[0, 1]` range, where `0` is birth and `1` is death
		 */
		virtual void affect(Particle* particle, float normalizedAge) = 0;

		/** @brief Returns the affector type */
		inline Type type() const {
			return type_;
		}

	protected:
		/** @brief Affector type */
		Type type_;

		/** @brief Protected default copy constructor used to clone objects */
		ParticleAffector(const ParticleAffector& other) = default;
	};

	/**
		@brief Affector that animates the color of a particle
		
		Linearly interpolates the particle color between consecutive @ref ColorStep entries ordered by age.
	*/
	class ColorAffector : public ParticleAffector
	{
	public:
		/** @brief A color keyed to a normalized particle age */
		struct ColorStep
		{
			/** @brief Normalized particle age at which the color applies */
			float age;
			/** @brief Color reached at this age */
			Colorf color;

			ColorStep()
				: age(0.0f) {}
			ColorStep(float newAge, const Colorf& newColor)
				: age(newAge), color(newColor) {}
		};

		ColorAffector()
			: ParticleAffector(Type::COLOR), colorSteps_(StepsInitialSize) {}

		/** @brief Default move constructor */
		ColorAffector(ColorAffector&&) = default;
		/** @brief Default move assignment operator */
		ColorAffector& operator=(ColorAffector&&) = default;

		/** @brief Returns a copy of this object */
		inline ColorAffector clone() const {
			return ColorAffector(*this);
		}

		void affect(Particle* particle, float normalizedAge) override;
		/**
		 * @brief Appends a color step at the specified age
		 *
		 * Steps must be added in ascending age order; an out-of-order step is rejected with a warning.
		 */
		void addColorStep(float age, const Colorf& color);

		/** @brief Returns the array of color steps */
		inline SmallVectorImpl<ColorStep>& steps() {
			return colorSteps_;
		}
		/** @brief Returns the array of color steps (read-only) */
		inline const SmallVectorImpl<ColorStep>& steps() const {
			return colorSteps_;
		}

	protected:
		/** @brief Protected default copy constructor used to clone objects */
		ColorAffector(const ColorAffector& other) = default;

	private:
		SmallVector<ColorStep, 0> colorSteps_;
	};

	/**
		@brief Affector that animates the scale of a particle
		
		Linearly interpolates the scale between consecutive @ref SizeStep entries; the interpolated value is
		multiplied by a constant base scale applied to every particle.
	*/
	class SizeAffector : public ParticleAffector
	{
	public:
		/** @brief A scale factor keyed to a normalized particle age */
		struct SizeStep
		{
			/** @brief Normalized particle age at which the scale applies */
			float age;
			/** @brief Scale factor reached at this age */
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

		/** @brief Constructs a size affector with a unit base scale factor */
		SizeAffector()
			: SizeAffector(1.0f, 1.0f) {}
		/** @brief Constructs a size affector with a uniform base scale factor */
		explicit SizeAffector(float baseScale)
			: SizeAffector(baseScale, baseScale) {}
		/** @brief Constructs a size affector with a horizontal and a vertical base scale factor */
		SizeAffector(float baseScaleX, float baseScaleY)
			: ParticleAffector(Type::SIZE), sizeSteps_(StepsInitialSize), baseScale_(baseScaleX, baseScaleY) {}
		/** @brief Constructs a size affector with a vector base scale factor */
		explicit SizeAffector(Vector2f baseScale)
			: SizeAffector(baseScale.X, baseScale.Y) {}

		/** @brief Default move constructor */
		SizeAffector(SizeAffector&&) = default;
		/** @brief Default move assignment operator */
		SizeAffector& operator=(SizeAffector&&) = default;

		/** @brief Returns a copy of this object */
		inline SizeAffector clone() const {
			return SizeAffector(*this);
		}

		void affect(Particle* particle, float normalizedAge) override;
		/** @brief Appends a uniform scale step at the specified age */
		inline void addSizeStep(float age, float scale) {
			addSizeStep(age, scale, scale);
		}
		/**
		 * @brief Appends a non-uniform scale step at the specified age
		 *
		 * Steps must be added in ascending age order; an out-of-order step is rejected with a warning.
		 */
		void addSizeStep(float age, float scaleX, float scaleY);
		/** @brief Appends a vector scale step at the specified age */
		inline void addSizeStep(float age, Vector2f scale) {
			addSizeStep(age, scale.X, scale.Y);
		}

		/** @brief Returns the array of size steps */
		inline SmallVectorImpl<SizeStep>& steps() {
			return sizeSteps_;
		}
		/** @brief Returns the array of size steps (read-only) */
		inline const SmallVectorImpl<SizeStep>& steps() const {
			return sizeSteps_;
		}

		/** @brief Returns the horizontal base scale factor */
		inline float baseScaleX() const {
			return baseScale_.X;
		}
		/** @brief Sets the horizontal base scale factor */
		inline void setBaseScaleX(float baseScaleX) {
			baseScale_.X = baseScaleX;
		}
		/** @brief Returns the vertical base scale factor */
		inline float baseScaleY() const {
			return baseScale_.Y;
		}
		/** @brief Sets the vertical base scale factor */
		inline void setBaseScaleY(float baseScaleY) {
			baseScale_.Y = baseScaleY;
		}

		/** @brief Returns the base scale factor */
		inline Vector2f baseScale() const {
			return baseScale_;
		}
		/** @brief Sets a uniform base scale factor */
		inline void setBaseScale(float baseScale) {
			baseScale_.Set(baseScale, baseScale);
		}
		/** @brief Sets a vector base scale factor */
		inline void setBaseScale(Vector2f baseScale) {
			baseScale_ = baseScale;
		}

	protected:
		/** @brief Protected default copy constructor used to clone objects */
		SizeAffector(const SizeAffector& other) = default;

	private:
		SmallVector<SizeStep, 0> sizeSteps_;
		Vector2f baseScale_;
	};

	/**
		@brief Affector that animates the rotation of a particle
		
		Linearly interpolates an angle offset between consecutive @ref RotationStep entries; the offset is added
		to the particle's starting rotation.
	*/
	class RotationAffector : public ParticleAffector
	{
	public:
		/** @brief An angle offset keyed to a normalized particle age */
		struct RotationStep
		{
			/** @brief Normalized particle age at which the angle applies */
			float age;
			/** @brief Angle offset in degrees reached at this age */
			float angle;

			RotationStep()
				: age(0.0f), angle(0.0f) {}
			RotationStep(float newAge, float newAngle)
				: age(newAge), angle(newAngle) {}
		};

		RotationAffector()
			: ParticleAffector(Type::ROTATION), rotationSteps_(StepsInitialSize) {}

		/** @brief Default move constructor */
		RotationAffector(RotationAffector&&) = default;
		/** @brief Default move assignment operator */
		RotationAffector& operator=(RotationAffector&&) = default;

		/** @brief Returns a copy of this object */
		inline RotationAffector clone() const {
			return RotationAffector(*this);
		}

		void affect(Particle* particle, float normalizedAge) override;
		/**
		 * @brief Appends a rotation step at the specified age
		 *
		 * Steps must be added in ascending age order; an out-of-order step is rejected with a warning.
		 */
		void addRotationStep(float age, float angle);

		/** @brief Returns the array of rotation steps */
		inline SmallVectorImpl<RotationStep>& steps() {
			return rotationSteps_;
		}
		/** @brief Returns the array of rotation steps (read-only) */
		inline const SmallVectorImpl<RotationStep>& steps() const {
			return rotationSteps_;
		}

	protected:
		/** @brief Protected default copy constructor used to clone objects */
		RotationAffector(const RotationAffector& other) = default;

	private:
		SmallVector<RotationStep, 0> rotationSteps_;
	};

	/**
		@brief Affector that animates the position of a particle
		
		Linearly interpolates a position offset between consecutive @ref PositionStep entries and moves the
		particle by the resulting amount.
	*/
	class PositionAffector : public ParticleAffector
	{
	public:
		/** @brief A position offset keyed to a normalized particle age */
		struct PositionStep
		{
			/** @brief Normalized particle age at which the offset applies */
			float age;
			/** @brief Position offset reached at this age */
			Vector2f position;

			PositionStep()
				: age(0.0f), position(0.0f, 0.0f) {}
			PositionStep(float newAge, float newPositionX, float newPositionY)
				: age(newAge), position(newPositionX, newPositionY) {}
		};

		PositionAffector()
			: ParticleAffector(Type::POSITION), positionSteps_(StepsInitialSize) {}

		/** @brief Default move constructor */
		PositionAffector(PositionAffector&&) = default;
		/** @brief Default move assignment operator */
		PositionAffector& operator=(PositionAffector&&) = default;

		/** @brief Returns a copy of this object */
		inline PositionAffector clone() const {
			return PositionAffector(*this);
		}

		void affect(Particle* particle, float normalizedAge) override;
		/**
		 * @brief Appends a position step at the specified age
		 *
		 * Steps must be added in ascending age order; an out-of-order step is rejected with a warning.
		 */
		void addPositionStep(float age, float posX, float posY);
		/** @brief Appends a vector position step at the specified age */
		inline void addPositionStep(float age, Vector2f position) {
			addPositionStep(age, position.X, position.Y);
		}

		/** @brief Returns the array of position steps */
		inline SmallVectorImpl<PositionStep>& steps() {
			return positionSteps_;
		}
		/** @brief Returns the array of position steps (read-only) */
		inline const SmallVectorImpl<PositionStep>& steps() const {
			return positionSteps_;
		}

	protected:
		/** @brief Protected default copy constructor used to clone objects */
		PositionAffector(const PositionAffector& other) = default;

	private:
		SmallVector<PositionStep, 0> positionSteps_;
	};

	/**
		@brief Affector that animates the velocity of a particle
		
		Linearly interpolates a velocity offset between consecutive @ref VelocityStep entries and adds the
		resulting amount to the particle's velocity each frame.
	*/
	class VelocityAffector : public ParticleAffector
	{
	public:
		/** @brief A velocity offset keyed to a normalized particle age */
		struct VelocityStep
		{
			/** @brief Normalized particle age at which the offset applies */
			float age;
			/** @brief Velocity offset reached at this age */
			Vector2f velocity;

			VelocityStep()
				: age(0.0f), velocity(0.0f, 0.0f) {}
			VelocityStep(float newAge, float newVelocityX, float newVelocityY)
				: age(newAge), velocity(newVelocityX, newVelocityY) {}
		};

		VelocityAffector()
			: ParticleAffector(Type::VELOCITY), velocitySteps_(StepsInitialSize) {}

		/** @brief Default move constructor */
		VelocityAffector(VelocityAffector&&) = default;
		/** @brief Default move assignment operator */
		VelocityAffector& operator=(VelocityAffector&&) = default;

		/** @brief Returns a copy of this object */
		inline VelocityAffector clone() const {
			return VelocityAffector(*this);
		}

		void affect(Particle* particle, float normalizedAge) override;
		/**
		 * @brief Appends a velocity step at the specified age
		 *
		 * Steps must be added in ascending age order; an out-of-order step is rejected with a warning.
		 */
		void addVelocityStep(float age, float velX, float velY);
		/** @brief Appends a vector velocity step at the specified age */
		inline void addVelocityStep(float age, Vector2f velocity) {
			addVelocityStep(age, velocity.X, velocity.Y);
		}

		/** @brief Returns the array of velocity steps */
		inline SmallVectorImpl<VelocityStep>& steps() {
			return velocitySteps_;
		}
		/** @brief Returns the array of velocity steps (read-only) */
		inline const SmallVectorImpl<VelocityStep>& steps() const {
			return velocitySteps_;
		}

	protected:
		/** @brief Protected default copy constructor used to clone objects */
		VelocityAffector(const VelocityAffector& other) = default;

	private:
		SmallVector<VelocityStep, 0> velocitySteps_;
	};

}
