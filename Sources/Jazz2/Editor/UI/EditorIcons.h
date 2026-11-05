#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../UI/Windows/Icons.h"

namespace Jazz2::Editor
{
	/**
		@brief Icon of the level editor

		Those every interface has are named here too, with the values of their @ref UI::Windows::StockIcon, the rest
		are the editor's own, see @ref EditorIcons.
	*/
	enum class EditorIcon : std::uint16_t
	{
		None = std::uint16_t(UI::Windows::StockIcon::None),
		Close = std::uint16_t(UI::Windows::StockIcon::Close),
		CloseSmall = std::uint16_t(UI::Windows::StockIcon::CloseSmall),
		Check = std::uint16_t(UI::Windows::StockIcon::Check),
		ChevronLeft = std::uint16_t(UI::Windows::StockIcon::ChevronLeft),
		ChevronRight = std::uint16_t(UI::Windows::StockIcon::ChevronRight),
		ChevronDown = std::uint16_t(UI::Windows::StockIcon::ChevronDown),
		ArrowUp = std::uint16_t(UI::Windows::StockIcon::ArrowUp),
		ArrowDown = std::uint16_t(UI::Windows::StockIcon::ArrowDown),
		Up = std::uint16_t(UI::Windows::StockIcon::Up),
		Plus = std::uint16_t(UI::Windows::StockIcon::Plus),
		Minus = std::uint16_t(UI::Windows::StockIcon::Minus),
		Folder = std::uint16_t(UI::Windows::StockIcon::Folder),
		File = std::uint16_t(UI::Windows::StockIcon::File),
		Delete = std::uint16_t(UI::Windows::StockIcon::Delete),
		Search = std::uint16_t(UI::Windows::StockIcon::Search),
		Info = std::uint16_t(UI::Windows::StockIcon::Info),
		Warning = std::uint16_t(UI::Windows::StockIcon::Warning),
		Undock = std::uint16_t(UI::Windows::StockIcon::Undock),
		Dock = std::uint16_t(UI::Windows::StockIcon::Dock),
		CursorResizeHorizontal = std::uint16_t(UI::Windows::StockIcon::CursorResizeHorizontal),
		CursorResizeVertical = std::uint16_t(UI::Windows::StockIcon::CursorResizeVertical),
		CursorResizeNWSE = std::uint16_t(UI::Windows::StockIcon::CursorResizeNWSE),
		CursorResizeNESW = std::uint16_t(UI::Windows::StockIcon::CursorResizeNESW),
		CursorMove = std::uint16_t(UI::Windows::StockIcon::CursorMove),

		Brush = UI::Windows::IconId::FirstCustom,
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
		Layers,
		Save,
		New,
		Play,
		Stop,
		ZoomIn,
		ZoomOut,
		Fit,
		FlipX,
		FlipY,
		Copy,
		Paste,
		Cut,
		Settings,
		Parallax,
		Tiles,
		Export,
		Event,
		Mask,
		Pause,
		Path,
		Measure,
		Reach,
		SmartBrush,
		Slope,
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
		// A pencil, a value changed from what it was
		Pencil,
		// A page with hills, a level file
		LevelFile,
		// A level file with a badge of a prompt, a level with a script next to it
		ScriptedLevelFile,
		// A page with a badge of a cross, a file that couldn't be read
		BrokenFile,
		// A page with tiles, a tile set file
		TileSetFile,
		// A page with a note, a music file
		MusicFile,
		// A page with a gradient, a tile set with true color tiles
		TrueColorTileSetFile,

		// The end of the editor's own icons
		CustomEnd
	};

	/** @brief Vector art of the icons of the level editor, see @ref UI::Windows::Icons */
	struct EditorIcons
	{
		/** @brief Adds the editor's own icons to the interface, it does nothing if they're added already */
		static void Register();
		/** @brief Returns how an icon of the editor is drawn, empty for a stock icon */
		static UI::Windows::IconArt GetArt(UI::Windows::IconId icon);
	};
}

namespace Jazz2::UI::Windows
{
	template<>
	struct IsIconEnum<Editor::EditorIcon> : std::true_type {};
}

#endif
