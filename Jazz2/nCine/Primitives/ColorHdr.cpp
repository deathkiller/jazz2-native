#include "ColorHdr.h"
#include "Colorf.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	const ColorHdr ColorHdr::Black(0.0f, 0.0f, 0.0f);
	const ColorHdr ColorHdr::White(1.0f, 1.0f, 1.0f);
	const ColorHdr ColorHdr::Red(1.0f, 0.0f, 0.0f);
	const ColorHdr ColorHdr::Green(0.0f, 1.0f, 0.0f);
	const ColorHdr ColorHdr::Blue(0.0f, 0.0f, 1.0f);

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	ColorHdr::ColorHdr(const float channels[NumChannels])
	{
		SetVec(channels);
	}

	ColorHdr::ColorHdr(const Colorf& color)
		: ColorHdr(color.R(), color.G(), color.B())
	{
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void ColorHdr::SetVec(const float channels[NumChannels])
	{
		Set(channels[0], channels[1], channels[2]);
	}

	void ColorHdr::Clamp()
	{
		for (unsigned int i = 0; i < NumChannels; i++) {
			if (Data()[i] < 0.0f)
				Data()[i] = 0.0f;
		}
	}

	ColorHdr ColorHdr::Clamped() const
	{
		ColorHdr result;

		for (unsigned int i = 0; i < NumChannels; i++) {
			if (Data()[i] < 0.0f)
				result.Data()[i] = 0.0f;
			else
				result.Data()[i] = Data()[i];
		}

		return result;
	}

	ColorHdr& ColorHdr::operator=(const Colorf& color)
	{
		red_ = color.R();
		green_ = color.G();
		blue_ = color.B();

		return *this;
	}

	ColorHdr& ColorHdr::operator+=(const ColorHdr& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++)
			Data()[i] += color.Data()[i];

		return *this;
	}

	ColorHdr& ColorHdr::operator-=(const ColorHdr& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++)
			Data()[i] -= color.Data()[i];

		return *this;
	}

	ColorHdr& ColorHdr::operator*=(const ColorHdr& color)
	{
		for (unsigned int i = 0; i < NumChannels; i++)
			Data()[i] *= color.Data()[i];

		return *this;
	}

	ColorHdr& ColorHdr::operator*=(float scalar)
	{
		for (unsigned int i = 0; i < NumChannels; i++)
			Data()[i] *= scalar;

		return *this;
	}

	ColorHdr ColorHdr::operator+(const ColorHdr& color) const
	{
		ColorHdr result;

		for (unsigned int i = 0; i < NumChannels; i++)
			result.Data()[i] = Data()[i] + color.Data()[i];

		return result;
	}

	ColorHdr ColorHdr::operator-(const ColorHdr& color) const
	{
		ColorHdr result;

		for (unsigned int i = 0; i < NumChannels; i++)
			result.Data()[i] = Data()[i] - color.Data()[i];

		return result;
	}

	ColorHdr ColorHdr::operator*(const ColorHdr& color) const
	{
		ColorHdr result;

		for (unsigned int i = 0; i < NumChannels; i++)
			result.Data()[i] = Data()[i] * color.Data()[i];

		return result;
	}

	ColorHdr ColorHdr::operator*(float scalar) const
	{
		ColorHdr result;

		for (unsigned int i = 0; i < NumChannels; i++)
			result.Data()[i] = Data()[i] * scalar;

		return result;
	}

}
