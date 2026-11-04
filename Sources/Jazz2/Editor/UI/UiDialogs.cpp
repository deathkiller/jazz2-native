#include "UiDialogs.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include "../../../nCine/I18n.h"
#include "../../../nCine/Graphics/Texture.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	include <CommonWindows.h>
#endif

// Files are read on a thread of their own where the platform has threads, as BackgroundJob does
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	define UI_FILE_DIALOG_THREADED
#	include "../../../nCine/Threading/Thread.h"
#	include "../../../nCine/Threading/ThreadSync.h"
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		/** The cross in the title of a window, it takes the pointer out to the corner of the window and as far around it */
		class DialogCloseButton : public UiButton
		{
		public:
			DialogCloseButton(const UiElement& window, Function<void()>&& onClick)
				: UiButton(StringView(), EditorIcon::Close, Death::move(onClick)), _window(window)
			{
			}

			void Arrange(const UiContext& context, const Rectf& bounds) override
			{
				// Larger than it's measured, so the title doesn't get taller
				float grow = context.Px(2.0f);
				UiButton::Arrange(context, Rectf(bounds.X - grow, bounds.Y - grow, bounds.W + grow * 2.0f, bounds.H + grow * 2.0f));
			}

			Rectf GetHitBounds() const override
			{
				// The padding of the window around it is as easy to hit, by a finger too, also as far to the left and below
				const Rectf& window = _window.Bounds;
				float reachX = std::max(window.X + window.W - (Bounds.X + Bounds.W), 0.0f);
				float reachY = std::max(Bounds.Y - window.Y, 0.0f);
				return Rectf(Bounds.X - reachX, Bounds.Y - reachY, Bounds.W + reachX * 2.0f, Bounds.H + reachY * 2.0f);
			}

		private:
			const UiElement& _window;
		};
	}

	UiDialog::UiDialog(StringView title, bool scrollContent)
		: UiPanel(UiPanelStyle::Floating, UiOrientation::Vertical, UiTheme::Spacing * 3.0f,
			UiThickness(UiTheme::Padding * 3.0f, UiTheme::Padding * 2.0f, UiTheme::Padding * 3.0f, UiTheme::Padding * 3.0f))
	{
		// The window is moved by its title
		_titleBar = Add<UiDragBar>(UiTheme::Spacing);
		_titleBar->Add<UiLabel>(title, UiTheme::TextHeader)->Flex = 1.0f;
		_closeButton = _titleBar->Add<DialogCloseButton>(*this, [this]() { Cancel(); });
		_closeButton->Style = UiButtonStyle::Flat;
		// The title stays low, the cross is still easy to hit by its padding (see DialogCloseButton)
		_closeButton->Width = _closeButton->Height = UiTheme::ButtonHeight - 2.0f;
		// TRANSLATORS: Tooltip of the cross in the title of a dialog, with the key that closes it too
		_closeButton->Tooltip = _x("editor", "Close (Esc)");
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
		_buttons = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 2.0f);
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
			float stretched = std::floor(available.X * std::clamp(MinScreenWidthFraction, 0.0f, 1.0f));
			if (MaxStretchedWidth > 0.0f) {
				stretched = std::min(stretched, context.Px(MaxStretchedWidth));
			}
			size.X = std::max(size.X, stretched);
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
		if (!IsShown()) {
			return nullptr;
		}
		// The cross reaches into the padding around the title, the edges are still grabbed outside of the window
		if (_closeButton->IsShown() && Bounds.Contains(pos) && _closeButton->GetHitBounds().Contains(pos)) {
			return _closeButton;
		}
		if (GetEdgesAt(pos) != 0) {
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
		// Only as far as the part of the screen that isn't cut off
		Rectf screen = (surface != nullptr ? surface->GetSafeBounds() : Rectf(0.0f, 0.0f, 1.0e6f, 1.0e6f));
		Vector2f offset = event.Pos - _resizeStart;
		const Rectf& start = _resizeStartBounds;
		float minW = std::min(std::round(MinResizeSize.X * _scale), screen.W);
		float minH = std::min(std::round(MinResizeSize.Y * _scale), screen.H);
		// A window already smaller than the smallest size, or past the edge of a window that got smaller since, can't
		// get any smaller, nor larger past the edge, the limits are kept in order
		auto clampOrdered = [](float value, float low, float high) {
			return std::clamp(value, std::min(low, high), std::max(low, high));
		};
		Rectf rect = start;
		if (_resizeEdges & UiResizeEdges::Left) {
			rect.X = clampOrdered(start.X + offset.X, screen.X, start.X + start.W - minW);
			rect.W = start.X + start.W - rect.X;
		} else if (_resizeEdges & UiResizeEdges::Right) {
			rect.W = clampOrdered(start.W + offset.X, minW, screen.X + screen.W - start.X);
		}
		if (_resizeEdges & UiResizeEdges::Top) {
			rect.Y = clampOrdered(start.Y + offset.Y, screen.Y, start.Y + start.H - minH);
			rect.H = start.Y + start.H - rect.Y;
		} else if (_resizeEdges & UiResizeEdges::Bottom) {
			rect.H = clampOrdered(start.H + offset.Y, minH, screen.Y + screen.H - start.Y);
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
		// Larger than the buttons in the content, the answers are what the dialog is for
		button->Height = UiTheme::ButtonHeight + 4.0f;
		button->MinWidth = 72.0f;
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
		// At least this wide, a short question would make a cramped box
		dialog->MinScreenWidthFraction = 1.0f;
		dialog->MaxStretchedWidth = 280.0f;
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

	class UiFileDialog::FileDetails : public UiElement
	{
	public:
		static constexpr float ColumnWidth = 220.0f;
		static constexpr float MinLabelWidth = 0.4f;
		static constexpr float LabelGap = 24.0f;
		static constexpr float RowGap = 4.0f;
		static constexpr float ImageMaxHeight = UiTheme::RowHeight * 9.0f;

		std::unique_ptr<Texture> Image;
		SmallVector<UiFileInfo::Row, 8> Rows;

		FileDetails()
		{
			Visible = false;
		}

		Vector2f Measure(const UiContext& context, Vector2f available) override
		{
			if (Image == nullptr && Rows.empty()) {
				return Vector2f::Zero;
			}
			float width = context.Px(ColumnWidth);
			float height = GetImageSize(width, context.Px(ImageMaxHeight), context.Px(1.0f)).Y;
			if (!Rows.empty()) {
				std::int32_t lineCount = 0;
				SmallVector<StringView, 4> lines;
				for (const UiFileInfo::Row& row : Rows) {
					if (row.Label.empty()) {
						WrapText(row.Value, width, [&context](StringView text) { return context.MeasureText(text); }, lines);
						lineCount += std::max<std::int32_t>(std::int32_t(lines.size()), 1);
					} else {
						lineCount++;
					}
				}
				height += (height > 0.0f ? context.Px(UiTheme::Spacing * 2.0f) : 0.0f) + lineCount * context.Px(UiTheme::RowHeight) +
					float(Rows.size() - 1) * context.Px(RowGap);
			}
			return Vector2f(width, height);
		}

		void Draw(UiPainter& painter) override
		{
			// At the top - as the level select of the game shows it, in a thin black border with its corners rounded off by
			// leaving them out
			float border = GetBorder(painter.Px(1.0f));
			Vector2f imageSize = GetImageSize(Bounds.W, painter.Px(ImageMaxHeight), painter.Px(1.0f));
			float y = std::round(Bounds.Y);
			if (Image != nullptr && imageSize.X > border * 2.0f && imageSize.Y > border * 2.0f) {
				Rectf rect(std::round(Bounds.X) + border, y + border, imageSize.X - border * 2.0f, imageSize.Y - border * 2.0f);
				const Colorf borderColor(0.0f, 0.0f, 0.0f, 1.0f);
				painter.FillRect(Rectf(rect.X, rect.Y - border, rect.W, border), borderColor, UiLayer::Content);
				painter.FillRect(Rectf(rect.X, rect.Y + rect.H, rect.W, border), borderColor, UiLayer::Content);
				painter.FillRect(Rectf(rect.X - border, rect.Y, border, rect.H), borderColor, UiLayer::Content);
				painter.FillRect(Rectf(rect.X + rect.W, rect.Y, border, rect.H), borderColor, UiLayer::Content);
				painter.Image(*Image, rect, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White, -1, UiLayer::Content);
				y += imageSize.Y + painter.Px(UiTheme::Spacing * 2.0f);
			}

			float rowHeight = painter.Px(UiTheme::RowHeight);
			float rowGap = painter.Px(RowGap);
			// The values start together after the widest label, at 40-45% of the column
			float labelWidth = 0.0f;
			for (const UiFileInfo::Row& row : Rows) {
				if (!row.Label.empty()) {
					labelWidth = std::max(labelWidth, painter.MeasureText(row.Label));
				}
			}
			labelWidth = std::min(std::max(labelWidth + painter.Px(LabelGap), std::floor(Bounds.W * MinLabelWidth)), std::floor(Bounds.W * 0.45f));
			_rowsTop = y;
			_rowBottoms.clear();
			SmallVector<StringView, 4> lines;
			for (const UiFileInfo::Row& row : Rows) {
				if (!_rowBottoms.empty()) {
					y += rowGap;
				}
				if (row.Label.empty()) {
					// A message without a label on as many lines as it needs
					WrapText(row.Value, Bounds.W, [&painter](StringView text) { return painter.MeasureText(text); }, lines);
					for (StringView line : lines) {
						painter.TextInRect(line, Rectf(Bounds.X, y, Bounds.W, rowHeight), row.Color);
						y += rowHeight;
					}
					if (lines.empty()) {
						y += rowHeight;
					}
				} else {
					painter.TextInRect(row.Label, Rectf(Bounds.X, y, labelWidth, rowHeight), UiTheme::TextDim);
					// Cut off where the column ends, the whole value is in the tooltip
					Rectf rect(Bounds.X + labelWidth, y, Bounds.W - labelWidth, rowHeight);
					painter.PushClip(rect);
					painter.TextInRect(row.Value, rect, row.Color);
					painter.PopClip();
					y += rowHeight;
				}
				_rowBottoms.push_back(y);
			}
		}

		bool AcceptsPointer() const override
		{
			return true;
		}

		StringView GetTooltip(Vector2f pos) const override
		{
			// The whole value of the row pointed at, it may be cut off
			if (pos.Y < _rowsTop) {
				return {};
			}
			for (std::size_t i = 0; i < _rowBottoms.size() && i < Rows.size(); i++) {
				if (pos.Y < _rowBottoms[i]) {
					return Rows[i].Value;
				}
			}
			return {};
		}

	private:
		// Where the rows were drawn, to find the one pointed at
		float _rowsTop = 0.0f;
		SmallVector<float, 8> _rowBottoms;

		static float GetBorder(float pixel)
		{
			return std::max(1.0f, std::round(pixel));
		}

		// Breaks a text at spaces into lines of a width, a word wider than it gets a line of its own
		template<class Measurer>
		static void WrapText(StringView text, float width, const Measurer& measure, SmallVectorImpl<StringView>& lines)
		{
			lines.clear();
			StringView rest = text.trimmed();
			while (!rest.empty()) {
				std::size_t end = rest.size();
				if (measure(rest) > width) {
					std::size_t fits = 0, firstBreak = rest.size();
					for (std::size_t i = 1; i < rest.size(); i++) {
						if (rest[i] != ' ') {
							continue;
						}
						firstBreak = std::min(firstBreak, i);
						if (measure(rest.prefix(i)) > width) {
							break;
						}
						fits = i;
					}
					end = (fits > 0 ? fits : firstBreak);
				}
				lines.push_back(rest.prefix(end));
				rest = rest.exceptPrefix(end).trimmedPrefix();
			}
		}

		// Twice as large as it's stored at most, as wide as the column and not higher than the given height, with its border
		Vector2f GetImageSize(float width, float maxHeight, float pixel) const
		{
			if (Image == nullptr) {
				return Vector2f::Zero;
			}
			float border = GetBorder(pixel);
			Vector2i size = Image->GetSize();
			float scale = std::min({ 2.0f, (width - border * 2.0f) / float(size.X), maxHeight / float(size.Y) });
			if (scale <= 0.0f) {
				return Vector2f::Zero;
			}
			return Vector2f(std::floor(size.X * scale) + border * 2.0f, std::floor(size.Y * scale) + border * 2.0f);
		}
	};

	/**
		Reads files for a file dialog on a thread of its own, see @ref UiFileDialog::Asynchronous

		The kinds of the rows are read newest first, so the rows seen now come before those scrolled past, and the
		details of the selected file before all of them, only the last one asked for. Results are taken by the user
		interface each frame, those of an older listing or selection are dropped there. The thread is started with
		the first request and stopped and joined when the worker goes, which waits for the file being read.
	*/
	class UiFileDialog::Worker
	{
#if defined(UI_FILE_DIALOG_THREADED)
	public:
		struct Request
		{
			bool IsInfo = false;
			std::uint32_t Generation = 0;
			std::int32_t Index = -1;
			String Path;
		};

		struct Result
		{
			Request From;
			bool Success = false;
			UiFileKind Kind;
			UiFileInfo Info;
		};

		Worker(Function<bool(StringView, UiFileKind&)>& kindFunc, Function<bool(StringView, UiFileInfo&)>& infoFunc)
			: _kindFunc(kindFunc), _infoFunc(infoFunc)
		{
		}

		~Worker()
		{
			_lock.Lock();
			_stop = true;
			_kinds.clear();
			_hasInfo = false;
			_lock.Unlock();
			_wake.Broadcast();
			if (_started) {
				_thread.Join();
			}
		}

		Worker(const Worker&) = delete;
		Worker& operator=(const Worker&) = delete;

		bool Post(Request&& request)
		{
			if (_broken) {
				return false;
			}
			_lock.Lock();
			if (request.IsInfo) {
				_info = Death::move(request);
				_hasInfo = true;
			} else {
				_kinds.push_back(Death::move(request));
			}
			bool start = !_started;
			_started = true;
			_lock.Unlock();
			if (start) {
				_thread = nCine::Thread(Run, this);
				if (!_thread) {
					LOGE("Thread of the file dialog couldn't be started");
					_lock.Lock();
					_started = false;
					_kinds.clear();
					_hasInfo = false;
					_lock.Unlock();
					// The files are read right away from now on
					_broken = true;
					return false;
				}
			}
			_wake.Signal();
			return true;
		}

		void DropKinds()
		{
			_lock.Lock();
			_kinds.clear();
			_lock.Unlock();
		}

		void TakeResults(SmallVectorImpl<Result>& results)
		{
			_lock.Lock();
			for (Result& result : _results) {
				results.push_back(Death::move(result));
			}
			_results.clear();
			_lock.Unlock();
		}

	private:
		Function<bool(StringView, UiFileKind&)>& _kindFunc;
		Function<bool(StringView, UiFileInfo&)>& _infoFunc;
		nCine::Thread _thread;
		nCine::Mutex _lock;
		nCine::CondVariable _wake;
		SmallVector<Request, 0> _kinds;
		Request _info;
		bool _hasInfo = false;
		bool _stop = false;
		bool _started = false;
		bool _broken = false;
		SmallVector<Result, 0> _results;

		static void Run(void* arg)
		{
			nCine::Thread::SetCurrentName("FileDialog");
			static_cast<Worker*>(arg)->Loop();
		}

		void Loop()
		{
			while (true) {
				_lock.Lock();
				while (!_stop && !_hasInfo && _kinds.empty()) {
					_wake.Wait(_lock);
				}
				if (_stop) {
					_lock.Unlock();
					return;
				}
				Result result;
				if (_hasInfo) {
					result.From = Death::move(_info);
					_hasInfo = false;
				} else {
					result.From = Death::move(_kinds.back());
					_kinds.pop_back();
				}
				_lock.Unlock();

				if (result.From.IsInfo) {
					result.Success = (_infoFunc && _infoFunc(result.From.Path, result.Info));
				} else {
					result.Success = (_kindFunc && _kindFunc(result.From.Path, result.Kind));
				}

				_lock.Lock();
				if (!_stop) {
					_results.push_back(Death::move(result));
				}
				_lock.Unlock();
			}
		}
#endif
	};

	UiFileDialog::~UiFileDialog()
	{
		// Waits for the file being read, the functions it calls are members of the dialog
		_worker = nullptr;
	}

	UiFileDialog::UiFileDialog(StringView title, UiFileDialogMode mode, SmallVectorImpl<UiFileRoot>&& roots, StringView extension,
		StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen, bool anywhere)
		: UiDialog(title), _mode(mode), _rootIndex(0), _computerRoot(-1), _extension(extension), _selected(-1), _rows(nullptr), _nameBox(nullptr),
			_onChosen(Death::move(onChosen)), _preview(nullptr)
	{
		_roots = Death::move(roots);
		for (UiFileRoot& root : _roots) {
			// Directories in it are compared to it, they have no trailing separator
			StringView trimmed = root.Path.trimmedSuffix("/\\"_s);
			if (!trimmed.empty() && trimmed.size() != root.Path.size()) {
				root.Path = String(trimmed);
			}
		}
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || (defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_IOS)) || \
	((defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)) && !defined(DEATH_TARGET_ANDROID))
		// Only a desktop lets the files anywhere be reached, the places are where the game looks for them
		if (anywhere) {
			_computerRoot = std::int32_t(_roots.size());
#	if defined(DEATH_TARGET_WINDOWS)
			// TRANSLATORS: Name of a place in the file dialog, the whole computer with all its drives and folders
			_roots.push_back({ _x("editor", "Computer"), String(), true });
#	else
			_roots.push_back({ _x("editor", "Computer"), "/"_s, true });
#	endif
		}
#else
		static_cast<void>(anywhere);
#endif

		// Starts in the root the initial directory is in, the innermost one if they're inside of each other, the whole
		// computer only if it's in none of the others
		if (!initialDirectory.empty()) {
			std::int32_t found = FindRoot(initialDirectory);
			if (found >= 0) {
				_rootIndex = found;
			}
		}

		auto* rootRow = _content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		for (std::size_t i = 0; i < _roots.size(); i++) {
			std::int32_t index = std::int32_t(i);
			if (index == _computerRoot && index > 0) {
				// Apart from the places of the game
				rootRow->Add<UiSeparator>(UiOrientation::Vertical);
			}
			auto* button = rootRow->Add<UiButton>(_roots[i].Name, EditorIcon::Folder, [this, index]() {
				_rootIndex = index;
				Navigate(_roots[index].Path);
			});
			button->Style = UiButtonStyle::Flat;
			button->Height = UiTheme::ButtonHeight + 2.0f;
			button->IsCheckedFunc = [this, index]() { return (_rootIndex == index); };
			if (!_roots[i].Writable) {
				button->Tooltip = _x("editor", "Files here can't be changed");
			}
		}

		// Where the list is, a path can be typed there too
		auto* pathBox = _content->Add<UiTextBox>();
		pathBox->MaxLength = 1024;
		pathBox->TextFunc = [this]() { return GetRelativeDirectory(); };
		pathBox->OnKeyPreview = [this, pathBox](const KeyboardEvent& event) {
			if (event.sym != Keys::Return && event.sym != Keys::NumPadEnter) {
				return false;
			}
			// Only Enter goes there, and it doesn't also confirm the dialog with the file chosen before; deferred like going
			// into a directory, the box is still being edited now
			_pendingTyped = String(pathBox->Text.trimmed());
			if (UiSurface* surface = GetSurface()) {
				surface->SetFocus(_rows);
			}
			return true;
		};
		pathBox->Tooltip = _x("editor", "Type a path and press Enter to go there");

		auto* listRow = _content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		_list = listRow->Add<UiScrollView>();
		_list->Flex = 1.0f;
		_list->Height = 150.0f;
		_preview = listRow->Add<FileDetails>();
		// Only the rows that are seen are drawn, a directory may have thousands of files
		_rows = _list->SetContent<UiVirtualList>();
		// A little taller than other lists, it's what the dialog is for, and easier to hit
		_rows->RowHeight = UiTheme::RowHeight + 4.0f;
		_rows->CountFunc = [this]() { return std::int32_t(_entries.size()); };
		_rows->DrawRow = [this](UiPainter& painter, std::int32_t index, const Rectf& rect, bool selected, float hover) {
			Entry& entry = _entries[index];
			if (!entry.IsDirectory && !entry.KindKnown && !entry.KindPending) {
				ResolveKind(index);
			}
			if (selected) {
				painter.RoundRect(rect, UiTheme::Selection);
			} else if (hover > 0.0f) {
				Colorf color = UiTheme::Hover;
				color.A *= hover;
				painter.RoundRect(rect, color);
			}
			float iconSize = painter.Px(UiTheme::IconSize);
			float padding = painter.Px(UiTheme::Padding);
			if (entry.IsDirectory) {
				painter.Icon(entry.Name == ".."_s ? EditorIcon::Up : EditorIcon::Folder, Rectf(rect.X + padding, rect.Y, iconSize, rect.H), UiTheme::Accent);
			} else {
				painter.Icon(entry.Kind.Icon, Rectf(rect.X + padding, rect.Y, iconSize, rect.H), entry.Kind.Color);
			}
			float x = rect.X + padding * 2.0f + iconSize;
			painter.TextInRect(entry.Label.empty() ? entry.Name : entry.Label, Rectf(x, rect.Y, rect.X + rect.W - x, rect.H), UiTheme::Text);
			if (entry.Special >= 0 && entry.Special < std::int32_t(_special.size())) {
				// The value itself after the name
				x += painter.MeasureText(entry.Name) + padding * 2.0f;
				painter.TextInRect(_special[entry.Special].Value, Rectf(x, rect.Y, std::max(rect.X + rect.W - x, 0.0f), rect.H), UiTheme::TextDim);
			}
		};
		_rows->RowText = [this](std::int32_t index) { return (_entries[index].Label.empty() ? _entries[index].Name : _entries[index].Label); };
		_rows->RowTooltip = [this](std::int32_t index) {
			return (index >= 0 && index < std::int32_t(_entries.size()) ? _entries[index].Kind.Description : String());
		};
		_rows->OnSelectionChanged = [this](std::int32_t index) { Choose(index, false); };
		_rows->OnActivated = [this](std::int32_t index) { Choose(index, true); };

		if (_mode == UiFileDialogMode::Save) {
			auto* nameRow = _content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Label of the name of the file in the dialog to save a file, keep it short
			nameRow->Add<UiLabel>(_x("editor", "Name"), UiTheme::TextDim)->Width = 40.0f;
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

		// TRANSLATORS: Button in the dialog to open a file, opens the selected file
		AddButton(_mode == UiFileDialogMode::Save ? _("Save") : _x("editor", "Open"), [this]() { Confirm(); }, true)
			->IsEnabledFunc = [this]() {
				if (_mode == UiFileDialogMode::Save) {
					return (IsWritable() && _nameBox != nullptr && !_nameBox->Text.trimmed().empty());
				}
				return (_selected >= 0 && _selected < std::int32_t(_entries.size()) && !_entries[_selected].IsDirectory);
			};
		// TRANSLATORS: Button in the dialog to open or save a file, closes it without a change
		AddButton(_x("editor", "Cancel"), [this]() { Close(); });

		// Left of the buttons, before the space that pushes them to the right
		// TRANSLATORS: Check box at the bottom of the file dialog, lists also files and folders the system hides
		auto showHidden = std::make_unique<UiCheckBox>(_x("editor", "Show hidden files"), []() { return ShowHiddenFiles; }, [this](bool value) {
			ShowHiddenFiles = value;
			// The same directory listed again, its path is replaced by itself
			String directory = _directory;
			Navigate(directory);
		});
		_buttons->InsertChild(0, Death::move(showHidden));

		// A directory to save to doesn't have to exist yet, it's created along with the file
		bool useInitial = (!initialDirectory.empty() && (fs::DirectoryExists(initialDirectory) ||
			(_mode == UiFileDialogMode::Save && !_roots.empty() && IsInRoot(initialDirectory, _rootIndex))));
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
		StringView extension, StringView initialDirectory, StringView initialName, Function<void(StringView)>&& onChosen, bool anywhere)
	{
		auto dialog = std::make_unique<UiFileDialog>(title, mode, Death::move(roots), extension, initialDirectory, initialName, Death::move(onChosen), anywhere);
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
		// Wider with the details of the files next to the list
		float widthUnits = (InfoFunc ? std::clamp(available.X * 0.65f / scale, 460.0f, 720.0f) : std::clamp(available.X * 0.6f / scale, 360.0f, 640.0f));
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
		// What the worker reads for the rows listed before is dropped
		_listGeneration++;
#if defined(UI_FILE_DIALOG_THREADED)
		if (_worker != nullptr) {
			_worker->DropKinds();
		}
#endif

		if (_mode == UiFileDialogMode::Open) {
			for (std::size_t i = 0; i < _special.size(); i++) {
				Entry entry{_special[i].Label, false};
				entry.KindKnown = true;
				entry.Kind.Icon = _special[i].Icon;
				entry.Kind.Description = _special[i].Description;
				entry.Special = std::int32_t(i);
				_entries.push_back(Death::move(entry));
			}
		}

		const UiFileRoot* root = (_rootIndex < std::int32_t(_roots.size()) ? &_roots[_rootIndex] : nullptr);
		// Up only as far as the root, not above it
		if (root != nullptr && _directory != root->Path && IsInRoot(_directory, _rootIndex)) {
			_entries.push_back({ ".."_s, true });
		}

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (_rootIndex == _computerRoot && _directory.empty()) {
			// The whole computer starts at its drives
			DWORD drives = ::GetLogicalDrives();
			for (std::int32_t i = 0; i < 26; i++) {
				if ((drives & (DWORD(1) << i)) != 0) {
					char name[2] = { char('A' + i), ':' };
					_entries.push_back({ String(name, 2), true });
				}
			}
			_rows->SetSelected(-1, false);
			_list->ScrollTo(0.0f, true);
			return;
		}
#endif

		// Sorted by the name in small letters, made once for each, not on every comparison
		struct SortedEntry
		{
			String Key;
			Entry Item;
		};
		SmallVector<SortedEntry, 0> directories, files;
		// Hidden by the system, or by a dot as the convention of Unix has it, those are mostly settings of programs
		auto isHidden = [](StringView path, StringView name) {
			return (!ShowHiddenFiles && (name.hasPrefix('.') || fs::IsHidden(path)));
		};
		// A place may show other names than those of the files, they're sorted by what's shown
		auto labelFunc = (root != nullptr && IsInRoot(_directory, _rootIndex) ? root->LabelFunc : nullptr);
		if (fs::DirectoryExists(_directory)) {
			for (auto item : fs::Directory(_directory, fs::EnumerationOptions::SkipFiles)) {
				String name = fs::GetFileName(item);
				if (isHidden(item, name)) {
					continue;
				}
				Entry entry{Death::move(name), true};
				if (labelFunc != nullptr) {
					entry.Label = labelFunc(item, true);
				}
				String key = StringUtils::lowercase(entry.Label.empty() ? entry.Name : entry.Label);
				directories.push_back({ Death::move(key), Death::move(entry) });
			}
			for (auto item : fs::Directory(_directory, fs::EnumerationOptions::SkipDirectories)) {
				String name = fs::GetFileName(item);
				if (MatchesExtension(name) && !isHidden(item, name)) {
					Entry entry{Death::move(name), false};
					if (labelFunc != nullptr) {
						entry.Label = labelFunc(item, false);
					}
					String key = StringUtils::lowercase(entry.Label.empty() ? entry.Name : entry.Label);
					files.push_back({ Death::move(key), Death::move(entry) });
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
				// Up from the root of a drive are the drives, which are entered at their roots
				String target = (entry.Name == ".."_s ? String(fs::GetDirectoryName(_directory))
					: (_directory.empty() ? String(entry.Name + "\\"_s) : fs::CombinePath(_directory, entry.Name)));
				// Deferred, this row is destroyed by navigating
				_pendingNavigation = Death::move(target);
				_navigationPending = true;
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
		// The path may be empty, up from the root of a drive are the drives
		if (_navigationPending) {
			_navigationPending = false;
			String target = Death::move(_pendingNavigation);
			_pendingNavigation = {};
			Navigate(target);
		}
		if (!_pendingTyped.empty()) {
			String typed = Death::move(_pendingTyped);
			_pendingTyped = {};
			NavigateToTyped(typed);
		}
		// A few files a frame are looked into without the worker, a folder may have thousands of them
		_kindBudget = 6;
		TakeWorkerResults();
		UpdatePreview(timeMult);
		UiDialog::Update(timeMult);
	}

	void UiFileDialog::ResolveKind(std::int32_t index)
	{
		Entry& entry = _entries[index];
		if (!KindFunc) {
			entry.KindKnown = true;
			return;
		}
		String path = fs::CombinePath(_directory, entry.Name);
#if defined(UI_FILE_DIALOG_THREADED)
		if (Asynchronous) {
			if (_worker == nullptr) {
				_worker = std::make_unique<Worker>(KindFunc, InfoFunc);
			}
			Worker::Request request;
			request.Generation = _listGeneration;
			request.Index = index;
			request.Path = path;
			if (_worker->Post(Death::move(request))) {
				// Dim until it's known
				entry.KindPending = true;
				entry.Kind = UiFileKind();
				entry.Kind.Color = UiTheme::IconDisabled;
				return;
			}
		}
#endif
		if (_kindBudget <= 0) {
			return;
		}
		_kindBudget--;
		entry.KindKnown = true;
		if (!KindFunc(path, entry.Kind)) {
			entry.Kind = UiFileKind();
		}
	}

	void UiFileDialog::TakeWorkerResults()
	{
#if defined(UI_FILE_DIALOG_THREADED)
		if (_worker == nullptr) {
			return;
		}
		SmallVector<Worker::Result, 8> results;
		_worker->TakeResults(results);
		for (Worker::Result& result : results) {
			if (result.From.IsInfo) {
				// Only the last one asked for, while its file is still selected
				if (result.From.Generation != _infoGeneration || result.From.Path != _previewPath) {
					continue;
				}
				if (result.Success) {
					ShowInfo(Death::move(result.Info));
				} else {
					_preview->Visible = false;
				}
			} else if (result.From.Generation == _listGeneration && result.From.Index >= 0 && result.From.Index < std::int32_t(_entries.size())) {
				Entry& entry = _entries[result.From.Index];
				entry.KindPending = false;
				entry.KindKnown = true;
				entry.Kind = (result.Success ? Death::move(result.Kind) : UiFileKind());
			}
		}
#endif
	}

	void UiFileDialog::SetSpecialEntries(SmallVectorImpl<UiFileSpecialEntry>&& entries, StringView selectedValue, Function<void(StringView)>&& onChosen)
	{
		String selectedName;
		if (_selected >= 0 && _selected < std::int32_t(_entries.size()) && _entries[_selected].Special < 0 && !_entries[_selected].IsDirectory) {
			selectedName = _entries[_selected].Name;
		}
		_special = Death::move(entries);
		_onSpecialChosen = Death::move(onChosen);
		// Listed again with them at the top, the selection stays
		String directory = _directory;
		Navigate(directory);
		for (std::int32_t i = 0; i < std::int32_t(_entries.size()); i++) {
			const Entry& entry = _entries[i];
			if (entry.Special >= 0 ? (_special[entry.Special].Value == selectedValue && !selectedValue.empty())
				: (!entry.IsDirectory && !selectedName.empty() && entry.Name == selectedName)) {
				_selected = i;
				_rows->SetSelected(i, true);
				break;
			}
		}
	}

	void UiFileDialog::UpdatePreview(float timeMult)
	{
		if (_selected >= 0 && _selected < std::int32_t(_entries.size()) && _entries[_selected].Special >= 0) {
			// Shown at once, there's nothing to read
			const UiFileSpecialEntry& special = _special[_entries[_selected].Special];
			String key = "\x01"_s + special.Value;
			if (key != _previewPath) {
				_previewPath = Death::move(key);
				_previewDelay = 0.0f;
				_preview->Image = nullptr;
				_preview->Rows.clear();
				_preview->Rows.push_back({ {}, special.Description });
				// TRANSLATORS: Label next to a special value selected in a file dialog, such as the end of the episode instead of the next level
				_preview->Rows.push_back({ _x("editor", "Value"), special.Value, UiTheme::TextAccent });
				_preview->Visible = true;
			}
			return;
		}
		if (!InfoFunc) {
			return;
		}
		String path;
		if (_selected >= 0 && _selected < std::int32_t(_entries.size()) && !_entries[_selected].IsDirectory) {
			path = fs::CombinePath(_directory, _entries[_selected].Name);
		}
		if (path != _previewPath) {
			// Read once the selection rests, not for every file the keys go through; the one shown stays until then
			_previewPath = Death::move(path);
			_previewDelay = (Asynchronous ? 4.0f : 8.0f);
			// What is read for the file before isn't shown anymore
			_infoGeneration++;
			if (_previewPath.empty()) {
				_preview->Visible = false;
			}
			return;
		}
		if (_previewDelay <= 0.0f || (_previewDelay -= timeMult) > 0.0f || _previewPath.empty()) {
			return;
		}

#if defined(UI_FILE_DIALOG_THREADED)
		if (Asynchronous) {
			if (_worker == nullptr) {
				_worker = std::make_unique<Worker>(KindFunc, InfoFunc);
			}
			Worker::Request request;
			request.IsInfo = true;
			request.Generation = _infoGeneration;
			request.Index = _selected;
			request.Path = _previewPath;
			if (_worker->Post(Death::move(request))) {
				return;
			}
		}
#endif
		UiFileInfo info;
		if (!InfoFunc(_previewPath, info)) {
			_preview->Visible = false;
			return;
		}
		ShowInfo(Death::move(info));
	}

	void UiFileDialog::ShowInfo(UiFileInfo&& info)
	{
		if (info.HasKind && _selected >= 0 && _selected < std::int32_t(_entries.size())) {
			_entries[_selected].Kind = Death::move(info.Kind);
			_entries[_selected].KindKnown = true;
			_entries[_selected].KindPending = false;
		}
		Vector2i size = info.ImageSize;
		if (info.Rgba.size() >= std::size_t(size.X) * std::size_t(size.Y) * 4 && size.X > 0 && size.Y > 0) {
			if (_preview->Image == nullptr || _preview->Image->GetSize() != size) {
				_preview->Image = std::make_unique<Texture>("FilePreview", Texture::Format::RGBA8, size.X, size.Y);
			}
			_preview->Image->LoadFromTexels(info.Rgba.data(), 0, 0, size.X, size.Y);
		} else {
			_preview->Image = nullptr;
		}
		_preview->Rows = Death::move(info.Rows);
		_preview->Visible = (_preview->Image != nullptr || !_preview->Rows.empty());
	}

	void UiFileDialog::NavigateToTyped(StringView text)
	{
		// A path as the box shows it, starting with the name of a place, is in that place
		for (std::int32_t i = 0; i < std::int32_t(_roots.size()); i++) {
			const UiFileRoot& root = _roots[i];
			if (!text.hasPrefix(root.Name) || (text.size() > root.Name.size() && text[root.Name.size()] != '/' && text[root.Name.size()] != '\\')) {
				continue;
			}
			StringView rest = text.exceptPrefix(root.Name.size()).trimmedPrefix("/\\"_s);
			if (root.Path.empty()) {
				// The drives
				if (rest.empty()) {
					_rootIndex = i;
					Navigate({});
					return;
				}
				continue;
			}
			String path = (rest.empty() ? root.Path : fs::CombinePath(root.Path, fs::ToNativeSeparators(rest)));
			if (fs::DirectoryExists(path)) {
				_rootIndex = i;
				Navigate(path);
				return;
			}
		}

		// Relative to the directory shown, or a path of its own
		String path = (fs::IsAbsolutePath(text) ? String(text) : fs::CombinePath(_directory, text));
		String directory, fileName;
		if (fs::DirectoryExists(path)) {
			directory = path;
		} else if (fs::IsReadableFile(path) && MatchesExtension(path)) {
			directory = fs::GetDirectoryName(path);
			fileName = fs::GetFileName(path);
		} else if (_mode == UiFileDialogMode::Save && fs::DirectoryExists(fs::GetDirectoryName(path)) && !fs::GetFileName(path).empty()) {
			// A new file in a directory that is there
			directory = fs::GetDirectoryName(path);
			fileName = fs::GetFileName(path);
		} else {
			if (UiSurface* surface = GetSurface()) {
				surface->ShowToast(_x("editor", "There is no such folder"), EditorIcon::Warning);
			}
			return;
		}

		std::int32_t root = FindRoot(directory);
		if (root < 0) {
			if (UiSurface* surface = GetSurface()) {
				surface->ShowToast(_x("editor", "The file has to be in one of the places above"), EditorIcon::Warning);
			}
			return;
		}
		_rootIndex = root;
		Navigate(directory);
		if (fileName.empty()) {
			return;
		}
		if (_nameBox != nullptr) {
			_nameBox->Text = fileName;
			_nameBox->BeginEdit();
			return;
		}
		for (std::int32_t i = 0; i < std::int32_t(_entries.size()); i++) {
			if (!_entries[i].IsDirectory && StringUtils::equalsIgnoreCase(_entries[i].Name, fileName)) {
				_selected = i;
				_rows->SetSelected(i, true);
				break;
			}
		}
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
					surface->ShowToast(_x("editor", "The name can't contain slashes or colons"), EditorIcon::Warning);
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
			std::int32_t special = _entries[_selected].Special;
			if (special >= 0 && special < std::int32_t(_special.size())) {
				String value = _special[special].Value;
				Function<void(StringView)> onSpecialChosen = Death::move(_onSpecialChosen);
				Close();
				if (onSpecialChosen) {
					onSpecialChosen(value);
				}
				return;
			}
			path = fs::CombinePath(_directory, _entries[_selected].Name);
		}

		if (ConfirmFunc) {
			ConfirmFunc(path, [this, path]() { Choose(path); });
		} else {
			Choose(path);
		}
	}

	void UiFileDialog::Choose(StringView path)
	{
		String chosenPath = path;
		Function<void(StringView)> onChosen = Death::move(_onChosen);
		Close();
		if (onChosen) {
			onChosen(chosenPath);
		}
	}

	bool UiFileDialog::IsWritable() const
	{
		// Only inside of the root, a directory outside of it isn't one files may be saved to, nor the list of drives
		return (_rootIndex < std::int32_t(_roots.size()) && _roots[_rootIndex].Writable && !_directory.empty() && IsInRoot(_directory, _rootIndex));
	}

	bool UiFileDialog::IsInRoot(StringView path, std::int32_t rootIndex) const
	{
		if (rootIndex < 0 || rootIndex >= std::int32_t(_roots.size())) {
			return false;
		}
		if (rootIndex == _computerRoot) {
			// The drives are listed at an empty path
			return (path.empty() || fs::IsAbsolutePath(path));
		}
		return IsInRoot(path, _roots[rootIndex].Path);
	}

	std::int32_t UiFileDialog::FindRoot(StringView path) const
	{
		std::int32_t found = -1;
		std::size_t bestLength = 0;
		for (std::int32_t i = 0; i < std::int32_t(_roots.size()); i++) {
			if (i != _computerRoot && IsInRoot(path, _roots[i].Path) && (found < 0 || _roots[i].Path.size() > bestLength)) {
				found = i;
				bestLength = _roots[i].Path.size();
			}
		}
		if (found < 0 && IsInRoot(path, _computerRoot)) {
			found = _computerRoot;
		}
		return found;
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
		if (_rootIndex == _computerRoot) {
			// The whole path, as the system shows it, it can be typed over
			return (_directory.empty() ? _roots[_rootIndex].Name : _directory);
		}
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
