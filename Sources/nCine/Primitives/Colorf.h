#pragma once

#include <Common.h>

#include <algorithm>

namespace nCine
{
	class Color;

	/// A four channels normalized float color
	class Colorf
	{
	public:
		static constexpr std::int32_t NumChannels = 4;

		static const Colorf Black;
		static const Colorf White;
		static const Colorf Red;
		static const Colorf Green;
		static const Colorf Blue;
		static const Colorf Yellow;
		static const Colorf Magenta;
		static const Colorf Cyan;

		float R;
		float G;
		float B;
		float A;

		/// Default constructor (white color)
		constexpr Colorf()
			: Colorf(1.0f, 1.0f, 1.0f, 1.0f)
		{
		}
		/// Three channels constructor
		constexpr Colorf(float red, float green, float blue)
			: Colorf(red, green, blue, 1.0f)
		{
		}
		/// Four channels constructor
		constexpr Colorf(float red, float green, float blue, float alpha)
			: R(red), G(green), B(blue), A(std::clamp(alpha, 0.0f, 1.0f))
		{
		}
		/// Four channels constructor from an array
		explicit Colorf(const float channels[NumChannels]);
		/// Constructor taking an unsigned char color
		explicit Colorf(const Color& color);

		/// Gets the color array
		inline const float* Data() const {
			return &R;
		}
		/// Gets the color array
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

		/// Assignment operator from an unsigned char color
		Colorf& operator=(const Color& color);

		/// Equality operator
		bool operator==(const Colorf& color) const;
		bool operator!=(const Colorf& color) const;

		Colorf& operator+=(const Colorf& v);
		Colorf& operator-=(const Colorf& v);

		Colorf& operator*=(const Colorf& color);
		/// Multiplication by a constant scalar
		Colorf& operator*=(float scalar);

		Colorf operator+(const Colorf& color) const;
		Colorf operator-(const Colorf& color) const;

		Colorf operator*(const Colorf& color) const;
		/// Multiplication by a constant scalar
		Colorf operator*(float scalar) const;
	};
}
