#pragma once

#include <Common.h>
#include <Containers/Tags.h>

namespace nCine
{
	inline namespace Primitives
	{
		class Colorf;

		using Death::Containers::NoInitT;

		/**
			@brief Four-channel color with 8-bit integers per component
			
			Stores the red, green, blue and alpha channels as @cpp std::uint8_t @ce values in the
			range 0-255 (member order follows the host endianness so that the channels pack into a
			single 32-bit integer). Provides packing into RGBA/ARGB/ABGR/BGRA integers, hexadecimal
			and array construction, channel-wise and scalar arithmetic, and conversion to and from
			the normalized floating-point @ref Colorf.
		*/
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

#if defined(DEATH_TARGET_BIG_ENDIAN)
			/** @brief Alpha */
			std::uint8_t A;  // Stored first (highest byte of std::uint32_t)
			/** @brief Blue */
			std::uint8_t B;
			/** @brief Green */
			std::uint8_t G;
			/** @brief Red */
			std::uint8_t R;  // Stored last (lowest byte of std::uint32_t)
#else
			/** @brief Red */
			std::uint8_t R;  // Stored first (lowest byte of std::uint32_t)
			/** @brief Green */
			std::uint8_t G;
			/** @brief Blue */
			std::uint8_t B;
			/** @brief Alpha */
			std::uint8_t A;  // Stored last (highest byte of std::uint32_t)
#endif

			/** @brief Default constructor, creates a fully transparent black color */
			constexpr Color() noexcept
				: Color(0, 0, 0, 0) {}

			explicit Color(NoInitT) noexcept {}

			/** @brief Constructs an opaque color from three channels */
			constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
				: Color(red, green, blue, 255) {}
			/** @brief Constructs a color from four channels */
			constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) noexcept
				: R(red), G(green), B(blue), A(alpha) {}
			/** @brief Constructs an opaque color from a 24-bit hexadecimal RGB code */
			explicit Color(std::uint32_t hex);
			/** @brief Constructs a color from a four-channel array */
			explicit Color(const std::uint8_t channels[NumChannels]);
			explicit Color(const Colorf& color);

			/** @brief Returns the color packed as a single RGBA integer */
			std::uint32_t Rgba() const;
			/** @brief Returns the color packed as a single ARGB integer */
			std::uint32_t Argb() const;
			/** @brief Returns the color packed as a single ABGR integer */
			std::uint32_t Abgr() const;
			/** @brief Returns the color packed as a single BGRA integer */
			std::uint32_t Bgra() const;

			/** @brief Returns a pointer to the channel array */
			inline const std::uint8_t* Data() const {
				return &R;
			}
			/** @overload */
			inline std::uint8_t* Data() {
				return &R;
			}

			/** @brief Sets all four channels */
			constexpr void Set(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
			{
				R = red;
				G = green;
				B = blue;
				A = alpha;
			}
			/** @brief Sets the three color channels, keeping the alpha unchanged */
			void Set(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
			/** @brief Sets the channels from a 24-bit hexadecimal RGB code */
			void Set(std::uint32_t hex);
			/** @brief Sets all four channels from an array */
			void SetVec(const std::uint8_t channels[NumChannels]);

			/** @brief Sets the alpha channel */
			void SetAlpha(std::uint8_t alpha);

			/** @brief Assigns from a normalized floating-point color */
			Color& operator=(const Colorf& color);

			bool operator==(const Color& color) const;
			bool operator!=(const Color& color) const;

			Color& operator+=(const Color& v);
			Color& operator-=(const Color& v);

			Color& operator*=(const Color& color);
			/** @brief Multiplies all channels by a scalar */
			Color& operator*=(float scalar);

			Color operator+(const Color& color) const;
			Color operator-(const Color& color) const;

			Color operator*(const Color& color) const;
			/** @brief Multiplies all channels by a scalar */
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
				
				@code{.cpp}
				using namespace nCine::Primitives::Literals;
				Color a = 0x5eb233bb_rgb;	// R: 0x5e, G: 0xb2, B: 0x33, A: 0xff
				@endcode

				See @ref Color for more information.
			*/
			constexpr Color operator"" _rgb(unsigned long long value) {
				return { std::uint8_t(value >> 16), std::uint8_t(value >> 8), std::uint8_t(value) };
			}

			/** @relatesalso nCine::Primitives::Color
				@brief 8bit-per-channel RGBA color literal
				
				@code{.cpp}
				using namespace nCine::Primitives::Literals;
				Color a = 0x5eb233bb_rgba;	// R: 0x5e, G: 0xb2, B: 0x33, A: 0xbb
				@endcode

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
