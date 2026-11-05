#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Skin.h"
#include "Theme.h"
#include "../Alignment.h"
#include "../Canvas.h"
#include "../Font.h"

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

#include <cmath>

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::UI::Windows
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::StringView;
	using UI::Alignment;
#endif

	/** @brief Layer of a slot, things of a later layer are drawn over those of an earlier one */
	enum class PaintLayer : std::uint8_t
	{
		Back,			/**< Shadows */
		Fill,			/**< Backgrounds of panels and controls */
		Content,		/**< Images, tiles and icons */
		Text,			/**< Text */
		Overlay,		/**< Highlights and outlines over the content */
		OverlayText		/**< Text over the highlights */
	};

	/**
		@brief Draws the interface

		All positions and sizes are in pixels of the screen. The interface is drawn in slots --- the docked panels in
		one, a floating window in another, a menu over them in yet another --- and a later slot is always drawn over
		an earlier one. Inside a slot, things are drawn in layers, see @ref PaintLayer.
	*/
	class Painter
	{
	public:
		/** @brief Number of depth values a slot takes */
		static constexpr std::int32_t SlotDepth = 8;
		/** @brief Depth of the first slot, the interface is drawn over everything else */
		static constexpr std::uint16_t BaseDepth = 0xE000;

		/** @brief Creates a new instance, @p lines draws slanted lines antialiased, they're rough without it */
		Painter(UI::Canvas& canvas, Skin& skin, UI::Font* font, float scale, Rendering::PolylineRenderer* lines = nullptr);

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
		std::uint16_t GetDepth(PaintLayer layer) const;

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
		void FillRect(const Rectf& rect, const Colorf& color, PaintLayer layer = PaintLayer::Fill);
		/** @brief Draws the outline of a rectangle, inside of it */
		void Frame(const Rectf& rect, const Colorf& color, float thickness, PaintLayer layer = PaintLayer::Overlay);
		/** @brief Fills a rounded rectangle */
		void RoundRect(const Rectf& rect, const Colorf& color, PaintLayer layer = PaintLayer::Fill);
		/** @brief Draws the border of a rounded rectangle */
		void RoundRectBorder(const Rectf& rect, const Colorf& color, PaintLayer layer = PaintLayer::Fill);
		/** @brief Draws a soft shadow of a rectangle */
		void Shadow(const Rectf& rect, float opacity = 1.0f);
		/** @brief Draws the ring around an element that has the keyboard focus */
		void FocusRing(const Rectf& rect);
		/**
		 * @brief Sets a blurred picture of what is behind the interface and the part of the screen it covers, `nullptr` for none
		 *
		 * Backgrounds drawn over it keep @p fillOpacity of their opacity, see @ref Theme::BackdropFillOpacity. Those
		 * drawn in a slot before @p firstSlot aren't drawn over it, they may be in the picture.
		 */
		void SetBackdrop(const Texture* texture, const Rectf& bounds, float fillOpacity, std::int32_t firstSlot = 0);
		/**
		 * @brief Draws the blurred picture behind a background of a color, see @ref SetBackdrop()
		 *
		 * Returns the color made more transparent if it was drawn, the color as it is otherwise --- there's no picture,
		 * the background is opaque or the rectangle isn't all inside of the picture. The corners of a rounded background
		 * are left out.
		 */
		Colorf Backdrop(const Rectf& rect, const Colorf& fill, bool rounded);
		/**
		 * @brief Draws into a target with more or fewer pixels than units, as a window on a display with another scale
		 *
		 * Everything is still laid out and drawn in units of the interface, the skin and the font given to the painter
		 * have to be made for the scale of the interface times @p raster though, so they're drawn a pixel of them to a
		 * pixel of the target. Positions are put on whole pixels of the target.
		 */
		void SetRaster(float raster);
		/** @brief Returns the pixels of the target for a pixel of the interface, see @ref SetRaster() */
		float GetRaster() const {
			return _raster;
		}
		/**
		 * @brief Sets the bounds of a window of the system that has square corners, `Rectf()` for none
		 *
		 * A panel that fills it gets square corners too and no shadow, rounded ones would leave the corners of the
		 * window showing the background, see @ref Panel().
		 */
		void SetSquareWindow(const Rectf& bounds) {
			_squareWindow = bounds;
		}
		/** @brief Draws a panel, its shadow, background and border, with square corners unless @p rounded */
		void Panel(const Rectf& rect, bool shadow = true, const Colorf& fill = Theme::PanelFill, const Colorf& border = Theme::PanelBorderDim, bool rounded = true);
		/** @brief Draws a line, antialiased unless it's horizontal or vertical */
		void Line(Vector2f from, Vector2f to, float thickness, const Colorf& color, PaintLayer layer = PaintLayer::Overlay);
		/** @brief Draws an antialiased line through points, with mitered corners */
		void Polyline(ArrayView<const Vector2f> points, float thickness, const Colorf& color, PaintLayer layer = PaintLayer::Overlay);
		/** @brief Draws a filled circle */
		void Circle(Vector2f center, float radius, const Colorf& color, PaintLayer layer = PaintLayer::Content);
		/** @brief Draws an icon centered in a rectangle, at the size of an icon at the scale of the interface */
		void Icon(IconId icon, const Rectf& rect, const Colorf& color, PaintLayer layer = PaintLayer::Content);

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
		 * The color is a text color (see @ref Theme::Text), a color of anything else --- its alpha is above 0.5 ---
		 * is toned down, as it would glare otherwise.
		 */
		void Text(StringView text, Vector2f pos, const Colorf& color, Alignment align = Alignment::TopLeft, PaintLayer layer = PaintLayer::Text);
		/** @brief Draws a text in a rectangle, vertically centered */
		void TextInRect(StringView text, const Rectf& rect, const Colorf& color, Alignment align = Alignment::Left, PaintLayer layer = PaintLayer::Text);

		/** @brief Draws a part of a texture, rotated clockwise by @p angle in radians around the center of the rectangle */
		void Image(const Texture& texture, const Rectf& rect, const Vector4f& texCoords, const Colorf& color,
			std::int32_t paletteOffset = -1, PaintLayer layer = PaintLayer::Content, float angle = 0.0f, UI::BlendMode blend = UI::BlendMode::Normal);
		/** @brief Draws a prepared render command, its depth has to be set already, see @ref GetDepth() */
		void Command(RenderCommand* command);

	private:
		UI::Canvas& _canvas;
		Skin& _skin;
		UI::Font* _font;
		float _scale;
		float _fontScale;
		std::int32_t _slot;
		SmallVector<Rectf, 8> _clips;
		SmallVector<float, 4> _opacities;
		float _opacity;
		Rendering::PolylineRenderer* _lines;
		const Texture* _backdrop;
		Rectf _backdropBounds;
		float _backdropFillOpacity;
		std::int32_t _backdropFirstSlot;
		Rectf _squareWindow;
		float _raster = 1.0f;
		// The scale of the text at the scale the skin and the font are made for
		float _textBase;

		void DrawBackdropPart(const Rectf& rect);
		// Puts the edges of a rectangle on whole pixels of the target
		Rectf SnapRect(const Rectf& rect) const;
		// Puts a position on a whole pixel of the target
		float Snap(float value) const {
			return std::round(value * _raster) / _raster;
		}
		void DrawNineSlice(const Skin::NineSlice& slice, const Rectf& rect, const Colorf& color, PaintLayer layer, bool withMiddle = true);
		Colorf ApplyOpacity(const Colorf& color) const;
		// Cuts a segment to the clipping made smaller by an inset, returns false if nothing of it is left
		static bool ClipSegment(const Rectf& clip, float inset, Vector2f& from, Vector2f& to);
	};
}

#endif
