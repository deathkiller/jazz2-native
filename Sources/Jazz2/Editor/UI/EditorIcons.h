#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

namespace Jazz2::Editor
{
	/** @brief Icon of the level editor */
	enum class EditorIcon : std::uint8_t
	{
		None,
		Brush,
		Eraser,
		Rectangle,
		Fill,
		Eyedropper,
		Select,
		Wand,
		Hand,
		Undo,
		Redo,
		Grid,
		Eye,
		EyeClosed,
		Lock,
		Unlock,
		Plus,
		Minus,
		ArrowUp,
		ArrowDown,
		Layers,
		Folder,
		Save,
		New,
		Play,
		Stop,
		ZoomIn,
		ZoomOut,
		Fit,
		Check,
		Close,
		ChevronRight,
		ChevronDown,
		FlipX,
		FlipY,
		Copy,
		Paste,
		Cut,
		Delete,
		Settings,
		Parallax,
		Warning,
		Info,
		Tiles,
		Export,
		File,
		Up,
		ChevronLeft,
		Event,
		Search,
		Mask,
		Pause,
		Path,
		Measure,
		Reach,
		SmartBrush,
		Slope,
		Undock,
		Dock,
		Usage,
		EventPath,
		Map,
		RotateCW,
		RotateCCW,
		Dice,
		Shuffle,
		RandomFlipX,
		RandomFlipY,
		ShapeLine,
		Ellipse,
		FilledEllipse,
		More,
		Debug,
		EnterEditor,
		Shapes,
		History,
		Properties,
		// Crossed out, a layer hidden in the game and so in the editor too, see Formats::LayerRecord::Visible
		EyeOff,
		// Only the outline of Play, as Play Here
		PlayOutline,
		// A flag with an arrow, an event of the original game kept for the level script, see EventType::PassthroughEvent
		Passthrough,
		// Shown by the pointer, see UiCursorOverlay
		CursorResizeHorizontal,
		CursorResizeVertical,
		CursorResizeNWSE,
		CursorResizeNESW,
		CursorMove,

		Count
	};

	/**
		@brief Pixels of the icons of the level editor

		Each icon is 16 × 16 pixels, written as text a row per string: `.` is transparent, `#` is the color the
		icon is drawn with, `+` the same color half transparent. An icon in the style of the game is drawn in colors
		of its own with a dark outline, see @ref GetPixelColor() --- the color it's drawn with multiplies them, so
		it's drawn in white to show them as they are, and the outline stays dark in any color.
	*/
	struct EditorIcons
	{
		/** @brief Size of an icon in pixels */
		static constexpr std::int32_t Size = 16;

		/** @brief Returns the rows of an icon */
		static const char* const* GetRows(EditorIcon icon);
		/**
		 * @brief Returns the color of a pixel of an icon as R, G, B and A bytes from the lowest, 0 for transparent
		 *
		 * Besides `#` and `+`, `1` to `7` are the color an eighth to seven eighths opaque, `o` is the dark outline and the letters are colors of the game, the capital one a
		 * shade darker: `w` white, `y` yellow, `r` red, `p` pink, `g` grey, `t` tan, `n` green and `b` blue. `m` is
		 * the shine on green, a pale green.
		 */
		static std::uint32_t GetPixelColor(char pixel);
	};
}

#endif
