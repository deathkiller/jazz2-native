#include "UiDialogs.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	UiDialog::UiDialog(StringView title)
		: UiPanel(UiPanelStyle::Floating, UiOrientation::Vertical, UiTheme::Spacing * 2.0f, UiThickness(UiTheme::Padding * 2.0f))
	{
		// The window is moved by its title
		_titleBar = Add<UiDragBar>(UiTheme::Spacing);
		_titleBar->Add<UiLabel>(title, UiTheme::TextHeader)->Flex = 1.0f;
		auto* closeButton = _titleBar->Add<UiButton>(StringView(), EditorIcon::Close, [this]() { Cancel(); });
		closeButton->Style = UiButtonStyle::Flat;
		closeButton->Tooltip = _("Close (Esc)");
		_titleBar->OnDragStart = [this](Vector2f) {
			_dragOrigin = Vector2f(Bounds.X, Bounds.Y);
		};
		_titleBar->OnDrag = [this](Vector2f offset, Vector2f) {
			if (UiSurface* surface = GetSurface()) {
				surface->MoveOverlay(this, _dragOrigin + offset);
			}
		};
		Add<UiSeparator>();
		_content = Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		_content->Flex = 1.0f;
		_buttons = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		_buttons->Add<UiElement>()->Flex = 1.0f;
	}

	UiButton* UiDialog::AddButton(StringView text, Function<void()>&& onClick, bool accent)
	{
		auto* button = _buttons->Add<UiButton>(text, EditorIcon::None, std::move(onClick));
		button->MinWidth = 64.0f;
		if (accent) {
			button->Style = UiButtonStyle::Accent;
		}
		_buttonList.push_back(button);
		return button;
	}

	void UiDialog::Close()
	{
		if (UiSurface* surface = GetSurface()) {
			surface->CloseOverlay(this);
		}
	}

	void UiDialog::Cancel()
	{
		if (_buttonList.empty()) {
			Close();
			return;
		}
		UiButton* button = _buttonList.back();
		if (button->IsUsable() && button->OnClick) {
			button->OnClick();
		} else {
			Close();
		}
	}

	bool UiDialog::OnKeyPressed(const KeyboardEvent& event)
	{
		if (event.sym == Keys::Escape) {
			Cancel();
			return true;
		}
		if ((event.sym != Keys::Return && event.sym != Keys::NumPadEnter) || _buttonList.empty()) {
			return false;
		}
		// The accented button is the one that confirms, a button that does something else can be first
		UiButton* button = _buttonList.front();
		for (UiButton* candidate : _buttonList) {
			if (candidate->Style == UiButtonStyle::Accent) {
				button = candidate;
				break;
			}
		}
		if (!button->IsUsable()) {
			return false;
		}
		if (button->OnClick) {
			button->OnClick();
		}
		return true;
	}

	UiDialog* UiMessageDialog::Show(UiSurface& surface, StringView title, StringView text, std::initializer_list<StringView> buttons,
		Function<void(std::int32_t)>&& callback)
	{
		auto dialog = std::make_unique<UiDialog>(title);
		UiDialog* dialogPtr = dialog.get();
		for (StringView line : text.split('\n')) {
			dialog->GetContent()->Add<UiLabel>(line, UiTheme::Text);
		}

		auto sharedCallback = std::make_shared<Function<void(std::int32_t)>>(std::move(callback));
		std::int32_t index = 0;
		for (StringView buttonText : buttons) {
			dialog->AddButton(buttonText, [dialogPtr, sharedCallback, index]() {
				dialogPtr->Close();
				if (*sharedCallback) {
					(*sharedCallback)(index);
				}
			}, index == 0);
			index++;
		}

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface.PushOverlay(std::move(dialog), std::move(options));
		return dialogPtr;
	}

	/** Row of the file list */
	class UiFileRow : public UiElement
	{
	public:
		UiFileRow(UiFileDialog& dialog, std::int32_t index)
			: _dialog(dialog), _index(index)
		{
		}

		Vector2f Measure(const UiContext& context, Vector2f available) override
		{
			return Vector2f(available.X, context.Px(UiTheme::RowHeight + 2.0f));
		}

		void Draw(UiPainter& painter) override
		{
			const auto& entry = _dialog._entries[_index];
			if (_dialog._selected == _index) {
				painter.RoundRect(Bounds, UiTheme::Selection);
			} else if (_hoverAmount > 0.0f) {
				Colorf hover = UiTheme::Hover;
				hover.A *= _hoverAmount;
				painter.RoundRect(Bounds, hover);
			}
			float iconSize = painter.Px(UiTheme::IconSize);
			float padding = painter.Px(UiTheme::Padding);
			painter.Icon(entry.IsDirectory ? (entry.Name == ".."_s ? EditorIcon::Up : EditorIcon::Folder) : EditorIcon::File,
				Rectf(Bounds.X + padding, Bounds.Y, iconSize, Bounds.H), entry.IsDirectory ? UiTheme::Accent : UiTheme::IconColor);
			float x = Bounds.X + padding * 2.0f + iconSize;
			painter.TextInRect(entry.Name, Rectf(x, Bounds.Y, Bounds.X + Bounds.W - x, Bounds.H), UiTheme::Text);
		}

		bool AcceptsPointer() const override
		{
			return true;
		}

		bool OnPointerDown(const UiPointerEvent& event) override
		{
			if (event.Button != MouseButton::Left) {
				return false;
			}
			_dialog.Choose(_index, event.Clicks >= 2);
			return false;
		}

	private:
		UiFileDialog& _dialog;
		std::int32_t _index;
	};

	UiFileDialog::UiFileDialog(StringView title, UiFileDialogMode mode, SmallVectorImpl<UiFileRoot>&& roots, StringView extension,
		StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen)
		: UiDialog(title), _mode(mode), _rootIndex(0), _extension(extension), _selected(-1), _nameBox(nullptr), _onChosen(std::move(onChosen)),
			_scrollToSelected(false)
	{
		_roots = std::move(roots);
		for (UiFileRoot& root : _roots) {
			// Directories in it are compared to it, they have no trailing separator
			StringView trimmed = root.Path.trimmedSuffix("/\\"_s);
			if (!trimmed.empty() && trimmed.size() != root.Path.size()) {
				root.Path = String(trimmed);
			}
		}

		// Starts in the root the initial directory is in
		for (std::size_t i = 0; i < _roots.size(); i++) {
			if (!initialDirectory.empty() && initialDirectory.hasPrefix(_roots[i].Path)) {
				_rootIndex = std::int32_t(i);
			}
		}

		auto* rootRow = _content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		for (std::size_t i = 0; i < _roots.size(); i++) {
			std::int32_t index = std::int32_t(i);
			auto* button = rootRow->Add<UiButton>(_roots[i].Name, EditorIcon::Folder, [this, index]() {
				_rootIndex = index;
				Navigate(_roots[index].Path);
			});
			button->Style = UiButtonStyle::Flat;
			button->IsCheckedFunc = [this, index]() { return (_rootIndex == index); };
			if (!_roots[i].Writable) {
				button->Tooltip = _("Files here can't be changed");
			}
		}

		auto* pathLabel = _content->Add<UiLabel>(StringView(), UiTheme::TextDim);
		pathLabel->TextFunc = [this]() { return GetRelativeDirectory(); };

		_list = _content->Add<UiScrollView>();
		_list->Height = 150.0f;

		if (_mode == UiFileDialogMode::Save) {
			auto* nameRow = _content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			nameRow->Add<UiLabel>(_("Name"), UiTheme::TextDim)->Width = 40.0f;
			_nameBox = nameRow->Add<UiTextBox>(initialName);
			_nameBox->Flex = 1.0f;
			_nameBox->MaxLength = 64;
		}

		AddButton(_mode == UiFileDialogMode::Save ? _("Save") : _("Open"), [this]() { Confirm(); }, true)
			->IsEnabledFunc = [this]() {
				if (_mode == UiFileDialogMode::Save) {
					return (IsWritable() && _nameBox != nullptr && !_nameBox->Text.trimmed().empty());
				}
				return (_selected >= 0 && _selected < std::int32_t(_entries.size()) && !_entries[_selected].IsDirectory);
			};
		AddButton(_("Cancel"), [this]() { Close(); });

		// A directory to save to doesn't have to exist yet, it's created along with the file
		bool useInitial = (!initialDirectory.empty() && (fs::DirectoryExists(initialDirectory) ||
			(_mode == UiFileDialogMode::Save && !_roots.empty() && initialDirectory.hasPrefix(_roots[_rootIndex].Path))));
		Navigate(useInitial ? initialDirectory : StringView(_roots.empty() ? String() : _roots[_rootIndex].Path));

		// The file chosen before is selected, it's scrolled to once the list is laid out
		if (_mode == UiFileDialogMode::Open && !initialName.empty()) {
			for (std::int32_t i = 0; i < std::int32_t(_entries.size()); i++) {
				if (!_entries[i].IsDirectory && StringUtils::equalsIgnoreCase(_entries[i].Name, initialName)) {
					_selected = i;
					_scrollToSelected = true;
					break;
				}
			}
		}
	}

	UiFileDialog* UiFileDialog::Show(UiSurface& surface, StringView title, UiFileDialogMode mode, SmallVectorImpl<UiFileRoot>&& roots,
		StringView extension, StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen)
	{
		auto dialog = std::make_unique<UiFileDialog>(title, mode, std::move(roots), extension, initialDirectory, initialName, std::move(onChosen));
		UiFileDialog* dialogPtr = dialog.get();
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface.PushOverlay(std::move(dialog), std::move(options));
		if (dialogPtr->_nameBox != nullptr) {
			dialogPtr->_nameBox->BeginEdit();
		}
		return dialogPtr;
	}

	Vector2f UiFileDialog::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f size = UiDialog::Measure(context, available);
		size.X = std::min(context.Px(300.0f), available.X);
		return size;
	}

	void UiFileDialog::Navigate(StringView directory)
	{
		_directory = directory;
		_entries.clear();
		_selected = -1;

		const UiFileRoot* root = (_rootIndex < std::int32_t(_roots.size()) ? &_roots[_rootIndex] : nullptr);
		if (root != nullptr && _directory != root->Path && _directory.hasPrefix(root->Path)) {
			_entries.push_back({ ".."_s, true });
		}

		SmallVector<Entry, 0> directories, files;
		if (fs::DirectoryExists(_directory)) {
			for (auto item : fs::Directory(_directory, fs::EnumerationOptions::SkipFiles)) {
				directories.push_back({ String(fs::GetFileName(item)), true });
			}
			for (auto item : fs::Directory(_directory, fs::EnumerationOptions::SkipDirectories)) {
				String name = fs::GetFileName(item);
				if (MatchesExtension(name)) {
					files.push_back({ std::move(name), false });
				}
			}
		}
		auto byName = [](const Entry& a, const Entry& b) { return StringUtils::lowercase(a.Name) < StringUtils::lowercase(b.Name); };
		std::sort(directories.begin(), directories.end(), byName);
		std::sort(files.begin(), files.end(), byName);
		for (Entry& entry : directories) {
			_entries.push_back(std::move(entry));
		}
		for (Entry& entry : files) {
			_entries.push_back(std::move(entry));
		}

		auto* rows = _list->SetContent<UiStack>(UiOrientation::Vertical, 0.0f);
		for (std::int32_t i = 0; i < std::int32_t(_entries.size()); i++) {
			rows->Add<UiFileRow>(*this, i);
		}
	}

	bool UiFileDialog::MatchesExtension(StringView name) const
	{
		if (_extension.empty()) {
			return true;
		}
		String lowercase = StringUtils::lowercase(name);
		for (StringView extension : _extension.split(';')) {
			if (!extension.empty() && lowercase.hasSuffix(extension)) {
				return true;
			}
		}
		return false;
	}

	void UiFileDialog::Choose(std::int32_t index, bool confirm)
	{
		if (index < 0 || index >= std::int32_t(_entries.size())) {
			return;
		}
		_selected = index;
		const Entry& entry = _entries[index];
		if (entry.IsDirectory) {
			if (confirm) {
				String target = (entry.Name == ".."_s ? String(fs::GetDirectoryName(_directory)) : fs::CombinePath(_directory, entry.Name));
				// Deferred, this row is destroyed by navigating
				_pendingNavigation = std::move(target);
			}
			return;
		}
		if (_nameBox != nullptr) {
			_nameBox->Text = entry.Name;
		}
		if (confirm) {
			Confirm();
		}
	}

	void UiFileDialog::Update(float timeMult)
	{
		if (!_pendingNavigation.empty()) {
			String target = std::move(_pendingNavigation);
			_pendingNavigation = {};
			Navigate(target);
		}
		if (_scrollToSelected && _list->Bounds.H > 0.0f) {
			UiElement* rows = _list->GetContent();
			UiElement* row = (rows != nullptr ? rows->GetChild(_selected) : nullptr);
			if (row != nullptr && row->Bounds.H > 0.0f) {
				float top = row->Bounds.Y - rows->Bounds.Y;
				_list->ScrollIntoView(top, top + row->Bounds.H);
			}
			_scrollToSelected = false;
		}
		UiDialog::Update(timeMult);
	}

	void UiFileDialog::Confirm()
	{
		String path;
		if (_mode == UiFileDialogMode::Save) {
			if (_nameBox == nullptr || !IsWritable()) {
				return;
			}
			String name = _nameBox->Text.trimmed();
			if (name.empty() || name.find('/') || name.find('\\') || name.find(':')) {
				if (UiSurface* surface = GetSurface()) {
					surface->ShowToast(_("The name can't contain slashes or colons"), EditorIcon::Warning);
				}
				return;
			}
			if (!MatchesExtension(name)) {
				StringView extension = _extension;
				if (StringView separator = extension.find(';')) {
					extension = extension.prefix(separator.begin());
				}
				name += extension;
			}
			path = fs::CombinePath(_directory, name);
		} else {
			if (_selected < 0 || _selected >= std::int32_t(_entries.size()) || _entries[_selected].IsDirectory) {
				return;
			}
			path = fs::CombinePath(_directory, _entries[_selected].Name);
		}

		Function<void(StringView)> onChosen = std::move(_onChosen);
		Close();
		if (onChosen) {
			onChosen(path);
		}
	}

	bool UiFileDialog::IsWritable() const
	{
		return (_rootIndex < std::int32_t(_roots.size()) && _roots[_rootIndex].Writable);
	}

	String UiFileDialog::GetRelativeDirectory() const
	{
		if (_rootIndex < std::int32_t(_roots.size())) {
			const UiFileRoot& root = _roots[_rootIndex];
			if (_directory.hasPrefix(root.Path)) {
				StringView rest = _directory.exceptPrefix(root.Path.size());
				return root.Name + (rest.empty() ? String("/"_s) : String(fs::FromNativeSeparators(rest)));
			}
		}
		return _directory;
	}
}

#endif
