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

		Count
	};

	/**
		@brief Pixels of the icons of the level editor

		Each icon is 16 × 16 pixels, written as text a row per string: `.` is transparent, `#` is the color the
		icon is drawn with, `+` the same color half transparent.
	*/
	struct EditorIcons
	{
		/** @brief Size of an icon in pixels */
		static constexpr std::int32_t Size = 16;

		/** @brief Returns the rows of an icon */
		static const char* const* GetRows(EditorIcon icon);
	};
}

#endif
