#pragma once

namespace nCine
{
	/**
		@brief Describes the pixel format and buffering properties of a display surface
		
		Immutable set of attributes requested for or reported by an OpenGL context: the per-channel color bit
		depths, the depth and stencil buffer sizes, and whether double buffering and vertical sync are enabled.
	*/
	class DisplayMode
	{
	public:
		/**
		 * @brief Double buffering state
		 */
		enum class DoubleBuffering {
			Disabled,
			Enabled
		};

		/**
		 * @brief Vertical sync state
		 */
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

		/** @brief Returns the number of bits for the red channel */
		inline std::uint8_t redBits() const {
			return redBits_;
		}
		/** @brief Returns the number of bits for the green channel */
		inline std::uint8_t greenBits() const {
			return greenBits_;
		}
		/** @brief Returns the number of bits for the blue channel */
		inline std::uint8_t blueBits() const {
			return blueBits_;
		}
		/** @brief Returns the number of bits for the alpha channel */
		inline std::uint8_t alphaBits() const {
			return alphaBits_;
		}
		/** @brief Returns the number of bits for the depth buffer */
		inline std::uint8_t depthBits() const {
			return depthBits_;
		}
		/** @brief Returns the number of bits for the stencil buffer */
		inline std::uint8_t stencilBits() const {
			return stencilBits_;
		}
		/** @brief Returns whether the display is double buffered */
		inline bool isDoubleBuffered() const {
			return isDoubleBuffered_;
		}
		/** @brief Returns whether the display has vertical sync enabled */
		inline bool hasVSync() const {
			return hasVSync_;
		}

	private:
		/** @brief Red channel bits */
		std::uint8_t redBits_;
		/** @brief Green channel bits */
		std::uint8_t greenBits_;
		/** @brief Blue channel bits */
		std::uint8_t blueBits_;
		/** @brief Alpha channel bits */
		std::uint8_t alphaBits_;
		/** @brief Depth buffer size in bits */
		std::uint8_t depthBits_;
		/** @brief Stencil buffer size in bits */
		std::uint8_t stencilBits_;
		/** @brief Whether double buffering is enabled */
		bool isDoubleBuffered_;
		/** @brief Whether vertical sync is enabled */
		bool hasVSync_;
	};

}

