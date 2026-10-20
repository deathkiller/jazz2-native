#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/Annotations.h"
#include "../Events/EventCatalog.h"
#include "../UI/UiWidgets.h"

namespace Jazz2::Editor
{
	class IEditorContext;
	class LevelDocument;
	class UiSurface;
	struct EditorAction;
	struct UiMenuEntry;
	struct UiOverlayOptions;

	/** @brief Makes an item of a menu running an action, with its label, icon and shortcut */
	UiMenuEntry MakeMenuEntry(EditorAction& action);
	/** @brief Formats a whole number */
	String FormatInteger(std::int32_t value);
	/** @brief Returns the short name of a unit of a parameter of an event shown after its name, empty for none */
	StringView GetUnitSuffix(ParamUnit unit);
	/**
	 * @brief Adds a row with a label
	 *
	 * A form puts the labels in a column as wide as the longest of them, any other element gets a row with a label
	 * of @p labelWidth units.
	 */
	UiStack* AddLabeledRow(UiElement* parent, StringView label, StringView tooltip = {}, float labelWidth = 62.0f);
	/** @brief Returns `true` if all words of a search are in a text, whatever the case of the letters */
	bool MatchesSearch(StringView text, StringView search);
	/** @brief Returns the first parameter of an event of a type, or `nullptr` */
	const EventParamField* FindEventField(const EventDescriptor* descriptor, ParamType type);
	/** @brief Returns the flag of an event of a difficulty of the game, see @ref Formats::EventRecord::Easy, 0 for none */
	std::uint8_t GetDifficultyFlag(std::int32_t difficulty);
	/**
	 * @brief Returns the event the event properties show
	 *
	 * The selected one off the grid, the first selected one, or the first one to place. Returns `false` if there's
	 * none.
	 */
	bool GetPrimaryEvent(IEditorContext& session, Formats::EventRecord& event);
	/** @brief Returns `true` if a layer is locked in the editor, see @ref DocumentLayer::EditorLocked */
	bool IsLayerLocked(const LevelDocument& document, std::int32_t layer);
	/** @brief Returns `true` if a layer can be changed, shows why not otherwise */
	bool CheckLayerUnlocked(IEditorContext& session, std::int32_t layer);
	/** @brief Shows a dialog to choose a PNG image to open, from the places images are read from, see @ref EditorSession::GetImageRoots() */
	void ShowOpenImageDialog(IEditorContext& session, StringView title, Function<void(StringView)>&& onChosen);

	/**
	 * @brief Shows a dialog that isn't modal, only one of each
	 *
	 * The editor remembers the dialog under @p id until it's closed. Returns the dialog.
	 */
	UiElement* ShowSingleDialog(IEditorContext& session, const char* id, std::unique_ptr<UiElement> dialog, UiOverlayOptions&& options,
		Function<void()>&& onShownAgain = {});
	/**
	 * @brief Returns `true` if a dialog of @p id is shown already
	 *
	 * With @p close it's closed, otherwise the function it was shown with is called, see @ref ShowSingleDialog().
	 */
	bool ReuseSingleDialog(IEditorContext& session, const char* id, bool close = false);

	/**
		@brief Takes no room, only calls a function every frame
	*/
	class UiUpdateWatcher : public UiElement
	{
	public:
		/** @brief Creates a new instance */
		explicit UiUpdateWatcher(Function<void(float)>&& onUpdate);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Update(float timeMult) override;

	private:
		Function<void(float)> _onUpdate;
	};

	/**
		@brief Bookmarks and notes of a level, read again only when they change

		They're drawn every frame, reading them each time would make strings of all the notes over and over.
	*/
	class AnnotationCache
	{
	public:
		/** @brief Returns the bookmarks and notes of a level */
		const SmallVectorImpl<Annotation>& Get(const LevelDocument& document);
		/** @brief Returns a number that changes each time they're read again, see @ref Get() */
		std::uint32_t GetVersion() const {
			return _version;
		}

	private:
		// A copy of the chunk they were read from, and its tag
		SmallVector<std::uint8_t, 0> _data;
		std::uint32_t _tag = 0;
		SmallVector<Annotation, 0> _annotations;
		std::uint32_t _version = 0;
		bool _valid = false;
		// The frame they were compared in last, and what with, see Get()
		std::uint32_t _checkedFrame = UINT32_MAX;
		const LevelDocument* _document = nullptr;
		const Formats::ChunkRecord* _chunk = nullptr;
	};
}

#endif
