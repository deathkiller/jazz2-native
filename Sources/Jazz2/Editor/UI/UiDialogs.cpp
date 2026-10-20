#include "UiDialogs.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	UiDialog::UiDialog(StringView title, bool scrollContent)
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
		if (scrollContent) {
			// What's left between the title and the buttons, the content scrolls in it if it's taller
			_contentScroll = Add<UiScrollView>();
			_contentScroll->Flex = 1.0f;
			_content = _contentScroll->SetContent<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		} else {
			_content = Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			_content->Flex = 1.0f;
		}
		_buttons = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		_buttons->Add<UiElement>()->Flex = 1.0f;
		// A window closed only by its cross has no row of buttons, nor one whose buttons are all hidden now
		_buttons->IsVisibleFunc = [this]() {
			for (UiButton* button : _buttonList) {
				if (button->IsShown()) {
					return true;
				}
			}
			return false;
		};
	}

	Vector2f UiDialog::Measure(const UiContext& context, Vector2f available)
	{
		// At most the part of the height of the screen it may take, as wide as the content needs, at least the part
		// of the width it should take
		Vector2f limit(available.X, std::floor(available.Y * std::clamp(MaxScreenHeightFraction, 0.0f, 1.0f)));
		Vector2f size = UiPanel::Measure(context, limit);
		if (_contentScroll != nullptr && size.Y > limit.Y && Width < 0.0f) {
			// Taller than the screen, the content scrolls with the bar next to it, the scroll view made room for the bar
			// already if the content alone is that tall
			float padX = context.Px(Padding.Left) + context.Px(Padding.Right);
			float padY = context.Px(Padding.Top) + context.Px(Padding.Bottom);
			float contentHeight = _content->MeasureCached(context, Vector2f(std::max(limit.X - padX, 0.0f), 1.0e6f)).Y;
			if (contentHeight <= std::max(limit.Y - padY, 0.0f)) {
				size.X += context.Px(UiTheme::ScrollBarWidth);
			}
		}
		if (Width < 0.0f) {
			size.X = std::max(size.X, std::floor(available.X * std::clamp(MinScreenWidthFraction, 0.0f, 1.0f)));
		}
		size.X = std::min(size.X, limit.X);
		size.Y = std::min(size.Y, limit.Y);
		return size;
	}

	void UiDialog::Arrange(const UiContext& context, const Rectf& bounds)
	{
		_scale = context.Scale;
		UiPanel::Arrange(context, bounds);
	}

	void UiDialog::Draw(UiPainter& painter)
	{
		UiPanel::Draw(painter);
		if (Resizable) {
			UiResizeEdges::Draw(painter, Bounds, _resizeEdges != 0 ? _resizeEdges : _hoverEdges);
		}
	}

	void UiDialog::Update(float timeMult)
	{
		UiSurface* surface = GetSurface();
		_hoverEdges = (Resizable && _hovered && surface != nullptr ? GetEdgesAt(surface->GetPointerPos()) : 0);
		if (surface != nullptr) {
			UiResizeEdges::ShowCursor(*surface, _resizeEdges != 0 ? _resizeEdges : _hoverEdges);
		}
		UiPanel::Update(timeMult);
	}

	std::uint8_t UiDialog::GetEdgesAt(Vector2f pos) const
	{
		return (Resizable ? UiResizeEdges::GetAt(Bounds, _scale, pos, 2.0f) : 0);
	}

	UiElement* UiDialog::HitTest(Vector2f pos)
	{
		if (IsShown() && GetEdgesAt(pos) != 0) {
			return this;
		}
		return UiPanel::HitTest(pos);
	}

	Rectf UiDialog::GetHitBounds() const
	{
		return (Resizable ? UiResizeEdges::GetHitBounds(Bounds, _scale) : Bounds);
	}

	bool UiDialog::OnPointerDown(const UiPointerEvent& event)
	{
		std::uint8_t edges = GetEdgesAt(event.Pos);
		if (edges == 0 || event.Button != MouseButton::Left) {
			return UiPanel::OnPointerDown(event);
		}
		_resizeEdges = edges;
		_resizeStart = event.Pos;
		_resizeStartBounds = Bounds;
		// From where it's seen, it's no longer centered, which would move it by half of each change
		if (UiSurface* surface = GetSurface()) {
			surface->MoveOverlay(this, Vector2f(Bounds.X, Bounds.Y));
		}
		return true;
	}

	void UiDialog::OnPointerMove(const UiPointerEvent& event)
	{
		if (_resizeEdges == 0) {
			UiPanel::OnPointerMove(event);
			return;
		}
		UiSurface* surface = GetSurface();
		Vector2f screen = (surface != nullptr ? surface->GetScreenSize() : Vector2f(1.0e6f, 1.0e6f));
		Vector2f offset = event.Pos - _resizeStart;
		const Rectf& start = _resizeStartBounds;
		float minW = std::min(std::round(MinResizeSize.X * _scale), screen.X);
		float minH = std::min(std::round(MinResizeSize.Y * _scale), screen.Y);
		// A window already smaller than the smallest size, or past the edge of a window that got smaller since, can't
		// get any smaller, nor larger past the edge, the limits are kept in order
		auto clampOrdered = [](float value, float low, float high) {
			return std::clamp(value, std::min(low, high), std::max(low, high));
		};
		Rectf rect = start;
		if (_resizeEdges & UiResizeEdges::Left) {
			rect.X = clampOrdered(start.X + offset.X, 0.0f, start.X + start.W - minW);
			rect.W = start.X + start.W - rect.X;
		} else if (_resizeEdges & UiResizeEdges::Right) {
			rect.W = clampOrdered(start.W + offset.X, minW, screen.X - start.X);
		}
		if (_resizeEdges & UiResizeEdges::Top) {
			rect.Y = clampOrdered(start.Y + offset.Y, 0.0f, start.Y + start.H - minH);
			rect.H = start.Y + start.H - rect.Y;
		} else if (_resizeEdges & UiResizeEdges::Bottom) {
			rect.H = clampOrdered(start.H + offset.Y, minH, screen.Y - start.Y);
		}
		// The size in units, which it's measured by from now on
		Width = rect.W / _scale;
		Height = rect.H / _scale;
		if (surface != nullptr && (rect.X != start.X || rect.Y != start.Y)) {
			surface->MoveOverlay(this, Vector2f(rect.X, rect.Y));
		}
	}

	void UiDialog::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (_resizeEdges != 0) {
			_resizeEdges = 0;
			return;
		}
		UiPanel::OnPointerUp(event, inside);
	}

	UiButton* UiDialog::AddButton(StringView text, Function<void()>&& onClick, bool accent)
	{
		auto* button = _buttons->Add<UiButton>(text, EditorIcon::None, Death::move(onClick));
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
		if (OnCancel) {
			OnCancel();
			return;
		}
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
		UiButton* button = nullptr;
		for (UiButton* candidate : _buttonList) {
			if (candidate->Style == UiButtonStyle::Accent) {
				button = candidate;
				break;
			}
		}
		if (button == nullptr) {
			// A window closed only by its cross, whose buttons do other things, isn't confirmed by Enter, the first button
			// is a choice only in a dialog that is cancelled by its last one
			if (OnCancel) {
				return false;
			}
			button = _buttonList.front();
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

		auto sharedCallback = std::make_shared<Function<void(std::int32_t)>>(Death::move(callback));
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
		// Escape or the cross choose none of the buttons, the last one may be one that discards something
		dialog->OnCancel = [dialogPtr, sharedCallback]() {
			dialogPtr->Close();
			if (*sharedCallback) {
				(*sharedCallback)(-1);
			}
		};

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface.PushOverlay(Death::move(dialog), Death::move(options));
		return dialogPtr;
	}

	UiFileDialog::UiFileDialog(StringView title, UiFileDialogMode mode, SmallVectorImpl<UiFileRoot>&& roots, StringView extension,
		StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen)
		: UiDialog(title), _mode(mode), _rootIndex(0), _extension(extension), _selected(-1), _rows(nullptr), _nameBox(nullptr), _onChosen(Death::move(onChosen))
	{
		_roots = Death::move(roots);
		for (UiFileRoot& root : _roots) {
			// Directories in it are compared to it, they have no trailing separator
			StringView trimmed = root.Path.trimmedSuffix("/\\"_s);
			if (!trimmed.empty() && trimmed.size() != root.Path.size()) {
				root.Path = String(trimmed);
			}
		}

		// Starts in the root the initial directory is in, the innermost one if they're inside of each other
		std::int32_t bestLength = -1;
		for (std::size_t i = 0; i < _roots.size(); i++) {
			if (!initialDirectory.empty() && IsInRoot(initialDirectory, _roots[i].Path) && std::int32_t(_roots[i].Path.size()) > bestLength) {
				_rootIndex = std::int32_t(i);
				bestLength = std::int32_t(_roots[i].Path.size());
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
		// Only the rows that are seen are drawn, a directory may have thousands of files
		_rows = _list->SetContent<UiVirtualList>();
		_rows->CountFunc = [this]() { return std::int32_t(_entries.size()); };
		_rows->DrawRow = [this](UiPainter& painter, std::int32_t index, const Rectf& rect, bool selected, float hover) {
			const Entry& entry = _entries[index];
			if (selected) {
				painter.RoundRect(rect, UiTheme::Selection);
			} else if (hover > 0.0f) {
				Colorf color = UiTheme::Hover;
				color.A *= hover;
				painter.RoundRect(rect, color);
			}
			float iconSize = painter.Px(UiTheme::IconSize);
			float padding = painter.Px(UiTheme::Padding);
			painter.Icon(entry.IsDirectory ? (entry.Name == ".."_s ? EditorIcon::Up : EditorIcon::Folder) : EditorIcon::File,
				Rectf(rect.X + padding, rect.Y, iconSize, rect.H), entry.IsDirectory ? UiTheme::Accent : UiTheme::IconColor);
			float x = rect.X + padding * 2.0f + iconSize;
			painter.TextInRect(entry.Name, Rectf(x, rect.Y, rect.X + rect.W - x, rect.H), UiTheme::Text);
		};
		_rows->RowText = [this](std::int32_t index) { return _entries[index].Name; };
		_rows->OnSelectionChanged = [this](std::int32_t index) { Choose(index, false); };
		_rows->OnActivated = [this](std::int32_t index) { Choose(index, true); };

		if (_mode == UiFileDialogMode::Save) {
			auto* nameRow = _content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			nameRow->Add<UiLabel>(_("Name"), UiTheme::TextDim)->Width = 40.0f;
			_nameBox = nameRow->Add<UiTextBox>(initialName);
			_nameBox->Flex = 1.0f;
			_nameBox->MaxLength = 64;
			// The list is moved through while the name is typed
			_nameBox->OnKeyPreview = [this](const KeyboardEvent& event) {
				if (event.sym == Keys::Up || event.sym == Keys::Down || event.sym == Keys::PageUp || event.sym == Keys::PageDown) {
					_rows->OnKeyPressed(event);
					return true;
				}
				return false;
			};
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
			(_mode == UiFileDialogMode::Save && !_roots.empty() && IsInRoot(initialDirectory, _roots[_rootIndex].Path))));
		Navigate(useInitial ? initialDirectory : StringView(_roots.empty() ? String() : _roots[_rootIndex].Path));

		// The file chosen before is selected, it's scrolled to once the list is laid out
		if (_mode == UiFileDialogMode::Open && !initialName.empty()) {
			for (std::int32_t i = 0; i < std::int32_t(_entries.size()); i++) {
				if (!_entries[i].IsDirectory && StringUtils::equalsIgnoreCase(_entries[i].Name, initialName)) {
					_selected = i;
					_rows->SetSelected(i, true);
					break;
				}
			}
		}
	}

	UiFileDialog* UiFileDialog::Show(UiSurface& surface, StringView title, UiFileDialogMode mode, SmallVectorImpl<UiFileRoot>&& roots,
		StringView extension, StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen)
	{
		auto dialog = std::make_unique<UiFileDialog>(title, mode, Death::move(roots), extension, initialDirectory, initialName, Death::move(onChosen));
		UiFileDialog* dialogPtr = dialog.get();
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface.PushOverlay(Death::move(dialog), Death::move(options));
		if (dialogPtr->_nameBox != nullptr) {
			dialogPtr->_nameBox->BeginEdit();
		} else {
			// The keys move through the files at once
			surface.SetFocus(dialogPtr->_rows);
		}
		return dialogPtr;
	}

	Vector2f UiFileDialog::Measure(const UiContext& context, Vector2f available)
	{
		// Wide enough for long file names and the buttons of the places, as much of the window as is sensible, and the
		// list as high, it's what the dialog is for
		float scale = std::max(context.Scale, 0.5f);
		float widthUnits = std::clamp(available.X * 0.6f / scale, 360.0f, 640.0f);
		float listUnits = std::clamp(available.Y * 0.45f / scale, 150.0f, 360.0f);
		_list->Height = std::floor(listUnits);
		Vector2f size = UiDialog::Measure(context, available);
		size.X = std::min(context.Px(std::floor(widthUnits)), available.X);
		return size;
	}

	void UiFileDialog::Navigate(StringView directory)
	{
		_directory = directory;
		_entries.clear();
		_selected = -1;

		const UiFileRoot* root = (_rootIndex < std::int32_t(_roots.size()) ? &_roots[_rootIndex] : nullptr);
		// Up only as far as the root, not above it
		if (root != nullptr && _directory != root->Path && IsInRoot(_directory, root->Path)) {
			_entries.push_back({ ".."_s, true });
		}

		// Sorted by the name in small letters, made once for each, not on every comparison
		struct SortedEntry
		{
			String Key;
			Entry Item;
		};
		SmallVector<SortedEntry, 0> directories, files;
		if (fs::DirectoryExists(_directory)) {
			for (auto item : fs::Directory(_directory, fs::EnumerationOptions::SkipFiles)) {
				String name = fs::GetFileName(item);
				String key = StringUtils::lowercase(name);
				directories.push_back({ Death::move(key), { Death::move(name), true } });
			}
			for (auto item : fs::Directory(_directory, fs::EnumerationOptions::SkipDirectories)) {
				String name = fs::GetFileName(item);
				if (MatchesExtension(name)) {
					String key = StringUtils::lowercase(name);
					files.push_back({ Death::move(key), { Death::move(name), false } });
				}
			}
		}
		auto byName = [](const SortedEntry& a, const SortedEntry& b) { return a.Key < b.Key; };
		std::sort(directories.begin(), directories.end(), byName);
		std::sort(files.begin(), files.end(), byName);
		for (SortedEntry& entry : directories) {
			_entries.push_back(Death::move(entry.Item));
		}
		for (SortedEntry& entry : files) {
			_entries.push_back(Death::move(entry.Item));
		}

		// The list shows the entries as they are now, from the top
		_rows->SetSelected(-1, false);
		_list->ScrollTo(0.0f, true);
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
				_pendingNavigation = Death::move(target);
			}
			return;
		}
		if (_nameBox != nullptr) {
			// The name is typed on, all of it selected, so a new one replaces it
			_nameBox->Text = entry.Name;
			_nameBox->BeginEdit();
		}
		if (confirm) {
			Confirm();
		}
	}

	void UiFileDialog::Update(float timeMult)
	{
		if (!_pendingNavigation.empty()) {
			String target = Death::move(_pendingNavigation);
			_pendingNavigation = {};
			Navigate(target);
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

		Function<void(StringView)> onChosen = Death::move(_onChosen);
		Close();
		if (onChosen) {
			onChosen(path);
		}
	}

	bool UiFileDialog::IsWritable() const
	{
		// Only inside of the root, a directory outside of it isn't one files may be saved to
		return (_rootIndex < std::int32_t(_roots.size()) && _roots[_rootIndex].Writable && IsInRoot(_directory, _roots[_rootIndex].Path));
	}

	bool UiFileDialog::IsInRoot(StringView path, StringView root)
	{
		if (root.empty() || !path.hasPrefix(root)) {
			return false;
		}
		// The root itself, or a directory in it, not one that only starts with the same letters
		if (path.size() == root.size() || root.back() == '/' || root.back() == '\\') {
			return true;
		}
		char next = path[root.size()];
		return (next == '/' || next == '\\');
	}

	String UiFileDialog::GetRelativeDirectory() const
	{
		if (_rootIndex < std::int32_t(_roots.size())) {
			const UiFileRoot& root = _roots[_rootIndex];
			if (IsInRoot(_directory, root.Path)) {
				StringView rest = _directory.exceptPrefix(root.Path.size());
				return root.Name + (rest.empty() ? String("/"_s) : String(fs::FromNativeSeparators(rest)));
			}
		}
		return _directory;
	}
}

#endif
