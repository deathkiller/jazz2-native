#pragma once

namespace nCine
{
	/// Display properties
	class DisplayMode
	{
	public:
		enum class DoubleBuffering {
			Disabled,
			Enabled
		};

		enum class VSync {
			Disabled,
			Enabled
		};

		DisplayMode()
			: DisplayMode(0, 0, 0, 0, 0, 0, DoubleBuffering::Disabled, VSync::Disabled) {}
		DisplayMode(std::uint8_t redBits, std::uint8_t greenBits, std::uint8_t blueBits)
			: DisplayMode(redBits, greenBits, blueBits, 0, 0, 0, DoubleBuffering::Enabled, VSync::Disabled) {}
		DisplayMode(std::uint8_t redBits, std::uint8_t greenBits, std::uint8_t blueBits, std::uint8_t alphaBits)
			: DisplayMode(redBits, greenBits, blueBits, alphaBits, 0, 0, DoubleBuffering::Enabled, VSync::Disabled) {}
		DisplayMode(std::uint8_t depthBits, std::uint8_t stencilBits, DoubleBuffering dbMode, VSync vsMode)
			: DisplayMode(0, 0, 0, 0, depthBits, stencilBits, dbMode, vsMode) {}
		DisplayMode(std::uint8_t redBits, std::uint8_t greenBits, std::uint8_t blueBits, std::uint8_t alphaBits,
					std::uint8_t depthBits, std::uint8_t stencilBits, DoubleBuffering dbMode, VSync vsMode)
			: redBits_(redBits), greenBits_(greenBits), blueBits_(blueBits), alphaBits_(alphaBits),
			depthBits_(depthBits), stencilBits_(stencilBits), isDoubleBuffered_(dbMode == DoubleBuffering::Enabled),
			hasVSync_(vsMode == VSync::Enabled) {}

		/// Returns the number of bits for the red channel
		inline std::uint8_t redBits() const {
			return redBits_;
		}
		/// Returns the number of bits for the green channel
		inline std::uint8_t greenBits() const {
			return greenBits_;
		}
		/// Returns the number of bits for the blue channel
		inline std::uint8_t blueBits() const {
			return blueBits_;
		}
		/// Returns the number of bits for the alpha channel
		inline std::uint8_t alphaBits() const {
			return alphaBits_;
		}
		/// Returns the number of bits for the depth buffer
		inline std::uint8_t depthBits() const {
			return depthBits_;
		}
		/// Returns the number of bits for the stencil buffer
		inline std::uint8_t stencilBits() const {
			return stencilBits_;
		}
		/// Returns true if the display is double buffered
		inline bool isDoubleBuffered() const {
			return isDoubleBuffered_;
		}
		/// Returns true if the dislpay has V-sync enabled
		inline bool hasVSync() const {
			return hasVSync_;
		}

	private:
		/// Red component bits
		std::uint8_t redBits_;
		/// Green component bits
		std::uint8_t greenBits_;
		/// Blue component bits
		std::uint8_t blueBits_;
		/// Alpha component bits
		std::uint8_t alphaBits_;
		/// Depth buffer size in bit
		std::uint8_t depthBits_;
		/// Stencil buffer size in bit
		std::uint8_t stencilBits_;
		/// Double buffering flag
		bool isDoubleBuffered_;
		/// VSync flag
		bool hasVSync_;
	};

}

