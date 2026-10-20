#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiSkin.h"
#include "UiTheme.h"
#include "../../UI/Alignment.h"
#include "../../UI/Canvas.h"
#include "../../UI/Font.h"

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::StringView;
	using UI::Alignment;
#endif

	/** @brief Layer of a slot, things of a later layer are drawn over those of an earlier one */
	enum class UiLayer : std::uint8_t
	{
		Back,			/**< Shadows */
		Fill,			/**< Backgrounds of panels and controls */
		Content,		/**< Images, tiles and icons */
		Text,			/**< Text */
		Overlay,		/**< Highlights and outlines over the content */
		OverlayText		/**< Text over the highlights */
	};

	/**
		@brief Draws the interface of the level editor

		All positions and sizes are in pixels of the screen. The interface is drawn in slots --- the docked panels in
		one, a floating window in another, a menu over them in yet another --- and a later slot is always drawn over
		an earlier one. Inside a slot, things are drawn in layers, see @ref UiLayer.
	*/
	class UiPainter
	{
	public:
		/** @brief Number of depth values a slot takes */
		static constexpr std::int32_t SlotDepth = 8;
		/** @brief Depth of the first slot, the interface is drawn over everything else */
		static constexpr std::uint16_t BaseDepth = 0xE000;

		/** @brief Creates a new instance, @p lines draws slanted lines antialiased, they're rough without it */
		UiPainter(UI::Canvas& canvas, UiSkin& skin, UI::Font* font, float scale, Rendering::PolylineRenderer* lines = nullptr);

		/** @brief Returns how many pixels a unit takes */
		float GetScale() const {
			return _scale;
		}
		/** @brief Converts units to whole pixels */
		float Px(float units) const;
		/** @brief Returns the font */
		UI::Font* GetFont() const {
			return _font;
		}

		/** @brief Sets the slot things are drawn in */
		void SetSlot(std::int32_t slot) {
			_slot = slot;
		}
		/** @brief Returns the slot things are drawn in */
		std::int32_t GetSlot() const {
			return _slot;
		}
		/** @brief Returns the depth of a layer of the current slot */
		std::uint16_t GetDepth(UiLayer layer) const;

		/** @brief Clips everything drawn to a rectangle, inside of the current clipping */
		void PushClip(const Rectf& rect);
		/** @brief Ends the last clipping */
		void PopClip();
		/** @brief Returns the current clipping, empty if nothing is clipped */
		const Rectf& GetClip() const;
		/** @brief Returns `false` if nothing drawn in a rectangle, or a little around it, would be seen inside of the clipping */
		bool IsVisible(const Rectf& rect) const;
		/** @brief Makes everything drawn more transparent */
		void PushOpacity(float opacity);
		/** @brief Ends the last transparency */
		void PopOpacity();

		/** @brief Fills a rectangle */
		void FillRect(const Rectf& rect, const Colorf& color, UiLayer layer = UiLayer::Fill);
		/** @brief Draws the outline of a rectangle, inside of it */
		void Frame(const Rectf& rect, const Colorf& color, float thickness, UiLayer layer = UiLayer::Overlay);
		/** @brief Fills a rounded rectangle */
		void RoundRect(const Rectf& rect, const Colorf& color, UiLayer layer = UiLayer::Fill);
		/** @brief Draws the border of a rounded rectangle */
		void RoundRectBorder(const Rectf& rect, const Colorf& color, UiLayer layer = UiLayer::Fill);
		/** @brief Draws a soft shadow of a rectangle */
		void Shadow(const Rectf& rect, float opacity = 1.0f);
		/** @brief Draws the ring around an element that has the keyboard focus */
		void FocusRing(const Rectf& rect);
		/** @brief Draws a panel, its shadow, background and border */
		void Panel(const Rectf& rect, bool shadow = true, const Colorf& fill = UiTheme::PanelFill, const Colorf& border = UiTheme::PanelBorderDim);
		/** @brief Draws a line, antialiased unless it's horizontal or vertical */
		void Line(Vector2f from, Vector2f to, float thickness, const Colorf& color, UiLayer layer = UiLayer::Overlay);
		/** @brief Draws an antialiased line through points, with mitered corners */
		void Polyline(ArrayView<const Vector2f> points, float thickness, const Colorf& color, UiLayer layer = UiLayer::Overlay);
		/** @brief Draws a filled circle */
		void Circle(Vector2f center, float radius, const Colorf& color, UiLayer layer = UiLayer::Content);
		/** @brief Draws an icon centered in a rectangle, at the size of an icon at the scale of the interface */
		void Icon(EditorIcon icon, const Rectf& rect, const Colorf& color, UiLayer layer = UiLayer::Content);

		/** @brief Returns the size of text relative to the usual size of the interface */
		float GetTextSize() const;
		/** @brief Makes text drawn and measured from now on larger or smaller than usual, 1 is the usual size */
		void SetTextSize(float size);
		/** @brief Returns the height of a line of text */
		float GetTextHeight() const;
		/** @brief Returns the width of a text */
		float MeasureText(StringView text) const;
		/**
		 * @brief Draws a text
		 *
		 * The color is a text color (see @ref UiTheme::Text), a color of anything else --- its alpha is above 0.5 ---
		 * is toned down, as it would glare otherwise.
		 */
		void Text(StringView text, Vector2f pos, const Colorf& color, Alignment align = Alignment::TopLeft, UiLayer layer = UiLayer::Text);
		/** @brief Draws a text in a rectangle, vertically centered */
		void TextInRect(StringView text, const Rectf& rect, const Colorf& color, Alignment align = Alignment::Left, UiLayer layer = UiLayer::Text);

		/** @brief Draws a part of a texture, rotated clockwise by @p angle in radians around the center of the rectangle */
		void Image(const Texture& texture, const Rectf& rect, const Vector4f& texCoords, const Colorf& color,
			std::int32_t paletteOffset = -1, UiLayer layer = UiLayer::Content, float angle = 0.0f);
		/**
		 * @brief Draws a tile of a level
		 *
		 * The tile ID is as the level file stores it and the flags as well, so it's drawn with the live palette.
		 * Returns `false` if the tile doesn't exist.
		 */
		bool Tile(Tiles::TileMap& tileMap, std::int32_t tileId, std::uint8_t flags, const Rectf& rect, float alpha = 1.0f, UiLayer layer = UiLayer::Content);
		/** @brief Draws a prepared render command, its depth has to be set already, see @ref GetDepth() */
		void Command(RenderCommand* command);

	private:
		UI::Canvas& _canvas;
		UiSkin& _skin;
		UI::Font* _font;
		float _scale;
		float _fontScale;
		std::int32_t _slot;
		SmallVector<Rectf, 8> _clips;
		SmallVector<float, 4> _opacities;
		float _opacity;
		Rendering::PolylineRenderer* _lines;

		void DrawNineSlice(const UiSkin::NineSlice& slice, const Rectf& rect, const Colorf& color, UiLayer layer, bool withMiddle = true);
		Colorf ApplyOpacity(const Colorf& color) const;
		// Cuts a segment to the clipping made smaller by an inset, returns false if nothing of it is left
		static bool ClipSegment(const Rectf& clip, float inset, Vector2f& from, Vector2f& to);
	};
}

#endif
