#pragma once

#include <algorithm>

namespace nCine
{
	class Color;

	/// A four channels normalized float color
	class Colorf
	{
	public:
		static constexpr int NumChannels = 4;

		static const Colorf Black;
		static const Colorf White;
		static const Colorf Red;
		static const Colorf Green;
		static const Colorf Blue;
		static const Colorf Yellow;
		static const Colorf Magenta;
		static const Colorf Cyan;

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
		{
			Set(red, green, blue, alpha);
		}
		/// Four channels constructor from an array
		explicit Colorf(const float channels[NumChannels]);
		/// Constructor taking an unsigned char color
		explicit Colorf(const Color& color);

		/// Gets the red channel of the color
		inline float R() const {
			return red_;
		}
		/// Gets the green channel of the color
		inline float G() const {
			return green_;
		}
		/// Gets the blue channel of the color
		inline float B() const {
			return blue_;
		}
		/// Gets the alpha channel of the color
		inline float A() const {
			return alpha_;
		}
		/// Gets the color array
		inline const float* Data() const {
			return &red_;
		}
		/// Gets the color array
		inline float* Data() {
			return &red_;
		}

		/// Sets four color channels
		constexpr void Set(float red, float green, float blue, float alpha)
		{
			red_ = red;
			green_ = green;
			blue_ = blue;
			alpha_ = std::clamp(alpha, 0.0f, 1.0f);
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

	private:
		float red_;
		float green_;
		float blue_;
		float alpha_;
	};

}
