#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "VirtualList.h"
#include "Widgets.h"

namespace Jazz2::UI::Windows
{
	class Surface;

	/**
		@brief Window shown over the interface with a title, content and buttons

		The window is moved by dragging its title, the cross next to the title cancels it as Escape does. A window
		that is @ref Resizable is resized by its edges and the corner with the dots.
	*/
	class Dialog : public Panel
	{
		DEATH_RUNTIME_OBJECT(Panel);

	public:
		/**
		 * @brief Creates a new instance
		 *
		 * With @p scrollContent, a dialog taller than the screen is made as tall as the screen and its content scrolls,
		 * the title and the buttons stay. It's left to the dialog, as a list in the content that has no height of its
		 * own would be as tall as all of its rows then, and the whole content would scroll instead of the list.
		 */
		explicit Dialog(StringView title, bool scrollContent = false);

		/** @brief Returns the area for the content */
		Stack* GetContent() const {
			return _content;
		}
		/**
		 * @brief Adds a button
		 *
		 * Enter chooses the first accented button, or the first one if none is accented and @ref OnCancel isn't set.
		 * Escape and the cross choose the last one, it should be the one that closes the dialog, unless @ref OnCancel
		 * is set. A window closed only by its cross has no button that closes it --- with no buttons at all, Escape
		 * and the cross close it, and the row of buttons isn't shown.
		 */
		Button* AddButton(StringView text, Function<void()>&& onClick, bool accent = false);
		/** @brief Closes the dialog */
		void Close();
		/** @brief Cancels the dialog as Escape does, by @ref OnCancel, by the last button, or closes it if it has none */
		void Cancel();

		/**
		 * @brief Called instead of the last button when the dialog is cancelled by Escape or the cross
		 *
		 * A window whose buttons do other things than closing it sets it, Enter then runs only an accented button.
		 */
		Function<void()> OnCancel;
		/**
		 * @brief Part of the height of the screen the dialog takes at most, 1 for all of it
		 *
		 * A list in the content that should use the room is given @ref Element::Flex and no height of its own, it
		 * gets what's left then, and scrolls if it's longer.
		 */
		float MaxScreenHeightFraction = 1.0f;
		/**
		 * @brief Part of the width of the screen the dialog takes if its content is narrower, 0 for as wide as its content
		 *
		 * A dialog is never narrower than its content, unless the screen is.
		 */
		float MinScreenWidthFraction = 0.0f;
		/** @brief Width in units @ref MinScreenWidthFraction makes the dialog at most, 0 for no limit, its content can still make it wider */
		float MaxStretchedWidth = 0.0f;
		/**
		 * @brief Whether the window can be resized by its edges
		 *
		 * Its size is then its @ref Width and @ref Height, set by the resizing, the size it starts with if they're set
		 * before, and the content should fill it by @ref Element::Flex.
		 */
		bool Resizable = false;
		/** @brief Smallest size in units the window can be resized to */
		Vector2f MinResizeSize = Vector2f(160.0f, 120.0f);

		StringView GetTitle() const override;
		Vector2f Measure(const Context& context, Vector2f available) override;
		void Arrange(const Context& context, const Rectf& bounds) override;
		void Draw(Painter& painter) override;
		void Update(float timeMult) override;
		Element* HitTest(Vector2f pos) override;
		/** @brief With the edges a window that is @ref Resizable is grabbed by, which reach a little outside of it */
		Rectf GetHitBounds() const override;
		bool OnPointerDown(const PointerEvent& event) override;
		void OnPointerMove(const PointerEvent& event) override;
		void OnPointerUp(const PointerEvent& event, bool inside) override;
		bool OnKeyPressed(const KeyboardEvent& event) override;

	protected:
		DragBar* _titleBar;
		String _title;
		// The cross in the title, it's easier to hit than its size, see HitTest()
		Button* _closeButton;
		Stack* _content;
		Stack* _buttons;
		SmallVector<Button*, 4> _buttonList;
		// The content is in it if it scrolls, see Dialog()
		ScrollView* _contentScroll = nullptr;

	private:
		Vector2f _dragOrigin;
		float _scale = 1.0f;
		// Edges being dragged, and those pointed at
		std::uint8_t _resizeEdges = 0;
		std::uint8_t _hoverEdges = 0;
		Vector2f _resizeStart;
		Rectf _resizeStartBounds;

		// Returns the edges of a resizable window at a position, a corner is two of them
		std::uint8_t GetEdgesAt(Vector2f pos) const;
	};

	/**
		@brief Shows a message with a choice of buttons
	*/
	struct MessageDialog
	{
		/**
		 * @brief Shows a message, the callback gets the index of the button chosen
		 *
		 * It gets -1 if the message was closed by Escape or the cross instead --- no button was chosen, so nothing
		 * should be done that can't be done later, nothing should be discarded.
		 */
		static Dialog* Show(Surface& surface, StringView title, StringView text, std::initializer_list<StringView> buttons,
			Function<void(std::int32_t)>&& callback);
	};

	/** @brief What a file dialog is for */
	enum class FileDialogMode : std::uint8_t
	{
		Open,			/**< Opening an existing file */
		Save			/**< Saving a file, a new name can be typed */
	};

	/** @brief Place where files are looked for */
	struct FileRoot
	{
		/** @brief Name shown */
		String Name;
		/** @brief Path of the directory */
		String Path;
		/** @brief Whether files can be saved there */
		bool Writable;
		/** @brief Returns the name shown for a file or a folder in it instead of its own, or an empty string, optional */
		String (*LabelFunc)(StringView path, bool isDirectory) = nullptr;
	};

	/** @brief How a file is shown in the list of a file dialog, see @ref FileDialog::KindFunc */
	struct FileKind
	{
		/** @brief Icon */
		IconId Icon = StockIcon::File;
		/** @brief Color of the icon */
		Colorf Color = Theme::IconColor;
		/** @brief Tooltip of the row, optional */
		String Description;
	};

	/** @brief Row at the top of the list of a file dialog that stands for a value instead of a file, see @ref FileDialog::SetSpecialEntries() */
	struct FileSpecialEntry
	{
		/** @brief Name shown */
		String Label;
		/** @brief Value chosen, shown after the name */
		String Value;
		/** @brief Icon */
		IconId Icon = StockIcon::None;
		/** @brief What it does, shown next to the list and as the tooltip */
		String Description;
	};

	/** @brief What a file dialog shows about the selected file next to the list, see @ref FileDialog::InfoFunc */
	struct FileInfo
	{
		/** @brief Row of a label and its value */
		struct Row
		{
			/** @brief Label, the value takes the whole row without it */
			String Label;
			/** @brief Value */
			String Value;
			/** @brief Color of the value, a text color */
			Colorf Color = Theme::Text;
		};

		/** @brief Picture, RGBA row by row, empty for none */
		SmallVector<std::uint8_t, 0> Rgba;
		/** @brief Size of the picture in pixels */
		Vector2i ImageSize;
		/** @brief Rows below the picture */
		SmallVector<Row, 8> Rows;
		/** @brief Whether @ref Kind replaces how the file is shown in the list, as read more thoroughly */
		bool HasKind = false;
		/** @brief How the file is shown in the list, with @ref HasKind */
		FileKind Kind;
	};

	/**
		@brief Picks a file to open or to save to

		Only files with the extension are listed, several can be separated by `;`, e.g. `".it;.xm"`. The first one is
		added to a name typed without it. If it's empty, all files are listed. When opening, a file of the initial name
		is selected.

		On a desktop, the whole computer can be browsed too, as the last place --- the drives on Windows, `/` elsewhere ---
		unless the file has to be in one of the places, see @p anywhere. A path can be typed into the row above the list.
	*/
	class FileDialog : public Dialog
	{
		DEATH_RUNTIME_OBJECT(Dialog);

	public:
		/**
		 * @brief Creates a new instance
		 *
		 * If @p anywhere is `false`, files only of the places can be chosen, for a file that is referred to by its path
		 * in one of them.
		 */
		FileDialog(StringView title, FileDialogMode mode, SmallVectorImpl<FileRoot>&& roots, StringView extension,
			StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen, bool anywhere = true);

		/** @brief Whether files and folders hidden by the system, or named with a dot first, are listed, until the application is closed */
		static inline bool ShowHiddenFiles = false;

		/**
		 * @brief Reads what is shown about a file next to the list, its picture and details, returns `false` if there's nothing
		 *
		 * Asked once a file stays selected for a moment.
		 */
		Function<bool(StringView path, FileInfo& info)> InfoFunc;
		/**
		 * @brief Tells how a file is shown in the list, returns `false` to show it as any other file
		 *
		 * Asked once for each file as its row is first seen, only for a few of them in a frame, so it may read the start of the file.
		 */
		Function<bool(StringView path, FileKind& kind)> KindFunc;
		/**
		 * @brief Whether @ref KindFunc and @ref InfoFunc may run on a thread of their own
		 *
		 * They must touch nothing the user interface uses then. The rows show a dim icon until it's known, and the
		 * details of the selected file appear once they're read. Without threads they're asked as without it.
		 */
		bool Asynchronous = false;
		/**
		 * @brief Asked before a file is chosen, while the dialog stays open, the file is chosen once it calls `proceed`
		 *
		 * The dialog stays open if it doesn't, e.g. to ask whether to overwrite a file, so another name can be entered.
		 */
		Function<void(StringView path, Function<void()>&& proceed)> ConfirmFunc;

		~FileDialog() override;

		/**
		 * @brief Lists values that aren't files at the top of the list in every folder, only when opening
		 *
		 * The entry of @p selectedValue is selected, @p onChosen gets the value of the one chosen instead of a file.
		 */
		void SetSpecialEntries(SmallVectorImpl<FileSpecialEntry>&& entries, StringView selectedValue, Function<void(StringView)>&& onChosen);

		/** @brief Shows a file dialog */
		static FileDialog* Show(Surface& surface, StringView title, FileDialogMode mode, SmallVectorImpl<FileRoot>&& roots,
			StringView extension, StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen, bool anywhere = true);

		Vector2f Measure(const Context& context, Vector2f available) override;
		void Update(float timeMult) override;

	private:
		struct Entry
		{
			String Name;
			bool IsDirectory;
			// Shown instead of the name if it's not empty, see FileRoot::LabelFunc
			String Label;
			// KindFunc is asked once the row is seen, on the worker if it's Asynchronous
			bool KindKnown = false;
			bool KindPending = false;
			FileKind Kind;
			// Index in _special, -1 for a file or a directory
			std::int32_t Special = -1;
		};

		FileDialogMode _mode;
		SmallVector<FileRoot, 4> _roots;
		std::int32_t _rootIndex;
		// Index of the place of the whole computer, -1 if it has none; its path is empty on Windows, the drives are
		// listed there
		std::int32_t _computerRoot;
		String _extension;
		String _directory;
		SmallVector<Entry, 0> _entries;
		std::int32_t _selected;
		ScrollView* _list;
		VirtualList* _rows;
		TextBox* _nameBox;
		Function<void(StringView)> _onChosen;
		String _pendingNavigation;
		bool _navigationPending = false;
		String _pendingTyped;
		// The picture and details of the selected file, see InfoFunc, and the file it waits to show
		class FileDetails;
		FileDetails* _preview;
		String _previewPath;
		float _previewDelay = 0.0f;
		// Rows whose KindFunc may still be asked in this frame
		std::int32_t _kindBudget = 0;
		SmallVector<FileSpecialEntry, 2> _special;
		Function<void(StringView)> _onSpecialChosen;
		// Results of the worker of an older listing or selection are dropped
		std::uint32_t _listGeneration = 0;
		std::uint32_t _infoGeneration = 0;
		// Reads the files off the user interface with Asynchronous, last so it's stopped before the rest goes
		class Worker;
		std::unique_ptr<Worker> _worker;

		void UpdatePreview(float timeMult);
		void ShowInfo(FileInfo&& info);
		void ResolveKind(std::int32_t index);
		void TakeWorkerResults();

		void Navigate(StringView directory);
		// Goes to a typed path, a directory or the directory of a file, which is chosen then
		void NavigateToTyped(StringView text);
		bool MatchesExtension(StringView name) const;
		void Choose(std::int32_t index, bool confirm);
		void Confirm();
		void Choose(StringView path);
		bool IsWritable() const;
		String GetRelativeDirectory() const;
		// Whether a path is in a place, the place of the whole computer has every path
		bool IsInRoot(StringView path, std::int32_t rootIndex) const;
		// The innermost place a path is in, -1 if none
		std::int32_t FindRoot(StringView path) const;
		// Whether a path is a root or a directory in it
		static bool IsInRoot(StringView path, StringView root);
	};
}

#endif
