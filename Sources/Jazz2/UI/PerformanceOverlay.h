#pragma once

#include "../../Main.h"
#include "Canvas.h"

#include "../../nCine/Graphics/Camera.h"

#include <memory>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class Texture;
	class Viewport;
}

namespace Jazz2::UI
{
	class Font;

	/**
		@brief Table of the detailed performance metrics

		The overlay @ref PerformanceMetricsLevel::Detailed shows. Its numbers change only when
		@ref nCine::FrameStatistics publishes a new snapshot, twice a second, so that is the only time the table is
		built: @ref Update() notices the snapshot and fills in the rows every screen has - the frame time, its phases,
		what the graphics backend reports and the memory - the caller adds rows of its own through @ref AddRow(), and
		@ref Build() measures them and has them rendered into a texture of their own, in an off-screen pass that
		runs in that one frame only. Every frame @ref Draw() then costs a single textured quad, where drawing the text
		itself was a render command per character - some 150 of them, which on the weakest consoles made the overlay
		one of the more expensive things on the screen.

		The texture is sized for the table, rounded up, and only ever grows, so the table changing by a digit does
		not reallocate it. A column is as wide as the widest text it held over the last few seconds rather than as
		its text is now: sized to the current text, the whole table would jump sideways every time a value gains
		or loses a digit, while a column that never narrowed would stay as wide as the frame time of the hitch
		that loaded the level. Where the off-screen pass cannot be set up, the table is drawn directly instead.
	*/
	class PerformanceOverlay
	{
	public:
		/** @brief Maximum number of rows, further ones are dropped */
		static constexpr std::int32_t MaxRows = 24;

		PerformanceOverlay();
		~PerformanceOverlay();

		PerformanceOverlay(const PerformanceOverlay&) = delete;
		PerformanceOverlay& operator=(const PerformanceOverlay&) = delete;

		/**
		 * @brief Prepares the table for the frame, to be called every frame before the scene is visited
		 *
		 * Returns `true` when a new snapshot has been published since the table was built. The rows then hold
		 * the ones describing the frame (see @ref nCine::FrameStatistics), the caller adds its own and finishes
		 * with @ref Build(). The table is not shown until the first snapshot is.
		 */
		bool Update();
		/** @brief Adds a row, between @ref Update() and @ref Build() */
		void AddRow(StringView label, StringView value);
		/**
		 * @brief Lays the rows out and schedules rendering them
		 *
		 * @param font			Font to draw with
		 * @param maxHeight		Height the table may take; the rows that do not fit continue in another block beside
		 *						the first, and the blocks read from left to right
		 */
		void Build(Font* font, float maxHeight);
		/**
		 * @brief Draws the table with its top right corner at the given point
		 *
		 * Draws nothing until the table has been rendered for the first time.
		 */
		void Draw(Canvas* canvas, float right, float top, std::uint16_t z);
		/**
		 * @brief Frees the texture and everything else the table holds
		 *
		 * For when the table is not going to be shown for a while; the next @ref Update() builds it again. Costs
		 * nothing if there is nothing to free.
		 */
		void Release();

	private:
		static constexpr std::size_t MaxLabelLength = 16;
		static constexpr std::size_t MaxValueLength = 24;
		static constexpr std::int32_t MaxBlocks = 3;

		struct Row
		{
			char Label[MaxLabelLength];
			char Value[MaxValueLength];
			std::uint8_t LabelLength;
			std::uint8_t ValueLength;
		};

		// The widest text of a column over two windows of builds, the current one and the one before it
		struct ColumnWidth
		{
			float Previous;
			float Current;

			float Get() const {
				return (Previous > Current ? Previous : Current);
			}
		};

		// Root of the off-screen pass, which draws the rows into the texture
		class TableCanvas : public Canvas
		{
		public:
			explicit TableCanvas(PerformanceOverlay* owner);

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			PerformanceOverlay* _owner;
		};

		Row _rows[MaxRows];
		std::int32_t _rowCount;
		// Snapshot the rows were made from, valid once one has been seen
		std::uint32_t _snapshotSequence;

		// Layout of the table, measured by Build()
		Font* _font;
		float _lineHeight;
		std::int32_t _blockCount;
		std::int32_t _rowsPerBlock;
		ColumnWidth _labelWidths[MaxBlocks];
		ColumnWidth _valueWidths[MaxBlocks];
		std::int32_t _buildsInWindow;
		Vector2i _tableSize;

		// Size of the table the texture holds, which trails the layout until the pass has rendered it
		Vector2i _contentSize;
		bool _snapshotSeen;
		// The pass is in the chain, or has to be put back into it, to render the layout
		bool _renderPending;
		// Cleared for good where the pass cannot be set up, the rows are then drawn directly
		bool _useTexture;

		// The viewport goes last, so it is destroyed first - it refers to all the others
		Camera _camera;
		std::unique_ptr<TableCanvas> _canvas;
		std::unique_ptr<Texture> _target;
		std::unique_ptr<Viewport> _view;

		void AddFrameStatistics();
		bool EnsureTarget(Vector2i size);
		void DisableTexture();
		void ScheduleRender();
		void RemoveFromChain();
		void DrawRows(Canvas* canvas, Vector2f origin, std::uint16_t z) const;
		float GetBlockWidth(std::int32_t block) const;
	};
}
