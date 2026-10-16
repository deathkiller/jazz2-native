#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiWidgets.h"
#include "../../../nCine/Graphics/Texture.h"

#include <memory>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using nCine::Texture;
#endif

	/**
		@brief Shows a color, a translucent one over a checkerboard

		If it has a callback, it can be clicked.
	*/
	class UiColorSwatch : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		explicit UiColorSwatch(Function<Colorf()>&& color, Function<void()>&& onClick = {});

		/** @brief Returns the color shown */
		Function<Colorf()> ColorFunc;
		/** @brief Called when it's clicked */
		Function<void()> OnClick;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
	};

	/**
		@brief The 256 entries of a palette in a grid of 16 × 16, to pick one of them, or a range of them

		Colors of the palette are as @ref ContentResolver::GetPalettes() has them, red in the lowest byte. A range is
		picked by dragging or with <kbd>Shift</kbd> if @ref SelectionEndFunc is set.
	*/
	class UiPaletteGrid : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Usual size of an entry in units */
		static constexpr float DefaultCellSize = 9.0f;

		/**
		 * @brief Creates a new instance
		 *
		 * @p onPick gets the picked entry, and whether it extends the range picked before instead of starting a new one.
		 */
		UiPaletteGrid(Function<const std::uint32_t*()>&& palette, Function<std::int32_t()>&& selected, Function<void(std::int32_t, bool)>&& onPick);

		/** @brief Size of an entry in units */
		float CellSize;
		/** @brief Returns the other end of the picked range, ranges can't be picked if it isn't set */
		Function<std::int32_t()> SelectionEndFunc;
		/** @brief Returns `true` for an entry that is crossed out, such as one nothing uses, none is if it isn't set */
		Function<bool(std::int32_t)> IsCrossedFunc;

		/** @brief Shows it in a popup at a position, it's closed when an entry is picked */
		static void ShowPopup(UiSurface& surface, Vector2f pos, Function<const std::uint32_t*()>&& palette,
			Function<std::int32_t()>&& selected, Function<void(std::int32_t)>&& onPick);

		/** @brief Converts an entry of a palette to a color */
		static Colorf ToColor(std::uint32_t entry);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		mutable Function<const std::uint32_t*()> _palette;
		Function<std::int32_t()> _selected;
		Function<void(std::int32_t, bool)> _onPick;
		mutable String _tooltip;
		std::int32_t _dragIndex;

		std::int32_t GetIndexAt(Vector2f pos) const;
		float GetCellPixels() const;
	};

	/**
		@brief Picks a color by its saturation and value in a square, its hue in a bar next to it, and optionally alpha

		The color is changed as it's dragged, every change goes to the callback. The hue and saturation are kept while
		the color is gray or black, so they don't jump back to red when it's dragged out of there again.
	*/
	class UiColorPicker : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		UiColorPicker(Function<Colorf()>&& color, Function<void(const Colorf&)>&& onChange, bool withAlpha = false);
		~UiColorPicker() override;

		/**
		 * @brief Shows it in a popup at a position, with the color it had before, and its hex code
		 *
		 * It's closed by a click outside of it. Clicking the color it had before brings it back.
		 */
		static void ShowPopup(UiSurface& surface, Vector2f pos, Function<Colorf()>&& color, Function<void(const Colorf&)>&& onChange, bool withAlpha = false);

		/** @brief Converts a color to `#RRGGBB` */
		static String ToHex(const Colorf& color);
		/** @brief Converts `#RRGGBB` or `RRGGBB` to a color, returns `false` if it isn't one */
		static bool FromHex(StringView text, Colorf& color);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;

	private:
		enum class Part { None, Square, Hue, Alpha };

		Function<Colorf()> _color;
		Function<void(const Colorf&)> _onChange;
		bool _withAlpha;
		float _hue, _saturation, _value, _alpha;
		// The color as bytes when hue, saturation and value were last set, a different one came from elsewhere
		std::uint32_t _knownColor;
		Part _dragPart;
		std::unique_ptr<Texture> _square;
		std::unique_ptr<Texture> _hueBar;
		std::unique_ptr<Texture> _alphaBar;
		float _squareHue;

		void SyncFromColor();
		void ChangeAt(Vector2f pos);
		void EnsureTextures();
		Rectf GetSquareRect() const;
		Rectf GetHueRect() const;
		Rectf GetAlphaRect() const;
	};
}

#endif
