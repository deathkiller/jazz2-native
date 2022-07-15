#pragma once

namespace nCine
{
	class Colorf;

	/// A three channels unclamped float color
	class ColorHdr
	{
	public:
		static const int NumChannels = 3;
		static const ColorHdr Black;
		static const ColorHdr White;
		static const ColorHdr Red;
		static const ColorHdr Green;
		static const ColorHdr Blue;

		/// Default constructor (white color)
		constexpr ColorHdr()
			: ColorHdr(1.0f, 1.0f, 1.0f)
		{
		}
		/// Three channels constructor
		constexpr ColorHdr(float red, float green, float blue)
		{
			Set(red, green, blue);
		}
		/// Three channels constructor from an array
		explicit ColorHdr(const float channels[NumChannels]);
		/// Constructor taking a float color
		explicit ColorHdr(const Colorf& color);

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
		/// Gets the color array
		inline const float* Data() const {
			return &red_;
		}
		/// Gets the color array
		inline float* Data() {
			return &red_;
		}

		/// Sets three color channels
		constexpr void Set(float red, float green, float blue)
		{
			red_ = red;
			green_ = green;
			blue_ = blue;
		}
		/// Sets four color channels from an array
		void SetVec(const float channels[NumChannels]);

		/// Clamps negative channel values to zero
		void Clamp();
		/// Returns a clamped version of this color
		ColorHdr Clamped() const;

		/// Assignment operator from a float color
		ColorHdr& operator=(const Colorf& color);

		ColorHdr& operator+=(const ColorHdr& v);
		ColorHdr& operator-=(const ColorHdr& v);

		ColorHdr& operator*=(const ColorHdr& color);
		/// Multiplication by a constant scalar
		ColorHdr& operator*=(float scalar);

		ColorHdr operator+(const ColorHdr& color) const;
		ColorHdr operator-(const ColorHdr& color) const;

		ColorHdr operator*(const ColorHdr& color) const;
		/// Multiplication by a constant scalar
		ColorHdr operator*(float scalar) const;

	private:
		float red_;
		float green_;
		float blue_;
	};

}
