#pragma once

#include <Common.h>

namespace nCine
{
	class Colorf;

	/// A four channels unsigned char color
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

		/// Default constructor (white color)
		constexpr Color()
			: Color(0, 0, 0, 0)
		{
		}
		/// Three channels constructor
		constexpr Color(std::uint32_t red, std::uint32_t green, std::uint32_t blue)
			: Color(red, green, blue, 255)
		{
		}
		/// Four channels constructor
		constexpr Color(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha)
			: R(red), G(green), B(blue), A(alpha)
		{
		}
		/// Three channels constructor from a hexadecimal code
		explicit Color(std::uint32_t hex);
		/// Four channels constructor from an array
		explicit Color(const std::uint32_t channels[NumChannels]);
		/// Constructor taking a normalized float color
		explicit Color(const Colorf& color);

		/// Returns the color as a single RGBA unsigned integer
		std::uint32_t Rgba() const;
		/// Returns the color as a single RGBA unsigned integer
		std::uint32_t Argb() const;
		/// Returns the color as a single ABGR unsigned integer
		std::uint32_t Abgr() const;
		/// Returns the color as a single BGRA unsigned integer
		std::uint32_t Bgra() const;

		/// Gets the color array
		inline const std::uint8_t* Data() const {
			return &R;
		}
		/// Gets the color array
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

		/// Assignment operator from a normalized float color
		Color& operator=(const Colorf& color);

		/// Equality operator
		bool operator==(const Color& color) const;
		bool operator!=(const Color& color) const;

		Color& operator+=(const Color& v);
		Color& operator-=(const Color& v);

		Color& operator*=(const Color& color);
		/// Multiplication by a constant scalar
		Color& operator*=(float scalar);

		Color operator+(const Color& color) const;
		Color operator-(const Color& color) const;

		Color operator*(const Color& color) const;
		/// Multiplication by a constant scalar
		Color operator*(float scalar) const;
	};
}
