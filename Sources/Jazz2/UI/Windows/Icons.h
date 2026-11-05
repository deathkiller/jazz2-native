#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <type_traits>

namespace Jazz2::UI::Windows
{
	/** @brief Icon every interface has, an application adds its own ones from @ref IconId::FirstCustom, see @ref Icons::Register() */
	enum class StockIcon : std::uint16_t
	{
		None,
		Close,
		/** A smaller and thinner @ref Close, for a menu bar */
		CloseSmall,
		Check,
		ChevronLeft,
		ChevronRight,
		ChevronDown,
		ArrowUp,
		ArrowDown,
		/** A folder with an arrow up, the folder a folder is in */
		Up,
		Plus,
		Minus,
		Folder,
		File,
		Delete,
		Search,
		Info,
		Warning,
		/** Lifts a panel out of where it's docked */
		Undock,
		/** Puts a lifted panel back */
		Dock,
		/** Shown by the pointer, see @ref CursorOverlay */
		CursorResizeHorizontal,
		CursorResizeVertical,
		CursorResizeNWSE,
		CursorResizeNESW,
		CursorMove,

		Count
	};

	/**
		@brief Tells whether an enum of an application holds icons, so it converts to @ref IconId

		Specialized as @ref std::true_type for such an enum, its values start at @ref IconId::FirstCustom, but it may
		name a @ref StockIcon by its value too.
	*/
	template<class T>
	struct IsIconEnum : std::false_type {};

	/** @brief Icon of the interface, a @ref StockIcon or one of an application */
	class IconId
	{
	public:
		/** @brief First value of icons of an application */
		static constexpr std::uint16_t FirstCustom = 256;

		constexpr IconId() noexcept : _value(0) {}
		constexpr IconId(StockIcon icon) noexcept : _value(std::uint16_t(icon)) {}
		template<class T, class = std::enable_if_t<IsIconEnum<T>::value>>
		constexpr IconId(T icon) noexcept : _value(std::uint16_t(icon)) {}

		/** @brief Returns the icon of a value */
		static constexpr IconId FromValue(std::uint16_t value) noexcept {
			IconId icon;
			icon._value = value;
			return icon;
		}

		/** @brief Returns the value */
		constexpr std::uint16_t GetValue() const noexcept {
			return _value;
		}
		/** @brief Returns `true` if it's not @ref StockIcon::None */
		constexpr explicit operator bool() const noexcept {
			return (_value != 0);
		}

		friend constexpr bool operator==(IconId a, IconId b) noexcept {
			return (a._value == b._value);
		}
		friend constexpr bool operator!=(IconId a, IconId b) noexcept {
			return (a._value != b._value);
		}

	private:
		std::uint16_t _value;
	};

	/**
		@brief How an icon is drawn, vector art or pixel art

		Vector art is drawn on a square of @ref Icons::DesignSize units at the size an icon has, see @ref VectorArt.
		Pixel art is 16 × 16 pixels, written as text a row per string: `.` is transparent, `#` is the color the icon
		is drawn with, `+` the same color half transparent, enlarged to the size of an icon. Both take the colors of
		@ref Icons::GetPixelColor().
	*/
	struct IconArt
	{
		/** @brief Vector art, it's drawn instead of the pixel art if there's any */
		const char* Vector = nullptr;
		/** @brief Rows of pixel art */
		const char* const* PixelArt = nullptr;
	};

	/**
		@brief Icons of the interface, those every interface has and those an application registers

		An icon in the style of the game is drawn in colors of its own with a dark outline, see @ref GetPixelColor()
		--- the color it's drawn with multiplies them, so it's drawn in white to show them as they are, and the outline
		stays dark in any color. See @ref IconArt for how icons are drawn.
	*/
	class Icons
	{
	public:
		/** @brief Size of the pixel art of an icon in pixels */
		static constexpr std::int32_t PixelArtSize = 16;
		/** @brief Side of the square vector art of an icon is drawn on, in its units */
		static constexpr float DesignSize = 16.0f;

		/** @brief Returns how an icon of an application is drawn, empty if there's no such icon */
		using ArtFunc = IconArt(*)(IconId icon);

		Icons() = delete;

		/**
		 * @brief Adds icons of an application, @p count of them from @p first
		 *
		 * Registering the same icons again does nothing, the interface draws them from the next frame.
		 */
		static void Register(IconId first, std::uint16_t count, ArtFunc getArt);

		/** @brief Returns the number of all icons, the stock ones and those registered */
		static std::int32_t GetCount();
		/** @brief Returns the place of an icon among all of them, -1 if it's not known */
		static std::int32_t GetIndex(IconId icon);
		/** @brief Returns how the icon at a place among all of them is drawn, see @ref GetIndex() */
		static IconArt GetArt(std::int32_t index);
		/** @brief Changes whenever icons are registered */
		static std::uint32_t GetRevision();

		/**
		 * @brief Returns the color of a pixel of an icon as R, G, B and A bytes from the lowest, 0 for transparent
		 *
		 * Besides `#` and `+`, `1` to `7` are the color an eighth to seven eighths opaque, `o` is the dark outline and
		 * the letters are colors of the game, the capital one a shade darker: `w` white, `y` yellow, `r` red, `p` pink,
		 * `g` grey, `t` tan, `n` green and `b` blue. `m` is the shine on green, a pale green.
		 */
		static std::uint32_t GetPixelColor(char pixel);
	};
}

#endif
