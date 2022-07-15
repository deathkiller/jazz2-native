#include "Colorf.h"
#include "Color.h"
#include "ColorHdr.h"
#include "../Base/Algorithms.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	const Colorf Colorf::Black(0.0f, 0.0f, 0.0f, 1.0f);
	const Colorf Colorf::White(1.0f, 1.0f, 1.0f, 1.0f);
	const Colorf Colorf::Red(1.0f, 0.0f, 0.0f, 1.0f);
	const Colorf Colorf::Green(0.0f, 1.0f, 0.0f, 1.0f);
	const Colorf Colorf::Blue(0.0f, 0.0f, 1.0f, 1.0f);
	const Colorf Colorf::Yellow(1.0f, 1.0f, 0.0f, 1.0f);
	const Colorf Colorf::Magenta(1.0f, 0.0f, 1.0f, 1.0f);
	const Colorf Colorf::Cyan(0.0f, 1.0f, 1.0f, 1.0f);

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Colorf::Colorf(const float channels[NumChannels])
	{
		SetVec(channels);
	}

	Colorf::Colorf(const Color& color)
	{
		const float inv = 1.0f / 255.0f;
		red_ = static_cast<float>(color.R() * inv);
		green_ = static_cast<float>(color.G() * inv);
		blue_ = static_cast<float>(color.B() * inv);
		alpha_ = static_cast<float>(color.A() * inv);
	}

	Colorf::Colorf(const ColorHdr& color)
		: Colorf(color.R(), color.G(), color.B(), 1.0f)
	{
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void Colorf::Set(float red, float green, float blue)
	{
		red_ = std::clamp(red, 0.0f, 1.0f);
		green_ = std::clamp(green, 0.0f, 1.0f);
		blue_ = std::clamp(blue, 0.0f, 1.0f);
	}

	void Colorf::SetVec(const float channels[NumChannels])
	{
		Set(channels[0], channels[1], channels[2], channels[3]);
	}

	void Colorf::SetAlpha(float alpha)
	{
		alpha_ = std::clamp(alpha, 0.0f, 1.0f);
	}

	Colorf& Colorf::operator=(const Color& color)
	{
		const float inv = 1.0f / 255.0f;
		red_ = static_cast<float>(color.R() * inv);
		green_ = static_cast<float>(color.G() * inv);
		blue_ = static_cast<float>(color.B() * inv);
		alpha_ = static_cast<float>(color.A() * inv);

		return *this;
	}

	Colorf& Colorf::operator+=(const Colorf& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] + color.Data()[i];
			Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return *this;
	}

	Colorf& Colorf::operator-=(const Colorf& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] - color.Data()[i];
			Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return *this;
	}

	Colorf& Colorf::operator*=(const Colorf& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] * color.Data()[i];
			Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return *this;
	}

	Colorf& Colorf::operator*=(float scalar)
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] * scalar;
			Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return *this;
	}

	Colorf Colorf::operator+(const Colorf& color) const
	{
		Colorf result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] + color.Data()[i];
			result.Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return result;
	}

	Colorf Colorf::operator-(const Colorf& color) const
	{
		Colorf result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] - color.Data()[i];
			result.Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return result;
	}

	Colorf Colorf::operator*(const Colorf& color) const
	{
		Colorf result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] * color.Data()[i];
			result.Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return result;
	}

	Colorf Colorf::operator*(float scalar) const
	{
		Colorf result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			const float channelValue = Data()[i] * scalar;
			result.Data()[i] = std::clamp(channelValue, 0.0f, 1.0f);
		}

		return result;
	}

}
