#pragma once

#include <Common.h>
#include <Containers/Tags.h>

namespace nCine
{
	inline namespace Primitives
	{
		class Colorf;

		using Death::Containers::NoInitT;

		/// Four-channel color with 8-bit integers per component
		class Color
		{
		public:
			/** @{ @name Constants */

			/** @brief Number of channels */
			static constexpr std::int32_t NumChannels = 4;

			/** @} */

			/** @{ @name Predefined colors */

			static const Color Black;
			static const Color White;
			static const Color Red;
			static const Color Green;
			static const Color Blue;
			static const Color Yellow;
			static const Color Magenta;
			static const Color Cyan;

			/** @} */

			/** @brief Red */
			std::uint8_t R;
			/** @brief Green */
			std::uint8_t G;
			/** @brief Blue */
			std::uint8_t B;
			/** @brief Alpha */
			std::uint8_t A;

			/// Default constructor (transparent color)
			constexpr Color() noexcept
				: Color(0, 0, 0, 0) {}

			explicit Color(NoInitT) noexcept {}

			/// Three channels constructor
			constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
				: Color(red, green, blue, 255) {}
			/// Four channels constructor
			constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) noexcept
				: R(red), G(green), B(blue), A(alpha) {}
			/// Three channels constructor from a hexadecimal code
			explicit Color(std::uint32_t hex);
			/// Four channels constructor from an array
			explicit Color(const std::uint8_t channels[NumChannels]);
			explicit Color(const Colorf& color);

			/// Returns the color as a single RGBA unsigned integer
			std::uint32_t Rgba() const;
			/// Returns the color as a single RGBA unsigned integer
			std::uint32_t Argb() const;
			/// Returns the color as a single ABGR unsigned integer
			std::uint32_t Abgr() const;
			/// Returns the color as a single BGRA unsigned integer
			std::uint32_t Bgra() const;

			/// Returns color array
			inline const std::uint8_t* Data() const {
				return &R;
			}
			/// @overload
			inline std::uint8_t* Data() {
				return &R;
			}

			/// Sets four color channels
			constexpr void Set(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
			{
				R = red;
				G = green;
				B = blue;
				A = alpha;
			}
			/// Sets three color channels
			void Set(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
			/// Sets three color channels from a hexadecimal code
			void Set(std::uint32_t hex);
			/// Sets four color channels from an array
			void SetVec(const std::uint8_t channels[NumChannels]);

			/// Sets the alpha channel
			void SetAlpha(std::uint8_t alpha);

			/// Assigns operator from a normalized float color
			Color& operator=(const Colorf& color);

			bool operator==(const Color& color) const;
			bool operator!=(const Color& color) const;

			Color& operator+=(const Color& v);
			Color& operator-=(const Color& v);

			Color& operator*=(const Color& color);
			/// Multiplies by a constant scalar
			Color& operator*=(float scalar);

			Color operator+(const Color& color) const;
			Color operator-(const Color& color) const;

			Color operator*(const Color& color) const;
			/// Multiplies by a constant scalar
			Color operator*(float scalar) const;
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

			/** @relatesalso nCine::Primitives::Color
				@brief 8bit-per-channel RGB color literal

				See @ref Color for more information.
			*/
			constexpr Color operator"" _rgb(unsigned long long value) {
				return { std::uint8_t(value >> 16), std::uint8_t(value >> 8), std::uint8_t(value) };
			}

			/** @relatesalso nCine::Primitives::Color
				@brief 8bit-per-channel RGBA color literal

				See @ref Color for more information.
			*/
			constexpr Color operator"" _rgba(unsigned long long value) {
				return { std::uint8_t(value >> 24), std::uint8_t(value >> 16), std::uint8_t(value >> 8), std::uint8_t(value) };
			}

			#if (defined(DEATH_TARGET_CLANG) && __clang_major__ >= 17) || (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 15)
			#	pragma GCC diagnostic pop
			#endif
		}
	}
}
