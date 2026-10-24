#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ToolHost.h"
#include "../Common/LayerView.h"
#include "../Common/TileStamp.h"
#include "../Document/CommandHistory.h"
#include "../UI/UiPainter.h"
#include "../../../nCine/Input/InputEvents.h"

#include <Containers/Function.h>

#include <memory>

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
#endif

	class UiDialog;
	class UiElement;

	/** @brief Tool of the level editor */
	enum class EditorToolType : std::uint8_t
	{
		Brush,
		Eraser,
		Rectangle,
		Fill,
		Eyedropper,
		Select,
		Wand,
		Hand,
		Event,
		Measure,
		Path,
		Smart,
		Ground,
		TilePixels,
		Shape,

		Count
	};

	/** @brief Modifier of tiles the tile tools paint, see @ref Formats::TileRecord::ModifierMask */
	enum class TileModifierMode : std::uint8_t
	{
		Keep,			/**< As the tiles of the stamp have it */
		Normal,			/**< Neither translucent, invisible nor heat */
		Translucent,	/**< Translucent */
		Invisible,		/**< Invisible, solid but not drawn */
		Heat			/**< Rows of pixels drawn wavily, as if seen through heat (see @ref Formats::TileRecord::ModifierHeat) */
	};

	/** @brief Shape the shape tool draws */
	enum class ToolShape : std::uint8_t
	{
		Line,			/**< A straight line */
		Rectangle,		/**< The outline of a rectangle */
		Ellipse,		/**< The outline of an ellipse */
		FilledEllipse	/**< A filled ellipse */
	};

	/** @brief How the event tool places the event of the stamp, or that it only selects events */
	enum class EventPlaceMode : std::uint8_t
	{
		Paint,			/**< Where it's clicked or dragged */
		Rectangle,		/**< On every empty tile of a rectangle */
		Fill,			/**< On the empty tiles of an area joined to the tile clicked, of the same kind of tiles, solid or not */
		Select			/**< Nowhere, a drag selects the events in a rectangle as the select tool selects tiles */
	};

	/**
		@brief Options shared by the tools, kept with the settings of the editor
	*/
	struct ToolSettings
	{
		/** @brief Jumps the measure tool shows, see @ref MeasureArcs */
		enum MeasureArcs : std::uint8_t
		{
			ArcWalk = 0x01,
			ArcDash = 0x02,
			ArcDoubleJump = 0x04,
			ArcCopter = 0x08,
			// 0x10 was the jump of Lori, a value kept from before is masked off by ArcAll
			ArcAll = 0x0F
		};

		/** @brief Modifier the tile tools paint the tiles with */
		TileModifierMode Modifier = TileModifierMode::Keep;
		/** @brief Whether the tile tools change only the modifier of the tiles there, not the tiles */
		bool ModifierOnly = false;
		/** @brief Whether the tiles of a stamp painted at random are flipped horizontally at random too */
		bool RandomFlipX = false;
		/** @brief Whether the tiles of a stamp painted at random are flipped vertically at random too */
		bool RandomFlipY = false;
		/** @brief Whether the fill and the magic wand go on across the edges of a layer that repeats */
		bool AcrossSeams = true;
		/** @brief Whether the fill and the magic wand take a tile flipped as the same tile */
		bool IgnoreFlips = false;
		/** @brief Whether the fill and the rectangle paint only tiles that are empty */
		bool OnlyEmpty = false;
		/** @brief Whether the magic wand selects all tiles of the layer like the one clicked, not only those joined to it */
		bool WandGlobal = false;
		/** @brief Shape the shape tool draws */
		ToolShape Shape = ToolShape::Line;
		/** @brief How the event tool places events */
		EventPlaceMode EventMode = EventPlaceMode::Paint;
		/** @brief Jumps the measure tool shows, a combination of @ref MeasureArcs */
		std::uint8_t ShownArcs = ArcWalk | ArcDash | ArcDoubleJump;
		/** @brief Whether the jumps of the measure tool stop at solid tiles */
		bool ArcCollisions = true;
		/** @brief Whether mirroring swaps the teams of events that belong to one */
		bool MirrorSwapTeams = false;
		/** @brief Called when the options are changed by a tool, so they're kept */
		Function<void()> OnChanged;

		/** @brief Tells that the options were changed */
		void NotifyChanged() {
			if (OnChanged) {
				OnChanged();
			}
		}

		/** @brief Makes the event tool place events again if it only selects them, as an event to place was picked */
		void PlaceEventsAgain() {
			if (EventMode == EventPlaceMode::Select) {
				EventMode = EventPlaceMode::Paint;
				NotifyChanged();
			}
		}

		/**
		 * @brief Returns the tile a stamp paints at a position of a layer with these options, `false` if it paints none there
		 *
		 * @p existing is the tile there now, @p options whether the options apply, not for erasing, and @p onlyEmpty
		 * whether only empty tiles are painted. The empty tiles of a stamp whose empty tiles are transparent (see
		 * @ref TileStamp::EmptyIsTransparent) are painted only with @p emptyToo, as with <kbd>Shift</kbd> held.
		 * @p stampIndex, if given, gets the index of the tile of the stamp that is painted, for its event.
		 */
		bool ResolveTile(const TileStamp& stamp, Vector2i position, Vector2i origin, const TileRef& existing, bool options,
			bool onlyEmpty, TileRef& result, bool emptyToo = false, std::size_t* stampIndex = nullptr) const;
	};

	/** @brief A value of an option of a tool, or a toggle, see @ref ToolOptionsBuilder */
	struct ToolOptionItem
	{
		/** @brief Short text, shown when it has no icon and in lists */
		String Label;
		/** @brief Icon, or @ref EditorIcon::None to show the label, the icon of the action isn't taken, the actions of a choice often share one */
		EditorIcon Icon = EditorIcon::None;
		/** @brief Action of the editor that chooses or toggles it and tells whether it's chosen, or `nullptr` for one the tool keeps */
		const char* ActionId = nullptr;
		/** @brief Tooltip, the label of the action with its shortcut if empty, or the label */
		String Tooltip;
	};

	/**
		@brief Makes the options of a tool, shown in the toolbar while the tool is chosen, see @ref EditorTool::BuildOptions()

		Options are compact controls in a row. Where the row hasn't room for all of them, choices and groups of toggles
		turn into dropdowns, and those that still don't fit are found behind a button at its end. Each option reads its
		value whenever it's drawn, so a value changed by a shortcut is shown right away. Options kept by actions of the
		editor are left out for those who aren't experts if the actions are only for experts, unless the tool is too.
	*/
	class ToolOptionsBuilder
	{
	public:
		virtual ~ToolOptionsBuilder() = default;

		/**
		 * @brief Adds an action of the editor
		 *
		 * A toggle if the action can be checked, a button otherwise. It shows its icon, or @p icon, or @p label if it has
		 * none, or the label of the action. The tooltip is the label of the action with its shortcut.
		 */
		virtual void AddAction(const char* actionId, StringView label = {}, EditorIcon icon = EditorIcon::None) = 0;
		/** @brief Adds a choice of actions of the editor, the one checked is chosen, as buttons side by side or as a dropdown */
		virtual void AddChoice(StringView label, ArrayView<const ToolOptionItem> items, bool preferDropdown = false) = 0;
		/** @brief Adds a choice the tool keeps, @p getter returns the index of the item chosen, @p setter chooses one */
		virtual void AddChoice(StringView label, ArrayView<const ToolOptionItem> items, Function<std::int32_t()>&& getter,
			Function<void(std::int32_t)>&& setter, bool preferDropdown = false) = 0;
		/** @brief Adds actions of the editor that are toggled each by itself, as buttons side by side or as a dropdown */
		virtual void AddToggles(StringView label, ArrayView<const ToolOptionItem> items) = 0;
		/** @brief Adds a toggle the tool keeps */
		virtual void AddToggle(const ToolOptionItem& item, Function<bool()>&& getter, Function<void(bool)>&& setter) = 0;
		/** @brief Adds a number the tool keeps, @p setter gets whether the value is final or only dragged */
		virtual void AddNumber(StringView label, StringView tooltip, float minValue, float maxValue, float step,
			Function<float()>&& getter, Function<void(float, bool)>&& setter) = 0;
		/** @brief Starts another group of options */
		virtual void AddSeparator() = 0;
	};

	/**
		@brief Tool used in the view of the level editor
	*/
	class EditorTool
	{
	public:
		/** @brief Creates a new instance */
		explicit EditorTool(IToolHost& host);
		virtual ~EditorTool();

		/** @brief Returns the type */
		virtual EditorToolType GetType() const = 0;

		/** @brief Called when the tool is chosen */
		virtual void OnActivate();
		/** @brief Called when another tool is chosen, anything in progress is cancelled */
		virtual void OnDeactivate();
		/** @brief Called when the pointer is pressed over the view */
		virtual void OnPointerDown(const ToolPointer& pointer);
		/** @brief Called when the pointer moves over the view, pressed or not */
		virtual void OnPointerMove(const ToolPointer& pointer);
		/** @brief Called when the pointer is released */
		virtual void OnPointerUp(const ToolPointer& pointer);
		/** @brief Called when a key is pressed over the view, returns `true` if the tool takes it, no action runs by it then */
		virtual bool OnKeyPressed(const KeyboardEvent& event);
		/** @brief Called when a key is released, whatever got it when it was pressed */
		virtual void OnKeyReleased(const KeyboardEvent& event);
		/** @brief Cancels what the tool is doing */
		virtual void Cancel();
		/** @brief Returns `true` if the tool is in the middle of something */
		virtual bool IsBusy() const;
		/**
		 * @brief Returns `true` if the tool follows the pointer pressed, another tool can't be taken for a while then
		 *
		 * A tool in the middle of something clicked point by point, as a path, doesn't, the hand can move the view
		 * between the clicks. It's @ref IsBusy() by default.
		 */
		virtual bool HasPointerCapture() const;
		/** @brief Draws what the tool shows over the view */
		virtual void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid);
		/** @brief Returns a hint of how the tool is used */
		virtual StringView GetHint() const;
		/** @brief Makes the options of the tool shown in the toolbar while it's chosen, none by default */
		virtual void BuildOptions(ToolOptionsBuilder& builder);

		/** @brief Gives the tool the options shared by the tools, see @ref ToolManager::GetSettings() */
		void SetSettings(ToolSettings* settings) {
			_settings = settings;
		}

	protected:
		IToolHost& _host;
		ToolSettings* _settings;

		/** @brief Returns the options shared by the tools */
		const ToolSettings& GetSettings() const;

		/** @brief Draws the outline of cells of a layer on the screen */
		void DrawCellsOutline(UiPainter& painter, const LayerView& view, Vector2i from, Vector2i to, const Colorf& color);
		/**
		 * @brief Draws a stamp on the screen, its cell at the anchor at a cell of a layer
		 *
		 * Its events are drawn too while the sprite layer is edited. Empty tiles that are transparent (see
		 * @ref TileStamp::EmptyIsTransparent) are left out unless @p emptyToo, as they're painted.
		 */
		void DrawStampGhost(UiPainter& painter, const LayerView& view, const TileStamp& stamp, Vector2i cell, Vector2i anchor, float alpha,
			bool emptyToo = false);

		/** @brief Adds the options of the stamp to the options of a tool --- flipping, turning and painting its tiles at random */
		static void AddStampOptions(ToolOptionsBuilder& builder);
		/** @brief Adds the options of the modifier the tiles are painted with to the options of a tool */
		static void AddModifierOptions(ToolOptionsBuilder& builder);
		/** @brief Adds the options of finding an area of the same tiles, as the fill and the magic wand do, to the options of a tool */
		static void AddAreaOptions(ToolOptionsBuilder& builder);
	};

	/**
		@brief Keeps the tools and which of them is used

		A tool can be chosen for a while only, as the hand while Space is held, after which the one used before
		comes back.
	*/
	class ToolManager
	{
	public:
		/** @brief Creates all tools */
		explicit ToolManager(IToolHost& host);
		~ToolManager();

		/** @brief Returns the tool in use */
		EditorTool* GetActive() const;
		/** @brief Returns the type of the tool in use */
		EditorToolType GetActiveType() const {
			return _active;
		}
		/** @brief Returns the type of the tool chosen, not just for a while */
		EditorToolType GetChosenType() const {
			return _chosen;
		}
		/** @brief Chooses a tool */
		void SetActive(EditorToolType type);
		/** @brief Uses a tool for a while */
		void BeginTemporary(EditorToolType type);
		/** @brief Returns to the tool used before the one used for a while */
		void EndTemporary(EditorToolType type);
		/** @brief Returns a tool */
		EditorTool* Get(EditorToolType type) const;
		/** @brief Returns the options shared by the tools */
		ToolSettings& GetSettings() {
			return _settings;
		}

		/** @brief Called whenever a tool is chosen by @ref SetActive(), the same one again too, not for a tool used for a while */
		Function<void(EditorToolType)> OnChosen;

	private:
		ToolSettings _settings;
		std::unique_ptr<EditorTool> _tools[std::int32_t(EditorToolType::Count)];
		EditorToolType _active;
		EditorToolType _chosen;
		bool _temporary;
	};
}

#endif
