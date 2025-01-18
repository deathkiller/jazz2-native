#pragma once

#include <Common.h>
#include <Containers/Tags.h>

namespace nCine
{
	class Colorf;

	using Death::Containers::NoInitT;

	/// Four-channels color with 8-bits integer per component
	class Color
	{
	public:
		static constexpr std::int32_t NumChannels = 4;

		static const Color Black;
		static const Color White;
		static const Color Red;
		static const Color Green;
		static const Color Blue;
		static const Color Yellow;
		static const Color Magenta;
		static const Color Cyan;

		std::uint8_t R;
		std::uint8_t G;
		std::uint8_t B;
		std::uint8_t A;

		/// Default constructor (transparent color)
		constexpr Color() noexcept
			: Color(0, 0, 0, 0)
		{
		}

		explicit Color(NoInitT) noexcept
		{
		}

		/// Three channels constructor
		constexpr Color(std::uint32_t red, std::uint32_t green, std::uint32_t blue) noexcept
			: Color(red, green, blue, 255)
		{
		}
		/// Four channels constructor
		constexpr Color(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha) noexcept
			: R(red), G(green), B(blue), A(alpha)
		{
		}
		/// Three channels constructor from a hexadecimal code
		explicit Color(std::uint32_t hex);
		/// Four channels constructor from an array
		explicit Color(const std::uint32_t channels[NumChannels]);
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
		constexpr void Set(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha)
		{
			R = static_cast<std::uint8_t>(red);
			G = static_cast<std::uint8_t>(green);
			B = static_cast<std::uint8_t>(blue);
			A = static_cast<std::uint8_t>(alpha);
		}
		/// Sets three color channels
		void Set(std::uint32_t red, std::uint32_t green, std::uint32_t blue);
		/// Sets three color channels from a hexadecimal code
		void Set(std::uint32_t hex);
		/// Sets four color channels from an array
		void SetVec(const std::uint32_t channels[NumChannels]);

		/// Sets the alpha channel
		void SetAlpha(std::uint32_t alpha);

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
}
