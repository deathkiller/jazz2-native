#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiVirtualList.h"
#include "UiWidgets.h"

namespace Jazz2::Editor
{
	class UiSurface;

	/**
		@brief Window shown over the interface with a title, content and buttons

		The window is moved by dragging its title, the cross next to the title cancels it as Escape does. A window
		that is @ref Resizable is resized by its edges and the corner with the dots.
	*/
	class UiDialog : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		/**
		 * @brief Creates a new instance
		 *
		 * With @p scrollContent, a dialog taller than the screen is made as tall as the screen and its content scrolls,
		 * the title and the buttons stay. It's left to the dialog, as a list in the content that has no height of its
		 * own would be as tall as all of its rows then, and the whole content would scroll instead of the list.
		 */
		explicit UiDialog(StringView title, bool scrollContent = false);

		/** @brief Returns the area for the content */
		UiStack* GetContent() const {
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
		UiButton* AddButton(StringView text, Function<void()>&& onClick, bool accent = false);
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
		 * A list in the content that should use the room is given @ref UiElement::Flex and no height of its own, it
		 * gets what's left then, and scrolls if it's longer.
		 */
		float MaxScreenHeightFraction = 1.0f;
		/**
		 * @brief Part of the width of the screen the dialog takes if its content is narrower, 0 for as wide as its content
		 *
		 * A dialog is never narrower than its content, unless the screen is.
		 */
		float MinScreenWidthFraction = 0.0f;
		/**
		 * @brief Whether the window can be resized by its edges
		 *
		 * Its size is then its @ref Width and @ref Height, set by the resizing, the size it starts with if they're set
		 * before, and the content should fill it by @ref UiElement::Flex.
		 */
		bool Resizable = false;
		/** @brief Smallest size in units the window can be resized to */
		Vector2f MinResizeSize = Vector2f(160.0f, 120.0f);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		UiElement* HitTest(Vector2f pos) override;
		/** @brief With the edges a window that is @ref Resizable is grabbed by, which reach a little outside of it */
		Rectf GetHitBounds() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		bool OnKeyPressed(const KeyboardEvent& event) override;

	protected:
		UiDragBar* _titleBar;
		// The cross in the title, it's easier to hit than its size, see HitTest()
		UiButton* _closeButton;
		UiStack* _content;
		UiStack* _buttons;
		SmallVector<UiButton*, 4> _buttonList;
		// The content is in it if it scrolls, see UiDialog()
		UiScrollView* _contentScroll = nullptr;

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
	struct UiMessageDialog
	{
		/**
		 * @brief Shows a message, the callback gets the index of the button chosen
		 *
		 * It gets -1 if the message was closed by Escape or the cross instead --- no button was chosen, so nothing
		 * should be done that can't be done later, nothing should be discarded.
		 */
		static UiDialog* Show(UiSurface& surface, StringView title, StringView text, std::initializer_list<StringView> buttons,
			Function<void(std::int32_t)>&& callback);
	};

	/** @brief What a file dialog is for */
	enum class UiFileDialogMode : std::uint8_t
	{
		Open,			/**< Opening an existing file */
		Save			/**< Saving a file, a new name can be typed */
	};

	/** @brief Place where files are looked for */
	struct UiFileRoot
	{
		/** @brief Name shown */
		String Name;
		/** @brief Path of the directory */
		String Path;
		/** @brief Whether files can be saved there */
		bool Writable;
	};

	/**
		@brief Picks a file to open or to save to

		Only files with the extension are listed, several can be separated by `;`, e.g. `".it;.xm"`. The first one is
		added to a name typed without it. If it's empty, all files are listed. When opening, a file of the initial name
		is selected.
	*/
	class UiFileDialog : public UiDialog
	{
		DEATH_RUNTIME_OBJECT(UiDialog);

	public:
		/** @brief Creates a new instance */
		UiFileDialog(StringView title, UiFileDialogMode mode, SmallVectorImpl<UiFileRoot>&& roots, StringView extension,
			StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen);

		/** @brief Shows a file dialog */
		static UiFileDialog* Show(UiSurface& surface, StringView title, UiFileDialogMode mode, SmallVectorImpl<UiFileRoot>&& roots,
			StringView extension, StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Update(float timeMult) override;

	private:
		struct Entry
		{
			String Name;
			bool IsDirectory;
		};

		UiFileDialogMode _mode;
		SmallVector<UiFileRoot, 4> _roots;
		std::int32_t _rootIndex;
		String _extension;
		String _directory;
		SmallVector<Entry, 0> _entries;
		std::int32_t _selected;
		UiScrollView* _list;
		UiVirtualList* _rows;
		UiTextBox* _nameBox;
		Function<void(StringView)> _onChosen;
		String _pendingNavigation;

		void Navigate(StringView directory);
		bool MatchesExtension(StringView name) const;
		void Choose(std::int32_t index, bool confirm);
		void Confirm();
		bool IsWritable() const;
		String GetRelativeDirectory() const;
		// Whether a path is a root or a directory in it
		static bool IsInRoot(StringView path, StringView root);
	};
}

#endif
