#pragma once

namespace nCine
{
	class Colorf;

	/// A four channels unsigned char color
	class Color
	{
	public:
		static constexpr int NumChannels = 4;

		static const Color Black;
		static const Color White;
		static const Color Red;
		static const Color Green;
		static const Color Blue;
		static const Color Yellow;
		static const Color Magenta;
		static const Color Cyan;

		/// Default constructor (white color)
		constexpr Color()
			: Color(0, 0, 0, 0)
		{
		}
		/// Three channels constructor
		constexpr Color(unsigned int red, unsigned int green, unsigned int blue)
			: Color(red, green, blue, 255)
		{
		}
		/// Four channels constructor
		constexpr Color(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha)
			: red_(red), green_(green), blue_(blue), alpha_(alpha)
		{
		}
		/// Three channels constructor from a hexadecimal code
		explicit Color(unsigned int hex);
		/// Four channels constructor from an array
		explicit Color(const unsigned int channels[NumChannels]);
		/// Constructor taking a normalized float color
		explicit Color(const Colorf& color);

		/// Gets the red channel of the color
		inline unsigned char R() const {
			return red_;
		}
		/// Gets the green channel of the color
		inline unsigned char G() const {
			return green_;
		}
		/// Gets the blue channel of the color
		inline unsigned char B() const {
			return blue_;
		}
		/// Gets the alpha channel of the color
		inline unsigned char A() const {
			return alpha_;
		}

		/// Returns the color as a single RGBA unsigned integer
		unsigned int Rgba() const;
		/// Returns the color as a single RGBA unsigned integer
		unsigned int Argb() const;
		/// Returns the color as a single ABGR unsigned integer
		unsigned int Abgr() const;
		/// Returns the color as a single BGRA unsigned integer
		unsigned int Bgra() const;

		/// Gets the color array
		inline const unsigned char* Data() const {
			return &red_;
		}
		/// Gets the color array
		inline unsigned char* Data() {
			return &red_;
		}

		/// Sets four color channels
		constexpr void Set(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha)
		{
			red_ = static_cast<unsigned char>(red);
			green_ = static_cast<unsigned char>(green);
			blue_ = static_cast<unsigned char>(blue);
			alpha_ = static_cast<unsigned char>(alpha);
		}
		/// Sets three color channels
		void Set(unsigned int red, unsigned int green, unsigned int blue);
		/// Sets three color channels from a hexadecimal code
		void Set(unsigned int hex);
		/// Sets four color channels from an array
		void SetVec(const unsigned int channels[NumChannels]);

		/// Sets the alpha channel
		void SetAlpha(unsigned int alpha);

		/// Assignment operator from a normalized float color
		Color& operator=(const Colorf& color);

		/// Equality operator
		bool operator==(const Color& color) const;

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

	private:
		unsigned char red_;
		unsigned char green_;
		unsigned char blue_;
		unsigned char alpha_;
	};

}
