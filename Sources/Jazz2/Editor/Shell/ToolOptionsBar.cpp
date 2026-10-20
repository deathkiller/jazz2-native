#include "ToolOptionsBar.h"

#if defined(WITH_EDITOR)

#include "EditorShell.h"
#include "ShellCommon.h"
#include "../Tools/EditorContext.h"
#include "../Files/EditorSettings.h"
#include "../Input/ActionRegistry.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// Units around the icon of a button, as the other buttons of the toolbar have, and for a finger
		constexpr float ButtonExtra = 2.0f;
		constexpr float TouchButtonExtra = 6.0f;
		// Units between two options, and for a finger
		constexpr float OptionSpacing = 2.0f;
		constexpr float TouchOptionSpacing = 4.0f;
		// Width of a number in units
		constexpr float NumberWidth = 38.0f;

		bool IsTouch(const UiElement& element)
		{
			UiSurface* surface = element.GetSurface();
			return (surface != nullptr && surface->TouchInput);
		}

		// Button of an option, as high as the other buttons of the toolbar, wider for a finger
		class OptionButton : public UiButton
		{
		public:
			OptionButton(StringView text, EditorIcon icon, Function<void()>&& onClick)
				: UiButton(text, icon, Death::move(onClick))
			{
				Style = UiButtonStyle::Flat;
				Height = UiTheme::ButtonHeight + ButtonExtra;
			}

			// Makes the tooltip when it's shown, a shortcut in it may have been changed meanwhile
			mutable Function<String()> TooltipFunc;

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				Vector2f size = UiButton::Measure(context, available);
				float square = context.Px(UiTheme::ButtonHeight + (IsTouch(*this) ? TouchButtonExtra : ButtonExtra));
				size.X = (Text.empty() ? square : std::max(size.X, square));
				return size;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				if (!TooltipFunc) {
					return UiButton::GetTooltip(pos);
				}
				_liveTooltip = TooltipFunc();
				return _liveTooltip;
			}

		private:
			mutable String _liveTooltip;
		};

		// Dropdown as wide as the longest of the texts it can show
		class OptionDropdown : public UiDropdown
		{
		public:
			OptionDropdown(Function<String()>&& textFunc, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder, SmallVector<String, 0>&& texts)
				: UiDropdown(Death::move(textFunc), Death::move(builder)), _texts(Death::move(texts))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float widest = 0.0f;
				for (const String& text : _texts) {
					widest = std::max(widest, context.MeasureText(text));
				}
				float padding = context.Px(UiTheme::Padding);
				return Vector2f(std::round(widest + padding * 2.5f + context.Px(UiTheme::IconSize)), context.Px(UiTheme::RowHeight + 2.0f));
			}

		private:
			SmallVector<String, 0> _texts;
		};

		void BindAction(OptionButton& button, EditorAction* action)
		{
			button.IsEnabledFunc = [action]() {
				return action->CanExecute();
			};
			if (action->IsChecked) {
				button.IsCheckedFunc = [action]() {
					return action->IsChecked();
				};
			}
			button.TooltipFunc = [action]() {
				return action->GetTooltip();
			};
		}

		// Label before an option in the list of those that don't fit the row
		void AddListLabel(UiElement* row, StringView label)
		{
			if (!label.empty()) {
				auto* text = row->Add<UiLabel>(label, UiTheme::TextDim);
				text->Width = 48.0f;
			}
		}

		// Values of a choice, or toggles each by itself, of actions of the editor or kept by a tool
		struct Choice
		{
			struct Entry
			{
				String Label;
				EditorIcon Icon;
				String Tooltip;
				EditorAction* Action;
			};

			String Label;
			SmallVector<Entry, 0> Entries;
			Function<std::int32_t()> Getter;
			Function<void(std::int32_t)> Setter;
			bool PreferDropdown = false;
			bool Toggles = false;

			bool HasAllIcons() const
			{
				for (const Entry& entry : Entries) {
					if (entry.Icon == EditorIcon::None) {
						return false;
					}
				}
				return true;
			}

			std::int32_t GetChosen()
			{
				if (Getter) {
					return Getter();
				}
				for (std::int32_t i = 0; i < std::int32_t(Entries.size()); i++) {
					EditorAction* action = Entries[i].Action;
					if (action != nullptr && action->IsChecked && action->IsChecked()) {
						return i;
					}
				}
				return -1;
			}

			bool IsChecked(std::int32_t index)
			{
				if (Toggles) {
					EditorAction* action = Entries[index].Action;
					return (action != nullptr && action->IsChecked && action->IsChecked());
				}
				return (GetChosen() == index);
			}

			bool IsEnabled(std::int32_t index)
			{
				EditorAction* action = Entries[index].Action;
				return (action == nullptr || action->CanExecute());
			}

			void Choose(std::int32_t index)
			{
				if (Setter) {
					Setter(index);
					return;
				}
				EditorAction* action = Entries[index].Action;
				if (action != nullptr && action->CanExecute()) {
					action->Execute();
				}
			}

			String GetTooltip(std::int32_t index) const
			{
				const Entry& entry = Entries[index];
				String shortcut = (entry.Action != nullptr ? entry.Action->Key.ToString() : String());
				if (entry.Tooltip.empty()) {
					return (entry.Action != nullptr ? entry.Action->GetTooltip() : entry.Label);
				}
				return (shortcut.empty() ? entry.Tooltip : String(entry.Tooltip + " ("_s + shortcut + ")"_s));
			}

			String GetDropdownText()
			{
				if (Toggles) {
					std::int32_t checked = 0;
					for (std::int32_t i = 0; i < std::int32_t(Entries.size()); i++) {
						checked += (IsChecked(i) ? 1 : 0);
					}
					return String(Label + " ("_s + FormatInteger(checked) + ")"_s);
				}
				std::int32_t chosen = GetChosen();
				String value = (chosen >= 0 && chosen < std::int32_t(Entries.size()) ? Entries[chosen].Label : String());
				return (Label.empty() ? value : String(Label + ": "_s + value));
			}
		};

		UiElement* MakeChoice(UiElement* parent, bool list, bool dropdown, const std::shared_ptr<Choice>& choice)
		{
			UiElement* container = parent;
			UiStack* row = nullptr;
			if (list) {
				row = parent->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				AddListLabel(row, choice->Label);
				container = row;
			}

			UiElement* element;
			if (dropdown) {
				SmallVector<String, 0> texts;
				if (choice->Toggles) {
					texts.push_back(String(choice->Label + " ("_s + FormatInteger(std::int32_t(choice->Entries.size())) + ")"_s));
				} else {
					for (const Choice::Entry& entry : choice->Entries) {
						texts.push_back(choice->Label.empty() ? entry.Label : String(choice->Label + ": "_s + entry.Label));
					}
				}
				auto* box = container->Add<OptionDropdown>([choice]() { return choice->GetDropdownText(); }, [choice](SmallVectorImpl<UiMenuEntry>& entries) {
					for (std::int32_t i = 0; i < std::int32_t(choice->Entries.size()); i++) {
						const Choice::Entry& source = choice->Entries[i];
						UiMenuEntry& entry = entries.emplace_back();
						entry.Label = source.Label;
						entry.Icon = source.Icon;
						if (source.Action != nullptr) {
							entry.ShortcutText = source.Action->Key.ToString();
						}
						entry.Execute = [choice, i]() { choice->Choose(i); };
						entry.IsEnabled = [choice, i]() { return choice->IsEnabled(i); };
						entry.IsChecked = [choice, i]() { return choice->IsChecked(i); };
					}
				}, Death::move(texts));
				box->Tooltip = choice->Label;
				element = box;
			} else {
				auto* group = container->Add<UiStack>(UiOrientation::Horizontal, 1.0f);
				for (std::int32_t i = 0; i < std::int32_t(choice->Entries.size()); i++) {
					const Choice::Entry& entry = choice->Entries[i];
					// In the list with their text, the icons alone could be hard to tell apart there
					StringView text = (list || entry.Icon == EditorIcon::None ? StringView(entry.Label) : StringView());
					auto* button = group->Add<OptionButton>(text, entry.Icon, [choice, i]() { choice->Choose(i); });
					button->IsCheckedFunc = [choice, i]() { return choice->IsChecked(i); };
					button->IsEnabledFunc = [choice, i]() { return choice->IsEnabled(i); };
					button->TooltipFunc = [choice, i]() { return choice->GetTooltip(i); };
				}
				element = group;
			}
			return (row != nullptr ? row : element);
		}

		// The action that chooses a tool, the options of a tool only for experts aren't left out for those who aren't
		const char* GetToolActionId(EditorToolType type)
		{
			switch (type) {
				case EditorToolType::Brush: return ActionIds::ToolBrush;
				case EditorToolType::Eraser: return ActionIds::ToolEraser;
				case EditorToolType::Rectangle: return ActionIds::ToolRectangle;
				case EditorToolType::Fill: return ActionIds::ToolFill;
				case EditorToolType::Eyedropper: return ActionIds::ToolEyedropper;
				case EditorToolType::Select: return ActionIds::ToolSelect;
				case EditorToolType::Wand: return ActionIds::ToolWand;
				case EditorToolType::Hand: return ActionIds::ToolHand;
				case EditorToolType::Event: return ActionIds::ToolEvent;
				case EditorToolType::Measure: return ActionIds::ToolMeasure;
				case EditorToolType::Path: return ActionIds::ToolPath;
				case EditorToolType::Smart: return ActionIds::ToolSmart;
				case EditorToolType::Ground: return ActionIds::ToolGround;
				case EditorToolType::Shape: return ActionIds::ToolShape;
				default: return nullptr;
			}
		}
	}

	class ToolOptionsBar::MoreButton : public OptionButton
	{
	public:
		explicit MoreButton(ToolOptionsBar& bar)
			: OptionButton({}, EditorIcon::More, {}), _bar(bar), _skip(false)
		{
			Tooltip = _("More options of the tool");
			OnClick = [this]() {
				if (!_skip) {
					_bar.ShowList();
				}
				_skip = false;
			};
		}

		bool OnPointerDown(const UiPointerEvent& event) override
		{
			// A click on it while its list is shown only closes the list
			_skip = *_bar._listJustClosed;
			return OptionButton::OnPointerDown(event);
		}

	private:
		ToolOptionsBar& _bar;
		bool _skip;
	};

	class ToolOptionsBar::Builder : public ToolOptionsBuilder
	{
	public:
		Builder(ToolOptionsBar& bar, bool hideExpert)
			: _bar(bar), _hideExpert(hideExpert), _separatorPending(false)
		{
		}

		void AddAction(const char* actionId, StringView label, EditorIcon icon) override
		{
			EditorAction* action = FindAction(actionId);
			if (action == nullptr) {
				return;
			}
			EditorIcon shownIcon = (icon != EditorIcon::None ? icon : action->Icon);
			String text = (label.empty() ? String(action->Label) : String(label));
			Push([action, shownIcon, text](UiElement* parent, Form form) -> UiElement* {
				bool list = (form == Form::List);
				// In the list with the whole label of the action, in the row by its icon or a short text
				StringView shownText = (list ? StringView(action->Label) : (shownIcon != EditorIcon::None ? StringView() : StringView(text)));
				auto* button = parent->Add<OptionButton>(shownText, shownIcon, [action]() {
					if (action->CanExecute()) {
						action->Execute();
					}
				});
				BindAction(*button, action);
				if (list) {
					button->TextAlign = Alignment::Left;
				}
				return button;
			}, false);
		}

		void AddChoice(StringView label, ArrayView<const ToolOptionItem> items, bool preferDropdown) override
		{
			auto choice = std::make_shared<Choice>();
			choice->Label = label;
			choice->PreferDropdown = preferDropdown;
			for (const ToolOptionItem& item : items) {
				// Only the icon of the item, the actions of a choice often share one
				if (EditorAction* action = FindAction(item.ActionId)) {
					choice->Entries.push_back(Choice::Entry{ item.Label, item.Icon, item.Tooltip, action });
				}
			}
			PushChoice(Death::move(choice));
		}

		void AddChoice(StringView label, ArrayView<const ToolOptionItem> items, Function<std::int32_t()>&& getter,
			Function<void(std::int32_t)>&& setter, bool preferDropdown) override
		{
			auto choice = std::make_shared<Choice>();
			choice->Label = label;
			choice->PreferDropdown = preferDropdown;
			choice->Getter = Death::move(getter);
			choice->Setter = Death::move(setter);
			for (const ToolOptionItem& item : items) {
				choice->Entries.push_back(Choice::Entry{ item.Label, item.Icon, item.Tooltip, nullptr });
			}
			PushChoice(Death::move(choice));
		}

		void AddToggles(StringView label, ArrayView<const ToolOptionItem> items) override
		{
			auto choice = std::make_shared<Choice>();
			choice->Label = label;
			choice->Toggles = true;
			for (const ToolOptionItem& item : items) {
				if (EditorAction* action = FindAction(item.ActionId)) {
					choice->Entries.push_back(Choice::Entry{ item.Label, item.Icon, item.Tooltip, action });
				}
			}
			PushChoice(Death::move(choice));
		}

		void AddToggle(const ToolOptionItem& item, Function<bool()>&& getter, Function<void(bool)>&& setter) override
		{
			auto get = std::make_shared<Function<bool()>>(Death::move(getter));
			auto set = std::make_shared<Function<void(bool)>>(Death::move(setter));
			String label = item.Label;
			EditorIcon icon = item.Icon;
			String tooltip = (item.Tooltip.empty() ? item.Label : item.Tooltip);
			Push([get, set, label, icon, tooltip](UiElement* parent, Form form) -> UiElement* {
				bool list = (form == Form::List);
				auto* button = parent->Add<OptionButton>(list || icon == EditorIcon::None ? StringView(label) : StringView(), icon, [get, set]() {
					(*set)(!(*get)());
				});
				button->IsCheckedFunc = [get]() { return (*get)(); };
				button->Tooltip = tooltip;
				if (list) {
					button->TextAlign = Alignment::Left;
				}
				return button;
			}, false);
		}

		void AddNumber(StringView label, StringView tooltip, float minValue, float maxValue, float step,
			Function<float()>&& getter, Function<void(float, bool)>&& setter) override
		{
			auto get = std::make_shared<Function<float()>>(Death::move(getter));
			auto set = std::make_shared<Function<void(float, bool)>>(Death::move(setter));
			String labelText = label;
			String tooltipText = tooltip;
			// With its label always, a number alone wouldn't tell what it is
			Push([get, set, labelText, tooltipText, minValue, maxValue, step](UiElement* parent, Form form) -> UiElement* {
				auto* row = parent->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				if (form == Form::List) {
					AddListLabel(row, labelText);
				} else if (!labelText.empty()) {
					row->Add<UiLabel>(labelText, UiTheme::TextDim)->Tooltip = tooltipText;
				}
				auto* box = row->Add<UiNumberBox>([get]() { return (*get)(); }, [set](float value, bool final) { (*set)(value, final); },
					minValue, maxValue, step, 0);
				box->Width = NumberWidth;
				box->Tooltip = tooltipText;
				return row;
			}, false);
		}

		void AddSeparator() override
		{
			_separatorPending = true;
		}

	private:
		ToolOptionsBar& _bar;
		bool _hideExpert;
		bool _separatorPending;

		EditorAction* FindAction(const char* actionId)
		{
			if (actionId == nullptr) {
				return nullptr;
			}
			EditorAction* action = _bar._session.GetActions().Find(actionId);
			if (action == nullptr || (_hideExpert && EditorShell::IsExpertAction(actionId))) {
				return nullptr;
			}
			return action;
		}

		void PushChoice(std::shared_ptr<Choice> choice)
		{
			if (choice->Entries.empty()) {
				return;
			}
			// Icons side by side are as small as a dropdown already, and a dropdown is the same in the smaller row
			bool hasCompact = (!choice->PreferDropdown && !choice->HasAllIcons());
			Push([choice](UiElement* parent, Form form) -> UiElement* {
				bool dropdown = (form == Form::Compact || choice->PreferDropdown);
				return MakeChoice(parent, form == Form::List, dropdown, choice);
			}, hasCompact);
		}

		void Push(Function<UiElement*(UiElement*, Form)>&& make, bool hasCompact)
		{
			// No line before the first option or twice in a row
			if (_separatorPending && !_bar._items.empty()) {
				Item& separator = _bar._items.emplace_back();
				separator.IsSeparator = true;
				separator.Make = [](UiElement* parent, Form form) -> UiElement* {
					return parent->Add<UiSeparator>(form == Form::List ? UiOrientation::Horizontal : UiOrientation::Vertical);
				};
				separator.Full = separator.Make(&_bar, Form::Full);
			}
			_separatorPending = false;

			Item& item = _bar._items.emplace_back();
			item.Make = Death::move(make);
			item.Full = item.Make(&_bar, Form::Full);
			if (hasCompact) {
				item.Compact = item.Make(&_bar, Form::Compact);
			}
		}
	};

	ToolOptionsBar::ToolOptionsBar(IEditorContext& session)
		: _session(session), _more(nullptr), _firstHidden(0), _built(false), _shownTool(EditorToolType::Count), _shownExpert(false),
			_list(std::make_shared<UiElement*>(nullptr)), _listJustClosed(std::make_shared<bool>(false))
	{
	}

	ToolOptionsBar::~ToolOptionsBar()
	{
		// The list goes with the interface, it only shares the pointer to itself
	}

	Vector2f ToolOptionsBar::Measure(const UiContext& context, Vector2f available)
	{
		// It takes what the toolbar has left, see Arrange()
		return Vector2f(0.0f, context.Px(UiTheme::ButtonHeight + ButtonExtra));
	}

	void ToolOptionsBar::Arrange(const UiContext& context, const Rectf& bounds)
	{
		Bounds = bounds;
		std::size_t count = _items.size();
		float spacing = context.Px(IsTouch(*this) ? TouchOptionSpacing : OptionSpacing);
		Vector2f available(bounds.W, bounds.H);

		auto getElement = [this](std::size_t index, bool compact) {
			const Item& item = _items[index];
			return (compact && item.Compact != nullptr ? item.Compact : item.Full);
		};
		auto getRowWidth = [&](bool compact, std::size_t shown) {
			float width = 0.0f;
			for (std::size_t i = 0; i < shown; i++) {
				width += getElement(i, compact)->MeasureCached(context, available).X + (i > 0 ? spacing : 0.0f);
			}
			return width;
		};

		// All of them as they are, smaller, or as many as fit with the rest in the list
		bool compact = false;
		std::size_t shown = count;
		if (count > 0 && getRowWidth(false, count) > bounds.W) {
			compact = true;
			if (getRowWidth(true, count) > bounds.W) {
				float moreWidth = (_more != nullptr ? _more->MeasureCached(context, available).X : 0.0f);
				float width = 0.0f;
				shown = 0;
				for (std::size_t i = 0; i < count; i++) {
					float next = width + (i > 0 ? spacing : 0.0f) + getElement(i, true)->MeasureCached(context, available).X;
					if (next + spacing + moreWidth > bounds.W) {
						break;
					}
					width = next;
					shown = i + 1;
				}
				// No line right before the button, but the one before all of them stays
				while (shown > 1 && _items[shown - 1].IsSeparator) {
					shown--;
				}
			}
		}
		_firstHidden = shown;

		float x = bounds.X;
		for (std::size_t i = 0; i < count; i++) {
			Item& item = _items[i];
			UiElement* used = (i < shown ? getElement(i, compact) : nullptr);
			item.Full->Visible = (used == item.Full);
			if (item.Compact != nullptr) {
				item.Compact->Visible = (used == item.Compact);
			}
			if (used == nullptr) {
				continue;
			}
			Vector2f size = used->MeasureCached(context, available);
			float height = (item.IsSeparator ? bounds.H : std::min(size.Y, bounds.H));
			used->Arrange(context, Rectf(x, std::round(bounds.Y + (bounds.H - height) * 0.5f), size.X, height));
			x += size.X + spacing;
		}
		if (_more != nullptr) {
			_more->Visible = (shown < count);
			if (_more->Visible) {
				Vector2f size = _more->MeasureCached(context, available);
				_more->Arrange(context, Rectf(x, std::round(bounds.Y + (bounds.H - size.Y) * 0.5f), size.X, size.Y));
			}
		}
		// The list shows only those that don't fit, it's made again when that changes
		if (*_list != nullptr && _more != nullptr && !_more->Visible) {
			CloseList();
		}
	}

	void ToolOptionsBar::Update(float timeMult)
	{
		*_listJustClosed = false;
		// The tool chosen, not the one used for a while, so holding Space for the hand doesn't take the options away
		if (!_built || _session.GetTools().GetChosenType() != _shownTool || _session.GetSettings().Expert != _shownExpert) {
			Rebuild();
		}
		UiElement::Update(timeMult);
	}

	void ToolOptionsBar::Rebuild()
	{
		CloseList();
		ClearChildren();
		_items.clear();
		_more = nullptr;
		_firstHidden = 0;
		_built = true;

		ToolManager& tools = _session.GetTools();
		_shownTool = tools.GetChosenType();
		_shownExpert = _session.GetSettings().Expert;
		EditorTool* tool = tools.Get(_shownTool);
		if (tool == nullptr) {
			return;
		}
		// Whoever chose a tool only for experts sees all its options
		const char* toolAction = GetToolActionId(_shownTool);
		bool hideExpert = (!_shownExpert && (toolAction == nullptr || !EditorShell::IsExpertAction(toolAction)));
		Builder builder(*this, hideExpert);
		tool->BuildOptions(builder);
		if (_items.empty()) {
			return;
		}

		// A line between them and the buttons before
		Item separator;
		separator.IsSeparator = true;
		separator.Make = [](UiElement* parent, Form form) -> UiElement* {
			return parent->Add<UiSeparator>(form == Form::List ? UiOrientation::Horizontal : UiOrientation::Vertical);
		};
		separator.Full = separator.Make(this, Form::Full);
		_items.insert(_items.begin(), Death::move(separator));
		_more = Add<MoreButton>(*this);
	}

	void ToolOptionsBar::ShowList()
	{
		UiSurface* surface = GetSurface();
		if (surface == nullptr || _more == nullptr) {
			return;
		}
		if (*_list != nullptr) {
			CloseList();
			return;
		}

		auto panel = std::make_unique<UiPanel>(UiPanelStyle::Floating, UiOrientation::Vertical, UiTheme::Spacing, UiThickness(UiTheme::Padding));
		bool separatorPending = false;
		bool any = false;
		for (std::size_t i = _firstHidden; i < _items.size(); i++) {
			Item& item = _items[i];
			if (item.IsSeparator) {
				separatorPending = any;
				continue;
			}
			if (separatorPending) {
				panel->Add<UiSeparator>(UiOrientation::Horizontal);
				separatorPending = false;
			}
			item.Make(panel.get(), Form::List);
			any = true;
		}
		if (!any) {
			return;
		}

		UiOverlayOptions options;
		options.Position = Vector2f(_more->Bounds.X, _more->Bounds.Y + _more->Bounds.H);
		std::shared_ptr<UiElement*> list = _list;
		std::shared_ptr<bool> justClosed = _listJustClosed;
		options.OnClosed = [list, justClosed]() {
			*list = nullptr;
			*justClosed = true;
		};
		*_list = surface->PushOverlay(Death::move(panel), Death::move(options));
	}

	void ToolOptionsBar::CloseList()
	{
		if (*_list != nullptr) {
			UiElement* element = *_list;
			*_list = nullptr;
			if (UiSurface* surface = GetSurface()) {
				surface->CloseOverlay(element);
			}
		}
	}
}

#endif
