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

		inline namespace Literals
		{
			// According to https://wg21.link/CWG2521, space between "" and literal name is deprecated because _Uppercase
			// or _double names could be treated as reserved depending on whether the space was present or not,
			// and whitespace is not load-bearing in any other contexts. Clang 17+ adds an off-by-default warning for this;
			// GCC 4.8 however *requires* the space there, so until GCC 4.8 support is dropped, we suppress this warning
			// instead of removing the space. GCC 15 now has the same warning but it's enabled by default on -std=c++23.
			#if (defined(DEATH_TARGET_CLANG) && __clang_major__ >= 17) || (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 15)
			#	pragma GCC diagnostic push
			#	pragma GCC diagnostic ignored "-Wdeprecated-literal-operator"
			#endif

			/** @relatesalso nCine::Primitives::Colorf
				@brief Float RGB color literal

				See @ref Colorf for more information.
			*/
			constexpr Colorf operator"" _rgbf(unsigned long long value) {
				return { ((value >> 16) & 0xff) / 255.0f, ((value >> 8) & 0xff) / 255.0f, ((value) & 0xff) / 255.0f };
			}

			/** @relatesalso nCine::Primitives::Colorf
				@brief Float RGBA color literal

				See @ref Colorf for more information.
			*/
			constexpr Colorf operator"" _rgbaf(unsigned long long value) {
				return { ((value >> 24) & 0xff) / 255.0f, ((value >> 16) & 0xff) / 255.0f, ((value >> 8) & 0xff) / 255.0f, ((value) & 0xff) / 255.0f };
			}

			#if (defined(DEATH_TARGET_CLANG) && __clang_major__ >= 17) || (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 15)
			#	pragma GCC diagnostic pop
			#endif
		}
	}
}
