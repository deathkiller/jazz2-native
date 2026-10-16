#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiWidgets.h"

namespace Jazz2::Editor
{
	class UiSurface;

	/**
		@brief Window shown over the interface with a title, content and buttons

		The window is moved by dragging its title, the cross next to the title cancels it as Escape does.
	*/
	class UiDialog : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		/** @brief Creates a new instance */
		explicit UiDialog(StringView title);

		/** @brief Returns the area for the content */
		UiStack* GetContent() const {
			return _content;
		}
		/**
		 * @brief Adds a button
		 *
		 * Enter chooses the first accented button, or the first one if none is accented. Escape and the cross
		 * choose the last one, it should be the one that closes the dialog.
		 */
		UiButton* AddButton(StringView text, Function<void()>&& onClick, bool accent = false);
		/** @brief Closes the dialog */
		void Close();
		/** @brief Cancels the dialog as Escape does, by the last button, or closes it if it has none */
		void Cancel();

		bool OnKeyPressed(const KeyboardEvent& event) override;

	protected:
		UiDragBar* _titleBar;
		UiStack* _content;
		UiStack* _buttons;
		SmallVector<UiButton*, 4> _buttonList;

	private:
		Vector2f _dragOrigin;
	};

	/**
		@brief Shows a message with a choice of buttons
	*/
	struct UiMessageDialog
	{
		/** @brief Shows a message, the callback gets the index of the button chosen, or -1 if it was closed */
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
		UiTextBox* _nameBox;
		Function<void(StringView)> _onChosen;
		String _pendingNavigation;
		bool _scrollToSelected;

		void Navigate(StringView directory);
		bool MatchesExtension(StringView name) const;
		void Choose(std::int32_t index, bool confirm);
		void Confirm();
		bool IsWritable() const;
		String GetRelativeDirectory() const;

		friend class UiFileRow;
	};
}

#endif
