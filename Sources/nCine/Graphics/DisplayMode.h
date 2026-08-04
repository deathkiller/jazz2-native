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
			: _redBits(redBits), _greenBits(greenBits), _blueBits(blueBits), _alphaBits(alphaBits),
			_depthBits(depthBits), _stencilBits(stencilBits), _isDoubleBuffered(dbMode == DoubleBuffering::Enabled),
			_hasVSync(vsMode == VSync::Enabled) {}

		/** @brief Returns the number of bits for the red channel */
		inline std::uint8_t redBits() const {
			return _redBits;
		}
		/** @brief Returns the number of bits for the green channel */
		inline std::uint8_t greenBits() const {
			return _greenBits;
		}
		/** @brief Returns the number of bits for the blue channel */
		inline std::uint8_t blueBits() const {
			return _blueBits;
		}
		/** @brief Returns the number of bits for the alpha channel */
		inline std::uint8_t alphaBits() const {
			return _alphaBits;
		}
		/** @brief Returns the number of bits for the depth buffer */
		inline std::uint8_t depthBits() const {
			return _depthBits;
		}
		/** @brief Returns the number of bits for the stencil buffer */
		inline std::uint8_t stencilBits() const {
			return _stencilBits;
		}
		/** @brief Returns whether the display is double buffered */
		inline bool isDoubleBuffered() const {
			return _isDoubleBuffered;
		}
		/** @brief Returns whether the display has vertical sync enabled */
		inline bool hasVSync() const {
			return _hasVSync;
		}

	private:
		/** @brief Red channel bits */
		std::uint8_t _redBits;
		/** @brief Green channel bits */
		std::uint8_t _greenBits;
		/** @brief Blue channel bits */
		std::uint8_t _blueBits;
		/** @brief Alpha channel bits */
		std::uint8_t _alphaBits;
		/** @brief Depth buffer size in bits */
		std::uint8_t _depthBits;
		/** @brief Stencil buffer size in bits */
		std::uint8_t _stencilBits;
		/** @brief Whether double buffering is enabled */
		bool _isDoubleBuffered;
		/** @brief Whether vertical sync is enabled */
		bool _hasVSync;
	};

}

