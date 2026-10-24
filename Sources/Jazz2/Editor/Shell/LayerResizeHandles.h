#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Tools/EditorTool.h"
#include "../UI/UiElement.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Handles on the frame of the layer being edited that resize the layer when they're dragged

		The edges and the corners of the frame are dragged by whole tiles, the opposite edge or corner stays where it
		is. The new frame and its size are shown while dragging, letting go resizes the layer as one step, as
		@ref ShowResizeLayerDialog() does --- a layer that repeats is repeated where it grows, the others get empty
		tiles. With <kbd>Ctrl</kbd> held, a handle moves the whole layer instead, by its offsets in pixels (by whole
		tiles with <kbd>Shift</kbd> too), as one step, but not the sprite layer, which has none. <kbd>Esc</kbd> cancels
		either.

		The handles are there only while the selection or the hand is the tool chosen and nothing is being done with
		it, so they never take a stroke of a tool that paints near the edge of a layer, and not on a locked or hidden
		layer. Of a layer that repeats, they're on the copy that is framed, see @ref EditorShell.
	*/
	class LayerResizeHandles : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @p getViewRect returns the part of the screen the level is seen in, @p getFrame where a layer is framed on
		 * the screen.
		 */
		LayerResizeHandles(IEditorContext& session, Function<Rectf()>&& getViewRect, Function<Rectf(const LayerView&)>&& getFrame);

		/** @brief Returns `true` while a handle is dragged */
		bool IsDragging() const {
			return _dragging;
		}

		void Draw(UiPainter& painter) override;
		UiElement* HitTest(Vector2f pos) override;
		bool AcceptsPointer() const override;
		bool YieldsToViewGestures() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		void OnPointerCancel() override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		/** @brief Takes the focus while a handle is dragged, so <kbd>Esc</kbd> cancels it */
		bool IsFocusable() const override;
		bool IsTabStop() const override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		enum Edge : std::uint8_t
		{
			EdgeLeft = 0x01,
			EdgeRight = 0x02,
			EdgeTop = 0x04,
			EdgeBottom = 0x08
		};

		IEditorContext& _session;
		// Only read the shell, called from the methods that only look too
		mutable Function<Rectf()> _getViewRect;
		mutable Function<Rectf(const LayerView&)> _getFrame;
		bool _dragging;
		// Edges dragged, a corner is two of them
		std::uint8_t _edges;
		std::int32_t _layer;
		// How far from the edge the handle was taken, so the edge doesn't jump to the pointer
		Vector2f _grab;
		Vector2i _startSize;
		Vector2i _size;
		// Where the tile (0, 0) of the layer goes, see LevelDocument::ResizeLayer()
		Vector2i _offset;
		// The layer is moved by its offsets instead of resized, it's moved as the pointer goes, from where it was pressed
		bool _moving = false;
		Vector2f _pressPos;
		Vector2f _startLayerOffset;

		// Whether the handles are there, see the class
		bool IsActive() const;
		// Whether a handle moves the layer instead of resizing it, Ctrl is held and the layer has offsets
		bool WantsMove(std::int32_t modifiers) const;
		// Sets the offsets of the layer being moved, merged into one step until the drag ends
		void SetLayerOffset(Vector2f offset);
		bool GetFrame(Rectf& frame, Rectf& viewRect) const;
		// The edges a handle under a position of the screen drags, 0 if there's none
		std::uint8_t FindEdges(Vector2f pos) const;
		float GetGrabDistance() const;
		void EndDrag();
	};
}

#endif
