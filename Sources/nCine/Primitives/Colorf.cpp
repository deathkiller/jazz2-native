#include "Colorf.h"
#include "Color.h"
#include "../Base/Algorithms.h"

namespace nCine
{
	const Colorf Colorf::Black(0.0f, 0.0f, 0.0f, 1.0f);
	const Colorf Colorf::White(1.0f, 1.0f, 1.0f, 1.0f);
	const Colorf Colorf::Red(1.0f, 0.0f, 0.0f, 1.0f);
	const Colorf Colorf::Green(0.0f, 1.0f, 0.0f, 1.0f);
	const Colorf Colorf::Blue(0.0f, 0.0f, 1.0f, 1.0f);
	const Colorf Colorf::Yellow(1.0f, 1.0f, 0.0f, 1.0f);
	const Colorf Colorf::Magenta(1.0f, 0.0f, 1.0f, 1.0f);
	const Colorf Colorf::Cyan(0.0f, 1.0f, 1.0f, 1.0f);

	Colorf::Colorf(const float channels[NumChannels])
	{
		SetVec(channels);
	}

	Colorf::Colorf(const Color& color)
	{
		constexpr float inv = 1.0f / 255.0f;
		R = static_cast<float>(color.R * inv);
		G = static_cast<float>(color.G * inv);
		B = static_cast<float>(color.B * inv);
		A = static_cast<float>(color.A * inv);
	}

	void Colorf::Set(float red, float green, float blue)
	{
		R = red;
		G = green;
		B = blue;
	}

	void Colorf::SetVec(const float channels[NumChannels])
	{
		Set(channels[0], channels[1], channels[2], channels[3]);
	}

	void Colorf::SetAlpha(float alpha)
	{
		A = std::clamp(alpha, 0.0f, 1.0f);
	}

	Colorf& Colorf::operator=(const Color& color)
	{
		constexpr float inv = 1.0f / 255.0f;
		R = static_cast<float>(color.R * inv);
		G = static_cast<float>(color.G * inv);
		B = static_cast<float>(color.B * inv);
		A = static_cast<float>(color.A * inv);

		return *this;
	}

	bool Colorf::operator==(const Colorf& color) const
	{
		return (R == color.R && G == color.G &&
				B == color.B && A == color.A);
	}

	bool Colorf::operator!=(const Colorf& color) const
	{
		return (R != color.R || G != color.G ||
				B != color.B || A != color.A);
	}

	Colorf& Colorf::operator+=(const Colorf& color)
	{
		for (std::int32_t i = 0; i < NumChannels; i++) {
			Data()[i] = Data()[i] + color.Data()[i];
		}

		return *this;
	}

	Colorf& Colorf::operator-=(const Colorf& color)
	{
		for (std::int32_t i = 0; i < NumChannels; i++) {
			Data()[i] = Data()[i] - color.Data()[i];
		}

		return *this;
	}

	Colorf& Colorf::operator*=(const Colorf& color)
	{
		for (std::int32_t i = 0; i < NumChannels; i++) {
			Data()[i] = Data()[i] * color.Data()[i];
		}

		return *this;
	}

	Colorf& Colorf::operator*=(float scalar)
	{
		for (std::int32_t i = 0; i < NumChannels; i++) {
			Data()[i] = Data()[i] * scalar;
		}

		return *this;
	}

	Colorf Colorf::operator+(const Colorf& color) const
	{
		Colorf result;

		for (std::int32_t i = 0; i < NumChannels; i++) {
			result.Data()[i] = Data()[i] + color.Data()[i];
		}

		return result;
	}

	Colorf Colorf::operator-(const Colorf& color) const
	{
		Colorf result;

		for (std::int32_t i = 0; i < NumChannels; i++) {
			result.Data()[i] = Data()[i] - color.Data()[i];
		}

		return result;
	}

	Colorf Colorf::operator*(const Colorf& color) const
	{
		Colorf result;

		for (std::int32_t i = 0; i < NumChannels; i++) {
			result.Data()[i] = Data()[i] * color.Data()[i];
		}

		return result;
	}

	Colorf Colorf::operator*(float scalar) const
	{
		Colorf result;

		for (std::int32_t i = 0; i < NumChannels; i++) {
			result.Data()[i] = Data()[i] * scalar;
		}

		return result;
	}
}
