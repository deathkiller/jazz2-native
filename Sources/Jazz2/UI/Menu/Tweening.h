#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Jazz2::UI::Menu
{
	/**
		@brief Easing functions

		Collection of normalized easing curves mapping a linear progress @f$ t \in [0, 1] @f$ to an eased value.
		Used by @ref AnimatedValue and the menu transition system to shape animations in a single shared place
		instead of the formulas being scattered across sections.
	*/
	namespace Easing
	{
		/** @brief Pointer to an easing function */
		using Fn = float (*)(float);

		/** @brief Linear interpolation (no easing) */
		inline float Linear(float t) {
			return t;
		}

		/** @brief Quadratic ease-out */
		inline float OutQuad(float t) {
			float u = 1.0f - t;
			return 1.0f - u * u;
		}

		/** @brief Cubic ease-out */
		inline float OutCubic(float t) {
			float u = 1.0f - t;
			return 1.0f - u * u * u;
		}

		/** @brief Quartic ease-out */
		inline float OutQuart(float t) {
			float u = 1.0f - t;
			return 1.0f - u * u * u * u;
		}

		/** @brief Sinusoidal ease-in-out */
		inline float InOutSine(float t) {
			return -(std::cos(3.1415927f * t) - 1.0f) * 0.5f;
		}

		/** @brief Smoothstep (Hermite) ease-in-out */
		inline float SmoothStep(float t) {
			return t * t * (3.0f - 2.0f * t);
		}

		/** @brief Elastic ease-out (overshoots and settles) */
		inline float OutElastic(float t) {
			if (t <= 0.0f) {
				return 0.0f;
			}
			if (t >= 1.0f) {
				return 1.0f;
			}
			constexpr float p = 0.3f;
			return std::pow(2.0f, -10.0f * t) * std::sin((t - p * 0.25f) * (6.2831853f / p)) + 1.0f;
		}

		/** @brief Back ease-out (slight overshoot) */
		inline float OutBack(float t) {
			constexpr float c1 = 1.70158f;
			constexpr float c3 = c1 + 1.0f;
			float u = t - 1.0f;
			return 1.0f + c3 * u * u * u + c1 * u * u;
		}

		/** @brief Bounce ease-out */
		inline float OutBounce(float t) {
			constexpr float n1 = 7.5625f;
			constexpr float d1 = 2.75f;
			if (t < 1.0f / d1) {
				return n1 * t * t;
			} else if (t < 2.0f / d1) {
				t -= 1.5f / d1;
				return n1 * t * t + 0.75f;
			} else if (t < 2.5f / d1) {
				t -= 2.25f / d1;
				return n1 * t * t + 0.9375f;
			} else {
				t -= 2.625f / d1;
				return n1 * t * t + 0.984375f;
			}
		}
	}

	/**
		@brief Self-advancing animated scalar

		Lightweight 0–1 (or arbitrary range) value that eases toward a target at a fixed per-frame speed. Replaces
		the dozens of hand-rolled `if (_a < 1.0f) _a += timeMult * k;` and `_a -= timeMult * k;` counters previously
		duplicated across menu sections. Convert to the eased value implicitly, or read @ref Raw for the linear value.
	*/
	struct AnimatedValue
	{
		/** @brief Current raw (un-eased) progress */
		float Value;
		/** @brief Target the value advances toward */
		float Target;
		/** @brief Amount the raw value changes per `timeMult` unit */
		float Speed;
		/** @brief Easing function applied when reading the value */
		Easing::Fn Ease;

		AnimatedValue(float value = 0.0f, float target = 1.0f, float speed = 0.016f, Easing::Fn ease = Easing::Linear)
			: Value(value), Target(target), Speed(speed), Ease(ease) {}

		/** @brief Advances the value toward the target */
		void Update(float timeMult) {
			if (Value < Target) {
				Value = std::min(Value + Speed * timeMult, Target);
			} else if (Value > Target) {
				Value = std::max(Value - Speed * timeMult, Target);
			}
		}

		/** @brief Returns the eased value */
		float Eased() const {
			return Ease(Value);
		}

		/** @brief Returns the raw (un-eased) value */
		float Raw() const {
			return Value;
		}

		/** @brief Returns `true` if the value reached its target */
		bool IsDone() const {
			return (Value == Target);
		}

		/** @brief Restarts the animation from the specified value toward `1.0f` */
		void Restart(float from = 0.0f, float to = 1.0f) {
			Value = from;
			Target = to;
		}

		/** @brief Implicitly converts to the eased value */
		operator float() const {
			return Ease(Value);
		}
	};
}
