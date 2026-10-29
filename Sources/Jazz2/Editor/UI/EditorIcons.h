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
		// A sprite in front of the one it replaces, what the script of the level gives objects instead of theirs
		OverriddenAssets,
		// A smaller and thinner Close, for the menu bar
		CloseSmall,
		// Half of a square filled inside a selection
		InvertSelection,
		// A screen with a landscape, the frame of what the game screen shows
		ScreenFrame,
		// Tiles between two corners, as the block is grabbed by them
		GrabBlock,
		// Layers with a pause sign, their movement stopped
		PauseLayers,
		// An arrow down into a tray, the counterpart of Export
		Import,
		// A lifebuoy, the copies of levels kept to recover from
		Recovery,
		// A box with a prompt, actions found by typing
		CommandPalette,
		// The flag of events with a checkerboard, a waypoint of a race
		RaceFlag,
		// A shining bulb, an event of light
		LightBulb,
		// The flag of events standing on layers, events drawn over all of them
		EventsOnTop,
		// The blaster of Spaz with a small Play, trying a weapon
		Weapon,
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
