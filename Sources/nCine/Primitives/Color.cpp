#include "Color.h"
#include "Colorf.h"

#include <algorithm>

namespace nCine
{
	inline namespace Primitives
	{
		const Color Color::Black(0, 0, 0, 255);
		const Color Color::White(255, 255, 255, 255);
		const Color Color::Red(255, 0, 0, 255);
		const Color Color::Green(0, 255, 0, 255);
		const Color Color::Blue(0, 0, 255, 255);
		const Color Color::Yellow(255, 255, 0, 255);
		const Color Color::Magenta(255, 0, 255, 255);
		const Color Color::Cyan(0, 255, 255, 255);

		Color::Color(std::uint32_t hex)
		{
			SetAlpha(255);
			// The following method might set the alpha channel
			Set(hex);
		}

		Color::Color(const std::uint8_t channels[NumChannels])
		{
			SetVec(channels);
		}

		Color::Color(const Colorf& color)
		{
			R = std::uint8_t(color.R * 255);
			G = std::uint8_t(color.G * 255);
			B = std::uint8_t(color.B * 255);
			A = std::uint8_t(color.A * 255);
		}

		std::uint32_t Color::Rgba() const
		{
			return (R << 24) | (G << 16) | (B << 8) | A;
		}

		std::uint32_t Color::Argb() const
		{
			return (A << 24) | (R << 16) | (G << 8) | B;
		}

		std::uint32_t Color::Abgr() const
		{
			return (A << 24) | (B << 16) | (G << 8) | R;
		}

		std::uint32_t Color::Bgra() const
		{
			return (B << 24) | (G << 16) | (R << 8) | A;
		}

		void Color::Set(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
		{
			R = red;
			G = green;
			B = blue;
		}

		void Color::Set(std::uint32_t hex)
		{
			R = static_cast<std::uint8_t>((hex & 0xFF0000) >> 16);
			G = static_cast<std::uint8_t>((hex & 0xFF00) >> 8);
			B = static_cast<std::uint8_t>(hex & 0xFF);

			if (hex > 0xFFFFFF) {
				A = static_cast<std::uint8_t>((hex & 0xFF000000) >> 24);
			}
		}

		void Color::SetVec(const std::uint8_t channels[NumChannels])
		{
			Set(channels[0], channels[1], channels[2], channels[3]);
		}

		void Color::SetAlpha(std::uint8_t alpha)
		{
			A = alpha;
		}

		Color& Color::operator=(const Colorf& color)
		{
			R = std::uint8_t(color.R * 255.0f);
			G = std::uint8_t(color.G * 255.0f);
			B = std::uint8_t(color.B * 255.0f);
			A = std::uint8_t(color.A * 255.0f);

			return *this;
		}

		bool Color::operator==(const Color& color) const
		{
			return (R == color.R && G == color.G &&
					B == color.B && A == color.A);
		}

		bool Color::operator!=(const Color& color) const
		{
			return (R != color.R || G != color.G ||
					B != color.B || A != color.A);
		}

		Color& Color::operator+=(const Color& color)
		{
			for (std::uint32_t i = 0; i < NumChannels; i++) {
				std::uint32_t channelValue = Data()[i] + color.Data()[i];
				channelValue = std::clamp(channelValue, 0U, 255U);
				Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return *this;
		}

		Color& Color::operator-=(const Color& color)
		{
			for (std::uint32_t i = 0; i < NumChannels; i++) {
				std::uint32_t channelValue = Data()[i] - color.Data()[i];
				channelValue = std::clamp(channelValue, 0U, 255U);
				Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return *this;
		}

		Color& Color::operator*=(const Color& color)
		{
			for (std::uint32_t i = 0; i < NumChannels; i++) {
				float channelValue = Data()[i] * (color.Data()[i] / 255.0f);
				channelValue = std::clamp(channelValue, 0.0f, 255.0f);
				Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return *this;
		}

		Color& Color::operator*=(float scalar)
		{
			for (std::uint32_t i = 0; i < NumChannels; i++) {
				float channelValue = Data()[i] * scalar;
				channelValue = std::clamp(channelValue, 0.0f, 255.0f);
				Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return *this;
		}

		Color Color::operator+(const Color& color) const
		{
			Color result;

			for (std::uint32_t i = 0; i < NumChannels; i++) {
				std::uint32_t channelValue = Data()[i] + color.Data()[i];
				channelValue = std::clamp(channelValue, 0U, 255U);
				result.Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return result;
		}

		Color Color::operator-(const Color& color) const
		{
			Color result;

			for (std::uint32_t i = 0; i < NumChannels; i++) {
				std::uint32_t channelValue = Data()[i] - color.Data()[i];
				channelValue = std::clamp(channelValue, 0U, 255U);
				result.Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return result;
		}

		Color Color::operator*(const Color& color) const
		{
			Color result;

			for (std::uint32_t i = 0; i < NumChannels; i++) {
				float channelValue = Data()[i] * (color.Data()[i] / 255.0f);
				channelValue = std::clamp(channelValue, 0.0f, 255.0f);
				result.Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return result;
		}

		Color Color::operator*(float scalar) const
		{
			Color result;

			for (std::uint32_t i = 0; i < NumChannels; i++) {
				float channelValue = Data()[i] * scalar;
				channelValue = std::clamp(channelValue, 0.0f, 255.0f);
				result.Data()[i] = static_cast<std::uint8_t>(channelValue);
			}

			return result;
		}
	}
}
