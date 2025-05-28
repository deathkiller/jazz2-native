#pragma once

#include <Common.h>

#include <algorithm>

#include <Containers/Tags.h>

namespace nCine
{
	inline namespace Primitives
	{
		class Color;

		using Death::Containers::NoInitT;

		/// Four-channel normalized color with 32-bit floats per component
		class Colorf
		{
		public:
			/** @{ @name Constants */

			/** @brief Number of channels */
			static constexpr std::int32_t NumChannels = 4;

			/** @} */

			/** @{ @name Predefined colors */

			static const Colorf Black;
			static const Colorf White;
			static const Colorf Red;
			static const Colorf Green;
			static const Colorf Blue;
			static const Colorf Yellow;
			static const Colorf Magenta;
			static const Colorf Cyan;

			/** @} */

			/** @brief Red */
			float R;
			/** @brief Green */
			float G;
			/** @brief Blue */
			float B;
			/** @brief Alpha */
			float A;

			/// Default constructor (transparent color)
			constexpr Colorf() noexcept
				: Colorf(1.0f, 1.0f, 1.0f, 1.0f) {}
			explicit Colorf(NoInitT) noexcept {}
			/// Three channels constructor
			constexpr Colorf(float red, float green, float blue) noexcept
				: Colorf(red, green, blue, 1.0f) {}
			/// Four channels constructor
			constexpr Colorf(float red, float green, float blue, float alpha) noexcept
				: R(red), G(green), B(blue), A(std::clamp(alpha, 0.0f, 1.0f)) {}
			/// Four channels constructor from an array
			explicit Colorf(const float channels[NumChannels]) noexcept;
			explicit Colorf(const Color& color) noexcept;

			/// Returns color array
			inline const float* Data() const {
				return &R;
			}
			/// @overload
			inline float* Data() {
				return &R;
			}

			/// Sets four color channels
			constexpr void Set(float red, float green, float blue, float alpha)
			{
				R = red;
				G = green;
				B = blue;
				A = std::clamp(alpha, 0.0f, 1.0f);
			}
			/// Sets three color channels
			void Set(float red, float green, float blue);
			/// Sets four color channels from an array
			void SetVec(const float channels[NumChannels]);
			/// Sets the alpha channel
			void SetAlpha(float alpha);

			/// Assigns operator from an unsigned char color
			Colorf& operator=(const Color& color);

			bool operator==(const Colorf& color) const;
			bool operator!=(const Colorf& color) const;

			Colorf& operator+=(const Colorf& v);
			Colorf& operator-=(const Colorf& v);

			Colorf& operator*=(const Colorf& color);
			/// Multiplies by a constant scalar
			Colorf& operator*=(float scalar);

			Colorf operator+(const Colorf& color) const;
			Colorf operator-(const Colorf& color) const;

			Colorf operator*(const Colorf& color) const;
			/// Multiplies by a constant scalar
			Colorf operator*(float scalar) const;
		};
	}
}
