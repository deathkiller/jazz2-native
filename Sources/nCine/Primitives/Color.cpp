#include "Color.h"
#include "Colorf.h"

#include <algorithm>

namespace nCine {

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	const Color Color::Black(0, 0, 0, 255);
	const Color Color::White(255, 255, 255, 255);
	const Color Color::Red(255, 0, 0, 255);
	const Color Color::Green(0, 255, 0, 255);
	const Color Color::Blue(0, 0, 255, 255);
	const Color Color::Yellow(255, 255, 0, 255);
	const Color Color::Magenta(255, 0, 255, 255);
	const Color Color::Cyan(0, 255, 255, 255);

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Color::Color(unsigned int hex)
	{
		SetAlpha(255);
		// The following method might set the alpha channel
		Set(hex);
	}

	Color::Color(const unsigned int channels[NumChannels])
	{
		SetVec(channels);
	}

	Color::Color(const Colorf& color)
	{
		red_ = static_cast<unsigned char>(color.R() * 255);
		green_ = static_cast<unsigned char>(color.G() * 255);
		blue_ = static_cast<unsigned char>(color.B() * 255);
		alpha_ = static_cast<unsigned char>(color.A() * 255);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	unsigned int Color::Rgba() const
	{
		return (red_ << 24) + (green_ << 16) + (blue_ << 8) + alpha_;
	}

	unsigned int Color::Argb() const
	{
		return (alpha_ << 24) + (red_ << 16) + (green_ << 8) + blue_;
	}

	unsigned int Color::Abgr() const
	{
		return (alpha_ << 24) + (blue_ << 16) + (green_ << 8) + red_;
	}

	unsigned int Color::Bgra() const
	{
		return (blue_ << 24) + (green_ << 16) + (red_ << 8) + alpha_;
	}

	void Color::Set(unsigned int red, unsigned int green, unsigned int blue)
	{
		red_ = static_cast<unsigned char>(red);
		green_ = static_cast<unsigned char>(green);
		blue_ = static_cast<unsigned char>(blue);
	}

	void Color::Set(unsigned int hex)
	{
		red_ = static_cast<unsigned char>((hex & 0xFF0000) >> 16);
		green_ = static_cast<unsigned char>((hex & 0xFF00) >> 8);
		blue_ = static_cast<unsigned char>(hex & 0xFF);

		if (hex > 0xFFFFFF)
			alpha_ = static_cast<unsigned char>((hex & 0xFF000000) >> 24);
	}

	void Color::SetVec(const unsigned int channels[NumChannels])
	{
		Set(channels[0], channels[1], channels[2], channels[3]);
	}

	void Color::SetAlpha(unsigned int alpha)
	{
		alpha_ = static_cast<unsigned char>(alpha);
	}

	Color& Color::operator=(const Colorf& color)
	{
		red_ = static_cast<unsigned char>(color.R() * 255.0f);
		green_ = static_cast<unsigned char>(color.G() * 255.0f);
		blue_ = static_cast<unsigned char>(color.B() * 255.0f);
		alpha_ = static_cast<unsigned char>(color.A() * 255.0f);

		return *this;
	}

	bool Color::operator==(const Color& color) const
	{
		return (red_ == color.red_ && green_ == color.green_ &&
				blue_ == color.blue_ && alpha_ == color.alpha_);
	}

	bool Color::operator!=(const Color& color) const
	{
		return (red_ != color.red_ || green_ != color.green_ ||
				blue_ != color.blue_ || alpha_ != color.alpha_);
	}

	Color& Color::operator+=(const Color& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			unsigned int channelValue = Data()[i] + color.Data()[i];
			channelValue = std::clamp(channelValue, 0U, 255U);
			Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return *this;
	}

	Color& Color::operator-=(const Color& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			unsigned int channelValue = Data()[i] - color.Data()[i];
			channelValue = std::clamp(channelValue, 0U, 255U);
			Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return *this;
	}

	Color& Color::operator*=(const Color& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			float channelValue = Data()[i] * (color.Data()[i] / 255.0f);
			channelValue = std::clamp(channelValue, 0.0f, 255.0f);
			Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return *this;
	}

	Color& Color::operator*=(float scalar)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			float channelValue = Data()[i] * scalar;
			channelValue = std::clamp(channelValue, 0.0f, 255.0f);
			Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return *this;
	}

	Color Color::operator+(const Color& color) const
	{
		Color result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			unsigned int channelValue = Data()[i] + color.Data()[i];
			channelValue = std::clamp(channelValue, 0U, 255U);
			result.Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return result;
	}

	Color Color::operator-(const Color& color) const
	{
		Color result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			unsigned int channelValue = Data()[i] - color.Data()[i];
			channelValue = std::clamp(channelValue, 0U, 255U);
			result.Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return result;
	}

	Color Color::operator*(const Color& color) const
	{
		Color result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			float channelValue = Data()[i] * (color.Data()[i] / 255.0f);
			channelValue = std::clamp(channelValue, 0.0f, 255.0f);
			result.Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return result;
	}

	Color Color::operator*(float scalar) const
	{
		Color result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			float channelValue = Data()[i] * scalar;
			channelValue = std::clamp(channelValue, 0.0f, 255.0f);
			result.Data()[i] = static_cast<unsigned char>(channelValue);
		}

		return result;
	}

}
