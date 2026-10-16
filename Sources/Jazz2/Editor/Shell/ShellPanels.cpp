#include "ShellPanels.h"
#include "TileEditor.h"
#include "TileSetsDialog.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Runtime/EditorCamera.h"
#include "../UI/UiColors.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		String FormatInteger(std::int32_t value)
		{
			char buffer[16];
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%d", value);
			return String(buffer, std::size_t(std::max(length, 0)));
		}

		const DocumentLayer* GetCurrentLayer(EditorSession& session)
		{
			std::int32_t index = session.GetCurrentLayer();
			LevelDocument& document = session.GetDocument();
			return (index >= 0 && index < std::int32_t(document.Layers.size()) ? &document.Layers[index] : nullptr);
		}

		/** An editable text over a part of the interface, @p onDone gets the text unless the edit is cancelled by Escape */
		class InlineTextEdit : public UiTextBox
		{
		public:
			InlineTextEdit(StringView text, Function<void(StringView)>&& onDone)
				: UiTextBox(text), _onDone(std::move(onDone)), _done(false)
			{
				OnCommit = [this](StringView text) { Finish(text); };
			}

			void OnFocusChanged(bool focused) override
			{
				UiTextBox::OnFocusChanged(focused);
				if (!focused) {
					// Confirmed by Enter or by clicking elsewhere, which committed it already, or cancelled by Escape
					_done = true;
					if (UiSurface* surface = GetSurface()) {
						surface->CloseOverlay(this);
					}
				}
			}

			// The edit is shown over the interface, a click outside of it closes it, which confirms it too
			void Finish(StringView text)
			{
				if (_done) {
					return;
				}
				_done = true;
				if (_onDone) {
					_onDone(text);
				}
			}

			static void Show(UiSurface& surface, const Rectf& rect, StringView text, StringView placeholder, Function<void(StringView)>&& onDone)
			{
				auto edit = std::make_unique<InlineTextEdit>(text, std::move(onDone));
				InlineTextEdit* editPtr = edit.get();
				edit->Width = rect.W / surface.GetScale();
				edit->MaxLength = 64;
				edit->Placeholder = placeholder;
				UiOverlayOptions options;
				options.Position = Vector2f(rect.X, rect.Y);
				options.OnClosed = [editPtr]() { editPtr->Finish(editPtr->Text); };
				surface.PushOverlay(std::move(edit), std::move(options));
				editPtr->BeginEdit();
			}

		private:
			Function<void(StringView)> _onDone;
			bool _done;
		};
	}

	String GetAutoLayerName(const DocumentLayer& layer, std::int32_t index)
	{
		if (layer.IsSpriteLayer()) {
			return _("Sprite layer");
		}
		switch (Tiles::LayerRendererType(layer.Record.RendererType)) {
			case Tiles::LayerRendererType::Sky:
			case Tiles::LayerRendererType::Circle: return _("Textured Background");
			case Tiles::LayerRendererType::BlurBehind: return _("Effects");
			case Tiles::LayerRendererType::XRay: return _("Masked Layer");
			case Tiles::LayerRendererType::Tinted: return _("Tinted Layer");
			case Tiles::LayerRendererType::Solid: return _("Solid Color");
			default: break;
		}
		// By how it moves compared to the sprite layer
		std::uint8_t modelX = layer.Record.SpeedModels & 0x0F;
		if (modelX == std::uint8_t(Tiles::LayerSpeedModel::AlwaysOnTop)) {
			return _("Fixed to Screen");
		}
		if (modelX == std::uint8_t(Tiles::LayerSpeedModel::FitLevel)) {
			return _("Fitted Background");
		}
		float speed = std::max(std::abs(layer.Record.SpeedX), std::abs(layer.Record.SpeedY));
		if (speed > 1.0f) {
			return _("Foreground");
		}
		if (speed < 1.0f) {
			return _("Background");
		}
		// Moves with the sprite layer
		return (index >= 0 ? _f("Layer {}", index + 1) : String(_("Layer")));
	}

	String GetLayerTitle(const DocumentLayer& layer, std::int32_t index)
	{
		return (!layer.Name.empty() ? layer.Name : GetAutoLayerName(layer, index));
	}

	namespace
	{

		// Changes the description of the current layer as an undoable step, consecutive changes are merged
		void ChangeLayer(EditorSession& session, Function<void(Formats::LayerRecord&, String&)>&& change)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			if (layer == nullptr) {
				return;
			}
			Formats::LayerRecord record = layer->Record;
			String name = layer->Name;
			change(record, name);
			session.Execute(std::make_unique<SetLayerPropertiesCommand>(session.GetDocument(), session.GetCurrentLayer(), record, name, _("Layer properties")), true);
		}

		/** Row of the layer list */
		class LayerRow : public UiElement
		{
		public:
			LayerRow(EditorSession& session, LayersPanel& panel, std::int32_t index)
				: _session(session), _panel(panel), _index(index), _dragging(false)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(available.X, context.Px(UiTheme::RowHeight + 3.0f));
			}

			void Draw(UiPainter& painter) override
			{
				LevelDocument& document = _session.GetDocument();
				if (_index >= std::int32_t(document.Layers.size())) {
					return;
				}
				const DocumentLayer& layer = document.Layers[_index];
				bool selected = (_session.GetCurrentLayer() == _index);
				bool hidden = (layer.EditorFlags & DocumentLayer::EditorHidden) != 0;
				bool locked = (layer.EditorFlags & DocumentLayer::EditorLocked) != 0;

				if (selected) {
					painter.RoundRect(Bounds, UiTheme::Selection);
					painter.RoundRectBorder(Bounds, UiTheme::AccentDim);
				} else if (_hoverAmount > 0.0f) {
					Colorf hover = UiTheme::Hover;
					hover.A *= _hoverAmount;
					painter.RoundRect(Bounds, hover);
				}

				float iconSize = painter.Px(UiTheme::IconSize);
				Rectf eye = GetEyeRect(painter.GetScale());
				Rectf lock = GetLockRect(painter.GetScale());
				painter.Icon(hidden ? EditorIcon::EyeClosed : EditorIcon::Eye, eye, hidden ? UiTheme::IconDisabled : UiTheme::IconColor);
				painter.Icon(locked ? EditorIcon::Lock : EditorIcon::Unlock, lock, locked ? UiTheme::Accent : Colorf(1.0f, 1.0f, 1.0f, 0.25f));

				// A name the layer only gets by what it is is dimmer than one it was given
				float x = lock.X + iconSize + painter.Px(UiTheme::Padding);
				Colorf textColor = (hidden ? UiTheme::TextDisabled
					: (layer.IsSpriteLayer() ? UiTheme::TextHeader : (layer.Name.empty() ? UiTheme::TextDim : UiTheme::Text)));
				painter.PushClip(Bounds);
				painter.TextInRect(GetLayerTitle(layer, _index), GetNameRect(painter.GetScale()), textColor);
				painter.PopClip();
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
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				if (event.Clicks >= 2 && GetNameRect(scale).Contains(event.Pos)) {
					BeginRename();
					return false;
				}
				_pressPos = event.Pos;
				_dragging = false;
				return true;
			}

			void OnPointerMove(const UiPointerEvent& event) override
			{
				if (!_pressed) {
					return;
				}
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				// Only a real move starts dragging, a click wobbles a little
				if (!_dragging && std::abs(event.Pos.Y - _pressPos.Y) > 4.0f * scale) {
					_dragging = true;
				}
				if (_dragging) {
					_panel.UpdateLayerDrag(_index, event.Pos);
				}
			}

			void OnPointerUp(const UiPointerEvent& event, bool inside) override
			{
				if (_dragging) {
					_dragging = false;
					_panel.EndLayerDrag(true);
					return;
				}
				if (!inside) {
					return;
				}
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				if (GetEyeRect(scale).Contains(event.Pos)) {
					_session.ToggleLayerFlag(_index, DocumentLayer::EditorHidden);
				} else if (GetLockRect(scale).Contains(event.Pos)) {
					_session.ToggleLayerFlag(_index, DocumentLayer::EditorLocked);
				} else {
					_session.SetCurrentLayer(_index);
				}
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				if (GetEyeRect(scale).Contains(pos)) {
					return _("Show or hide the layer in the editor");
				}
				if (GetLockRect(scale).Contains(pos)) {
					return _("Lock the layer, so it can't be changed");
				}
				_tooltip = _f("Layer {} - double click to rename it, drag it to move it", _index + 1);
				return _tooltip;
			}

		private:
			EditorSession& _session;
			LayersPanel& _panel;
			std::int32_t _index;
			Vector2f _pressPos;
			bool _dragging;
			mutable String _tooltip;

			Rectf GetNameRect(float scale) const
			{
				Rectf lock = GetLockRect(scale);
				float x = lock.X + lock.W + std::round(UiTheme::Padding * scale);
				return Rectf(x, Bounds.Y, Bounds.X + Bounds.W - x, Bounds.H);
			}

			void BeginRename()
			{
				UiSurface* surface = GetSurface();
				LevelDocument& document = _session.GetDocument();
				if (surface == nullptr || _index >= std::int32_t(document.Layers.size())) {
					return;
				}
				const DocumentLayer& layer = document.Layers[_index];
				EditorSession* s = &_session;
				std::int32_t index = _index;
				InlineTextEdit::Show(*surface, GetNameRect(surface->GetScale()), layer.Name, GetAutoLayerName(layer, _index), [s, index](StringView text) {
					LevelDocument& document = s->GetDocument();
					if (index >= std::int32_t(document.Layers.size())) {
						return;
					}
					const DocumentLayer& layer = document.Layers[index];
					String name = text.trimmed();
					if (name != layer.Name) {
						s->Execute(std::make_unique<SetLayerPropertiesCommand>(document, index, layer.Record, name, _("Rename layer")));
					}
				});
			}

			Rectf GetEyeRect(float scale) const
			{
				float iconSize = std::round(UiTheme::IconSize * scale);
				return Rectf(Bounds.X + std::round(2.0f * scale), Bounds.Y, iconSize, Bounds.H);
			}

			Rectf GetLockRect(float scale) const
			{
				Rectf eye = GetEyeRect(scale);
				return Rectf(eye.X + eye.W, Bounds.Y, eye.W, Bounds.H);
			}
		};

		/** Row of the inspector, a name and an editor */
		UiStack* AddPropertyRow(UiElement* parent, StringView label)
		{
			// A form puts the labels in a column as wide as the longest of them
			if (auto* form = runtime_cast<UiForm>(parent)) {
				return form->AddRow(label);
			}
			auto* row = parent->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			auto* labelElement = row->Add<UiLabel>(label, UiTheme::TextDim);
			labelElement->Width = 60.0f;
			return row;
		}

		/** Shows a row of the inspector only when it applies, with its label */
		void ShowRowWhen(UiStack* row, Function<bool()>&& isVisible)
		{
			UiElement* parent = row->GetParent();
			UiElement* target = (parent != nullptr && runtime_cast<UiFormRow>(parent) != nullptr ? parent : row);
			target->IsVisibleFunc = std::move(isVisible);
		}

		std::uint8_t GetRendererType(EditorSession& session)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			return (layer != nullptr ? layer->Record.RendererType : std::uint8_t(Tiles::LayerRendererType::Default));
		}

		Colorf GetLayerColor(EditorSession& session)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			if (layer == nullptr) {
				return Colorf::White;
			}
			const std::uint8_t* color = layer->Record.Color;
			return Colorf(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f);
		}

		/** A byte of the color of the current layer, 0 is red, 3 is alpha */
		UiNumberBox* AddLayerColorByte(UiStack* row, EditorSession& session, std::int32_t i)
		{
			EditorSession* s = &session;
			auto* number = row->Add<UiNumberBox>([s, i]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr ? float(layer->Record.Color[i]) : 0.0f);
			}, [s, i](float value, bool final) {
				std::uint8_t byte = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255));
				ChangeLayer(*s, [i, byte](Formats::LayerRecord& record, String&) { record.Color[i] = byte; });
			}, 0.0f, 255.0f, 1.0f, 0);
			number->Flex = 1.0f;
			return number;
		}

		/** Swatch of the color of the current layer that opens a color picker, alpha is picked too if @p withAlpha */
		UiColorSwatch* AddLayerColorSwatch(UiStack* row, EditorSession& session, bool withAlpha)
		{
			EditorSession* s = &session;
			auto getColor = [s, withAlpha]() {
				Colorf color = GetLayerColor(*s);
				if (!withAlpha) {
					color.A = 1.0f;
				}
				return color;
			};
			auto* swatch = row->Add<UiColorSwatch>(getColor, [s, getColor, withAlpha]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				UiColorPicker::ShowPopup(*surface, surface->GetPointerPos(), getColor, [s, withAlpha](const Colorf& color) {
					std::uint8_t bytes[4] = { std::uint8_t(std::round(color.R * 255.0f)), std::uint8_t(std::round(color.G * 255.0f)),
						std::uint8_t(std::round(color.B * 255.0f)), std::uint8_t(std::round(color.A * 255.0f)) };
					ChangeLayer(*s, [&bytes, withAlpha](Formats::LayerRecord& record, String&) {
						// Without alpha, the last byte means something else and stays
						std::memcpy(record.Color, bytes, withAlpha ? 4 : 3);
					});
				}, withAlpha);
			});
			swatch->Tooltip = _("Pick a color");
			return swatch;
		}
	}

	void AddPickerPanelTabs(UiElement* header, EditorSession& session)
	{
		EditorSession* s = &session;
		struct Tab { const char* Label; EditorIcon Icon; PickerPanel Panel; };
		static const Tab Tabs[] = {
			{ N_("Tiles"), EditorIcon::Tiles, PickerPanel::Tiles },
			{ N_("Events"), EditorIcon::Event, PickerPanel::Events },
		};
		for (const Tab& tab : Tabs) {
			PickerPanel panel = tab.Panel;
			auto* button = header->Add<UiButton>(_(tab.Label), tab.Icon, [s, panel]() {
				s->SetPickerPanel(panel);
				// Events are placed by their own tool
				if (panel == PickerPanel::Events) {
					s->GetTools().SetActive(EditorToolType::Event);
				} else if (s->GetTools().GetChosenType() == EditorToolType::Event) {
					s->GetTools().SetActive(EditorToolType::Brush);
				}
			});
			button->Style = UiButtonStyle::Flat;
			button->Flex = 1.0f;
			button->IsCheckedFunc = [s, panel]() { return (s->GetPickerPanel() == panel); };
		}
	}

	LayersPanel::LayersPanel(EditorSession& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _list(nullptr), _shownCount(-1),
			_dragFrom(-1), _dropSlot(-1)
	{
		// The title is in the header of the panel it's in
		_list = Add<UiScrollView>();
		_list->Flex = 1.0f;

		auto* buttons = Add<UiStack>(UiOrientation::Horizontal, 1.0f);
		for (const char* id : { "layer.add", "layer.remove", "layer.up", "layer.down" }) {
			if (EditorAction* action = session.GetActions().Find(id)) {
				auto* button = buttons->Add<UiButton>(StringView(), action->Icon, [action]() {
					if (action->CanExecute()) {
						action->Execute();
					}
				});
				button->Style = UiButtonStyle::Flat;
				button->Tooltip = action->GetTooltip();
				button->IsEnabledFunc = [action]() {
					return action->CanExecute();
				};
			}
		}
		Rebuild();
	}

	void LayersPanel::Update(float timeMult)
	{
		if (_shownCount != std::int32_t(_session.GetDocument().Layers.size())) {
			Rebuild();
		}
		UiPanel::Update(timeMult);
	}

	void LayersPanel::Rebuild()
	{
		_shownCount = std::int32_t(_session.GetDocument().Layers.size());
		_dragFrom = -1;
		_dropSlot = -1;
		auto* rows = _list->SetContent<UiStack>(UiOrientation::Vertical, 1.0f);
		for (std::int32_t i = 0; i < _shownCount; i++) {
			rows->Add<LayerRow>(_session, *this, i);
		}
	}

	void LayersPanel::UpdateLayerDrag(std::int32_t from, Vector2f pos)
	{
		UiElement* rows = _list->GetContent();
		if (rows == nullptr) {
			return;
		}
		// The gap before the first row whose middle is below the pointer
		std::int32_t count = rows->GetChildCount();
		std::int32_t slot = count;
		for (std::int32_t i = 0; i < count; i++) {
			const Rectf& bounds = rows->GetChild(i)->Bounds;
			if (pos.Y < bounds.Y + bounds.H * 0.5f) {
				slot = i;
				break;
			}
		}
		_dragFrom = from;
		_dropSlot = slot;
	}

	void LayersPanel::EndLayerDrag(bool drop)
	{
		std::int32_t from = _dragFrom, slot = _dropSlot;
		_dragFrom = -1;
		_dropSlot = -1;
		if (!drop || from < 0 || slot < 0) {
			return;
		}
		// The gaps right before and after the layer leave it where it is
		std::int32_t to = (slot > from ? slot - 1 : slot);
		if (to != from) {
			_session.MoveLayerTo(from, to);
		}
	}

	Rectf LayersPanel::GetSlotLine(std::int32_t slot) const
	{
		UiElement* rows = _list->GetContent();
		if (rows == nullptr || rows->GetChildCount() == 0) {
			return Rectf();
		}
		std::int32_t count = rows->GetChildCount();
		const Rectf& row = rows->GetChild(std::min(slot, count - 1))->Bounds;
		float y = (slot < count ? row.Y : row.Y + row.H);
		return Rectf(row.X, y, row.W, 0.0f);
	}

	void LayersPanel::Draw(UiPainter& painter)
	{
		UiPanel::Draw(painter);

		// Where the dragged layer would go, unless it would stay where it is
		if (_dragFrom >= 0 && _dropSlot >= 0 && _dropSlot != _dragFrom && _dropSlot != _dragFrom + 1) {
			Rectf line = GetSlotLine(_dropSlot);
			float thickness = std::max(2.0f, std::round(painter.GetScale() * 2.0f));
			painter.PushClip(_list->Bounds);
			painter.FillRect(Rectf(line.X, std::round(line.Y - thickness * 0.5f), line.W, thickness), UiTheme::Accent, UiLayer::Overlay);
			painter.PopClip();
		}
	}

	InspectorPanel::InspectorPanel(EditorSession& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session)
	{
		// The edge above, between it and the list of layers, is drawn by the splitter that resizes them
		Add<UiLabel>(_("Layer Properties"), UiTheme::TextHeader);

		auto* scroll = Add<UiScrollView>();
		scroll->Flex = 1.0f;
		auto* rows = scroll->SetContent<UiForm>(2.0f, UiThickness(0.0f, 0.0f, 2.0f, 0.0f));

		EditorSession* s = &session;
		auto isOtherLayer = [s]() {
			const DocumentLayer* layer = GetCurrentLayer(*s);
			return (layer != nullptr && !layer->IsSpriteLayer());
		};

		{
			auto* row = AddPropertyRow(rows, _("Name"));
			auto* name = row->Add<UiTextBox>();
			name->Flex = 1.0f;
			name->MaxLength = 64;
			// Without a name, the layer is known by what it is
			name->PlaceholderFunc = [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr ? GetAutoLayerName(*layer, s->GetCurrentLayer()) : String());
			};
			name->TextFunc = [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr ? layer->Name : String());
			};
			name->OnCommit = [s](StringView text) {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				if (layer != nullptr && layer->Name != text) {
					String newName = text;
					ChangeLayer(*s, [&newName](Formats::LayerRecord& record, String& name) { name = newName; });
				}
			};
		}
		{
			auto* row = AddPropertyRow(rows, _("Size"));
			auto* size = row->Add<UiLabel>();
			size->TextFunc = [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr ? String(FormatInteger(layer->GetWidth()) + " × "_s + FormatInteger(layer->GetHeight())) : String());
			};
		}

		{
			// Common ways a layer moves, the speeds and the models are set at once
			struct Preset { const char* Label; float Speed; std::uint8_t Model; bool Repeat; };
			static const Preset Presets[] = {
				{ N_("Foreground"), 1.5f, std::uint8_t(Tiles::LayerSpeedModel::Default), false },
				{ N_("With the sprite layer"), 1.0f, std::uint8_t(Tiles::LayerSpeedModel::Default), false },
				{ N_("Near background"), 0.5f, std::uint8_t(Tiles::LayerSpeedModel::Default), true },
				{ N_("Background"), 0.25f, std::uint8_t(Tiles::LayerSpeedModel::Default), true },
				{ N_("Far background"), 0.125f, std::uint8_t(Tiles::LayerSpeedModel::Default), true },
				{ N_("Fixed to screen"), 0.0f, std::uint8_t(Tiles::LayerSpeedModel::AlwaysOnTop), false },
				{ N_("Fit to level"), 1.0f, std::uint8_t(Tiles::LayerSpeedModel::FitLevel), false },
			};
			auto matches = [](const Formats::LayerRecord& record, const Preset& preset) {
				std::uint8_t models = std::uint8_t(preset.Model | (preset.Model << 4));
				if (record.SpeedModels != models || record.AutoSpeedX != 0.0f || record.AutoSpeedY != 0.0f) {
					return false;
				}
				// Only the models matter for these, not the speeds
				if (preset.Model != std::uint8_t(Tiles::LayerSpeedModel::Default)) {
					return true;
				}
				return (record.SpeedX == preset.Speed && record.SpeedY == preset.Speed);
			};
			auto* row = AddPropertyRow(rows, _("Movement"));
			auto* dropdown = row->Add<UiDropdown>([s, matches]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				if (layer != nullptr) {
					for (const Preset& preset : Presets) {
						if (matches(layer->Record, preset)) {
							return String(_(preset.Label));
						}
					}
				}
				return String(_("Custom"));
			}, [s, matches](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const Preset& preset : Presets) {
					const Preset* p = &preset;
					UiMenuEntry entry;
					entry.Label = _(preset.Label);
					entry.IsChecked = [s, p, matches]() {
						const DocumentLayer* layer = GetCurrentLayer(*s);
						return (layer != nullptr && matches(layer->Record, *p));
					};
					entry.Execute = [s, p]() {
						ChangeLayer(*s, [p](Formats::LayerRecord& record, String&) {
							if (p->Model == std::uint8_t(Tiles::LayerSpeedModel::Default)) {
								record.SpeedX = p->Speed;
								record.SpeedY = p->Speed;
							}
							record.AutoSpeedX = 0.0f;
							record.AutoSpeedY = 0.0f;
							record.SpeedModels = std::uint8_t(p->Model | (p->Model << 4));
							// A background slower than the level has to repeat to cover the view
							if (p->Repeat) {
								record.Flags |= Formats::LayerRecord::RepeatX | Formats::LayerRecord::RepeatY;
							}
						});
					};
					entries.push_back(std::move(entry));
				}
			});
			dropdown->Flex = 1.0f;
			dropdown->IsEnabledFunc = isOtherLayer;
			row->Tooltip = _("Sets the speeds and the models of a common way a layer moves");
		}

		struct FloatProperty {
			const char* Label;
			float Formats::LayerRecord::* Member;
			float Min, Max, Step;
			std::int32_t Decimals;
		};
		static const FloatProperty FloatProperties[] = {
			{ "Speed X", &Formats::LayerRecord::SpeedX, -16.0f, 16.0f, 0.01f, 3 },
			{ "Speed Y", &Formats::LayerRecord::SpeedY, -16.0f, 16.0f, 0.01f, 3 },
			{ "Auto X", &Formats::LayerRecord::AutoSpeedX, -64.0f, 64.0f, 0.05f, 2 },
			{ "Auto Y", &Formats::LayerRecord::AutoSpeedY, -64.0f, 64.0f, 0.05f, 2 },
			{ "Offset X", &Formats::LayerRecord::OffsetX, -32768.0f, 32767.0f, 1.0f, 0 },
			{ "Offset Y", &Formats::LayerRecord::OffsetY, -32768.0f, 32767.0f, 1.0f, 0 },
		};
		for (const FloatProperty& property : FloatProperties) {
			auto* row = AddPropertyRow(rows, _(property.Label));
			float Formats::LayerRecord::* member = property.Member;
			auto* number = row->Add<UiNumberBox>([s, member]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr ? layer->Record.*member : 0.0f);
			}, [s, member](float value, bool final) {
				ChangeLayer(*s, [member, value](Formats::LayerRecord& record, String&) { record.*member = value; });
			}, property.Min, property.Max, property.Step, property.Decimals);
			number->Flex = 1.0f;
			number->IsEnabledFunc = isOtherLayer;
		}
		{
			auto* row = AddPropertyRow(rows, _("Depth"));
			auto* number = row->Add<UiNumberBox>([s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr ? float(layer->Record.Depth) : 0.0f);
			}, [s](float value, bool final) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) { record.Depth = std::int16_t(value); });
			}, -32768.0f, 32767.0f, 1.0f, 0);
			number->Flex = 1.0f;
			number->IsEnabledFunc = isOtherLayer;
		}

		// How the layer moves with the camera, a model for each axis
		struct ModelOption { const char* Label; std::uint8_t Value; };
		static const ModelOption ModelOptions[] = {
			{ N_("Speed"), std::uint8_t(Tiles::LayerSpeedModel::Default) },
			{ N_("Fixed to screen"), std::uint8_t(Tiles::LayerSpeedModel::AlwaysOnTop) },
			{ N_("Fit to level"), std::uint8_t(Tiles::LayerSpeedModel::FitLevel) },
			{ N_("Speed of view size"), std::uint8_t(Tiles::LayerSpeedModel::SpeedMultipliers) },
		};
		for (std::int32_t axis = 0; axis < 2; axis++) {
			auto* row = AddPropertyRow(rows, axis == 0 ? _("Model X") : _("Model Y"));
			std::int32_t shift = axis * 4;
			auto* dropdown = row->Add<UiDropdown>([s, shift]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				std::uint8_t value = (layer != nullptr ? std::uint8_t((layer->Record.SpeedModels >> shift) & 0x0F) : 0);
				for (const ModelOption& option : ModelOptions) {
					if (option.Value == value) {
						return String(_(option.Label));
					}
				}
				return FormatInteger(value);
			}, [s, shift](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const ModelOption& option : ModelOptions) {
					std::uint8_t value = option.Value;
					UiMenuEntry entry;
					entry.Label = _(option.Label);
					entry.IsChecked = [s, shift, value]() {
						const DocumentLayer* layer = GetCurrentLayer(*s);
						return (layer != nullptr && ((layer->Record.SpeedModels >> shift) & 0x0F) == value);
					};
					entry.Execute = [s, shift, value]() {
						ChangeLayer(*s, [shift, value](Formats::LayerRecord& record, String&) {
							record.SpeedModels = std::uint8_t((record.SpeedModels & ~(0x0F << shift)) | (value << shift));
						});
					};
					entries.push_back(std::move(entry));
				}
			});
			dropdown->Flex = 1.0f;
			dropdown->IsEnabledFunc = isOtherLayer;
		}

		{
			struct RendererOption { const char* Label; Tiles::LayerRendererType Type; };
			static const RendererOption RendererOptions[] = {
				{ N_("Tiles"), Tiles::LayerRendererType::Default },
				{ N_("Solid color"), Tiles::LayerRendererType::Solid },
				{ N_("Tinted"), Tiles::LayerRendererType::Tinted },
				{ N_("X-ray"), Tiles::LayerRendererType::XRay },
				{ N_("Blur behind"), Tiles::LayerRendererType::BlurBehind },
				{ N_("Sky"), Tiles::LayerRendererType::Sky },
				{ N_("Circle"), Tiles::LayerRendererType::Circle },
			};
			auto* row = AddPropertyRow(rows, _("Renderer"));
			auto* dropdown = row->Add<UiDropdown>([s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				std::uint8_t value = (layer != nullptr ? layer->Record.RendererType : 0);
				for (const RendererOption& option : RendererOptions) {
					if (std::uint8_t(option.Type) == value) {
						return String(_(option.Label));
					}
				}
				return FormatInteger(value);
			}, [s](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const RendererOption& option : RendererOptions) {
					std::uint8_t value = std::uint8_t(option.Type);
					UiMenuEntry entry;
					entry.Label = _(option.Label);
					entry.IsChecked = [s, value]() {
						const DocumentLayer* layer = GetCurrentLayer(*s);
						return (layer != nullptr && layer->Record.RendererType == value);
					};
					entry.Execute = [s, value]() {
						ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) { record.RendererType = value; });
					};
					entries.push_back(std::move(entry));
				}
			});
			dropdown->Flex = 1.0f;
			dropdown->IsEnabledFunc = isOtherLayer;
		}
		// What the color means depends on the renderer, so it's edited differently for each of them
		auto rendererIs = [s](std::initializer_list<Tiles::LayerRendererType> types) {
			std::uint8_t type = GetRendererType(*s);
			for (Tiles::LayerRendererType t : types) {
				if (std::uint8_t(t) == type) {
					return true;
				}
			}
			return false;
		};
		{
			auto* row = AddPropertyRow(rows, _("Color"));
			AddLayerColorSwatch(row, *s, true)->IsEnabledFunc = isOtherLayer;
			for (std::int32_t i = 0; i < 4; i++) {
				AddLayerColorByte(row, *s, i)->IsEnabledFunc = isOtherLayer;
			}
			row->Tooltip = _("Red, green, blue and alpha, the tiles are multiplied by it, a solid color layer draws its tiles in it");
			ShowRowWhen(row, [rendererIs]() {
				return !rendererIs({ Tiles::LayerRendererType::Tinted, Tiles::LayerRendererType::Sky, Tiles::LayerRendererType::Circle });
			});
		}
		{
			// A tinted layer takes the color from an entry of the palette, so it follows the palette as it changes
			auto* row = AddPropertyRow(rows, _("Tint"));
			row->Add<UiColorSwatch>([s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				if (layer == nullptr) {
					return Colorf::White;
				}
				Colorf color = UiPaletteGrid::ToColor(ContentResolver::Get().GetPalettes()[layer->Record.Color[0]]);
				color.A *= layer->Record.Color[3] / 255.0f;
				return color;
			}, [s]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				UiPaletteGrid::ShowPopup(*surface, surface->GetPointerPos(), []() {
					return ContentResolver::Get().GetPalettes().data();
				}, [s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? std::int32_t(layer->Record.Color[0]) : -1);
				}, [s](std::int32_t index) {
					ChangeLayer(*s, [index](Formats::LayerRecord& record, String&) { record.Color[0] = std::uint8_t(index); });
				});
			})->Tooltip = _("Pick from the palette");
			AddLayerColorByte(row, *s, 0)->Tooltip = _("Index of the palette entry");
			row->Tooltip = _("Entry of the palette the layer is tinted with");
			ShowRowWhen(row, [rendererIs]() { return rendererIs({ Tiles::LayerRendererType::Tinted }); });

			auto* strengthRow = AddPropertyRow(rows, _("Strength"));
			AddLayerColorByte(strengthRow, *s, 3);
			strengthRow->Tooltip = _("How much the layer is tinted, 255 is fully");
			ShowRowWhen(strengthRow, [rendererIs]() { return rendererIs({ Tiles::LayerRendererType::Tinted }); });
		}
		{
			// The background blends towards the color of the horizon in the distance
			auto* row = AddPropertyRow(rows, _("Horizon"));
			AddLayerColorSwatch(row, *s, false);
			for (std::int32_t i = 0; i < 3; i++) {
				AddLayerColorByte(row, *s, i);
			}
			row->Tooltip = _("Red, green and blue of the horizon the background fades into");
			ShowRowWhen(row, [rendererIs]() { return rendererIs({ Tiles::LayerRendererType::Sky, Tiles::LayerRendererType::Circle }); });

			auto* stars = rows->Add<UiCheckBox>(_("Stars"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && layer->Record.Color[3] != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) { record.Color[3] = (value ? 255 : 0); });
			});
			stars->Tooltip = _("Shows a field of stars over the horizon");
			stars->IsVisibleFunc = [rendererIs]() { return rendererIs({ Tiles::LayerRendererType::Sky, Tiles::LayerRendererType::Circle }); };
		}

		struct FlagProperty {
			const char* Label;
			std::uint16_t Flag;
		};
		static const FlagProperty FlagProperties[] = {
			{ "Repeat horizontally", Formats::LayerRecord::RepeatX },
			{ "Repeat vertically", Formats::LayerRecord::RepeatY },
			{ "Inherent offset", Formats::LayerRecord::UseInherentOffset },
			{ "Visible in game", Formats::LayerRecord::Visible },
		};
		for (const FlagProperty& property : FlagProperties) {
			std::uint16_t flag = property.Flag;
			auto* check = rows->Add<UiCheckBox>(_(property.Label), [s, flag]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && (layer->Record.Flags & flag) != 0);
			}, [s, flag](bool value) {
				ChangeLayer(*s, [flag, value](Formats::LayerRecord& record, String&) {
					record.Flags = (value ? (record.Flags | flag) : (record.Flags & ~flag));
				});
			});
			if (flag != Formats::LayerRecord::Visible) {
				check->IsEnabledFunc = isOtherLayer;
			}
		}
	}

	TileGrid::TileGrid(EditorSession& session)
		: ShowUsage(false), _usageMax(0), _usageRevision(UINT32_MAX), _usageSource(0xFF), _session(session), _source(TileRef::DefaultTileSet),
			_tileSize(32.0f), _dragging(false)
	{
	}

	void TileGrid::UpdateUsage() const
	{
		std::uint32_t revision = _session.GetHistory().GetRevision();
		if (revision == _usageRevision && _source == _usageSource) {
			return;
		}
		_usageRevision = revision;
		_usageSource = _source;
		_usage.clear();
		_usage.resize(std::size_t(std::max(GetTileCount(), 0)), 0);
		_usageMax = 0;

		const LevelDocument& document = _session.GetDocument();
		auto count = [this](const TileRef& tile) {
			if (tile.Source == _source && tile.Index < _usage.size() && !tile.IsEmpty()) {
				_usage[tile.Index]++;
			}
		};
		for (const DocumentLayer& layer : document.Layers) {
			for (const TileRef& tile : layer.Tiles) {
				count(tile);
			}
		}
		// A tile only in an animation is used too
		for (const DocumentAnimation& animation : document.Animations) {
			for (const TileRef& frame : animation.Frames) {
				count(frame);
			}
		}
		for (std::int32_t used : _usage) {
			_usageMax = std::max(_usageMax, used);
		}
	}

	void TileGrid::SetSource(std::uint8_t source)
	{
		_source = source;
		_dragging = false;
	}

	std::int32_t TileGrid::GetColumns() const
	{
		return std::max(_session.GetSettings().TilePickerColumns, 1);
	}

	void TileGrid::SetColumns(std::int32_t columns)
	{
		EditorSettings& settings = _session.GetSettings();
		columns = std::clamp(columns, 1, 32);
		if (settings.TilePickerColumns != columns) {
			settings.TilePickerColumns = columns;
			settings.Save();
		}
		_dragging = false;
	}

	std::int32_t TileGrid::GetTileCount() const
	{
		LevelDocument& document = _session.GetDocument();
		if (_source == TileRef::Animated) {
			return std::int32_t(document.Animations.size());
		}
		std::int32_t count = document.GetSourceTileCount(_source);
		if (count < 0 && _source == TileRef::DefaultTileSet) {
			if (Tiles::TileMap* tileMap = _session.GetTileMap()) {
				count = tileMap->GetTileSetTileCount(0);
			}
		}
		return std::max(count, 0);
	}

	float TileGrid::GetTileSize(const UiContext& context, float availableWidth) const
	{
		// As large as the width allows for the number of tiles in a row
		return std::max(std::floor(availableWidth / GetColumns()), 4.0f);
	}

	Vector2f TileGrid::Measure(const UiContext& context, Vector2f available)
	{
		std::int32_t columns = GetColumns();
		_tileSize = GetTileSize(context, available.X);
		std::int32_t rows = (GetTileCount() + columns - 1) / columns;
		return Vector2f(available.X, rows * _tileSize);
	}

	Vector2i TileGrid::GetCellAt(Vector2f pos) const
	{
		return Vector2i(std::int32_t(std::floor((pos.X - Bounds.X) / _tileSize)), std::int32_t(std::floor((pos.Y - Bounds.Y) / _tileSize)));
	}

	void TileGrid::Draw(UiPainter& painter)
	{
		Tiles::TileMap* tileMap = _session.GetTileMap();
		if (tileMap == nullptr) {
			return;
		}

		LevelDocument& document = _session.GetDocument();
		std::int32_t columns = GetColumns();
		std::int32_t count = GetTileCount();
		std::int32_t rows = (count + columns - 1) / columns;
		float width = _tileSize * columns;

		// Only the rows that are seen
		const Rectf& clip = painter.GetClip();
		float clipTop = (clip.H > 0.0f ? clip.Y : Bounds.Y);
		float clipBottom = (clip.H > 0.0f ? clip.Y + clip.H : Bounds.Y + Bounds.H);
		std::int32_t firstRow = std::max(0, std::int32_t((clipTop - Bounds.Y) / _tileSize));
		std::int32_t lastRow = std::min(rows - 1, std::int32_t((clipBottom - Bounds.Y) / _tileSize));

		painter.FillRect(Rectf(Bounds.X, Bounds.Y, width, rows * _tileSize), Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
		for (std::int32_t row = firstRow; row <= lastRow; row++) {
			for (std::int32_t column = 0; column < columns; column++) {
				std::int32_t index = row * columns + column;
				if (index >= count) {
					break;
				}
				TileRef tile{ std::uint16_t(index), _source, 0 };
				Rectf rect(Bounds.X + column * _tileSize, Bounds.Y + row * _tileSize, _tileSize, _tileSize);
				painter.Tile(*tileMap, document.ToTileId(tile), 0, rect);
			}
		}

		if (ShowUsage) {
			// Unused tiles darkened, used ones with a bar as long as how often, on a logarithmic scale
			UpdateUsage();
			float barHeight = std::max(2.0f, std::round(_tileSize / 8.0f));
			float maxLog = std::log(float(std::max(_usageMax, 1)) + 1.0f);
			for (std::int32_t row = firstRow; row <= lastRow; row++) {
				for (std::int32_t column = 0; column < columns; column++) {
					std::int32_t index = row * columns + column;
					if (index >= count || index >= std::int32_t(_usage.size())) {
						break;
					}
					Rectf rect(Bounds.X + column * _tileSize, Bounds.Y + row * _tileSize, _tileSize, _tileSize);
					std::int32_t used = _usage[index];
					if (used == 0) {
						painter.FillRect(rect, Colorf(0.0f, 0.0f, 0.0f, 0.65f), UiLayer::Content);
					} else {
						float fraction = std::log(float(used) + 1.0f) / maxLog;
						painter.FillRect(Rectf(rect.X, rect.Y + rect.H - barHeight, std::max(barHeight, std::round(rect.W * fraction)), barHeight),
							Colorf(0.4f + 0.6f * fraction, 0.9f - 0.5f * fraction, 0.3f, 0.95f), UiLayer::Overlay);
					}
				}
			}
		}

		// The tiles the stamp was picked from, while it's still them
		const TileStamp& stamp = _session.GetStamp();
		if (!stamp.IsEmpty() && stamp.Tiles[0].Source == _source && !_dragging) {
			std::int32_t first = stamp.Tiles[0].Index;
			TileStamp expected = TileStamp::FromTileSet(_source, columns, Recti(first % columns, first / columns, stamp.Width, stamp.Height));
			bool same = (expected.Width == stamp.Width && expected.Height == stamp.Height);
			for (std::size_t i = 0; same && i < stamp.Tiles.size(); i++) {
				same = (stamp.Tiles[i].Index == expected.Tiles[i].Index && stamp.Tiles[i].Source == _source && stamp.Tiles[i].Flags == 0);
			}
			if (same) {
				Rectf rect(Bounds.X + (first % columns) * _tileSize, Bounds.Y + (first / columns) * _tileSize, stamp.Width * _tileSize, stamp.Height * _tileSize);
				painter.FillRect(rect, UiTheme::Selection, UiLayer::Overlay);
				painter.Frame(rect, UiTheme::Accent, std::max(1.0f, std::round(painter.GetScale())), UiLayer::Overlay);
			}
		}

		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		if (_dragging) {
			std::int32_t x1 = std::min(_dragStart.X, _dragEnd.X), y1 = std::min(_dragStart.Y, _dragEnd.Y);
			std::int32_t x2 = std::max(_dragStart.X, _dragEnd.X), y2 = std::max(_dragStart.Y, _dragEnd.Y);
			Rectf rect(Bounds.X + x1 * _tileSize, Bounds.Y + y1 * _tileSize, (x2 - x1 + 1) * _tileSize, (y2 - y1 + 1) * _tileSize);
			painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
		} else if (_hovered) {
			if (UiSurface* surface = GetSurface()) {
				Vector2i cell = GetCellAt(surface->GetPointerPos());
				if (cell.X >= 0 && cell.X < columns && cell.Y >= 0 && cell.Y * columns + cell.X < count) {
					painter.Frame(Rectf(Bounds.X + cell.X * _tileSize, Bounds.Y + cell.Y * _tileSize, _tileSize, _tileSize), UiTheme::Cursor, thickness, UiLayer::Overlay);
				}
			}
		}
	}

	bool TileGrid::AcceptsPointer() const
	{
		return true;
	}

	bool TileGrid::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		Vector2i cell = GetCellAt(event.Pos);
		std::int32_t columns = GetColumns();
		if (cell.X < 0 || cell.X >= columns || cell.Y < 0 || cell.Y * columns + cell.X >= GetTileCount()) {
			return false;
		}
		if (event.Clicks >= 2) {
			// The pixels and the mask of the tile, changed only in this level
			ShowTileEditor(_session, TileRef{ std::uint16_t(cell.Y * columns + cell.X), _source, 0 });
			return false;
		}
		_dragging = true;
		_dragStart = _dragEnd = cell;
		return true;
	}

	void TileGrid::OnPointerMove(const UiPointerEvent& event)
	{
		if (_dragging) {
			Vector2i cell = GetCellAt(event.Pos);
			std::int32_t columns = GetColumns();
			std::int32_t rows = (GetTileCount() + columns - 1) / columns;
			_dragEnd = Vector2i(std::clamp(cell.X, 0, columns - 1), std::clamp(cell.Y, 0, std::max(rows - 1, 0)));
		}
	}

	void TileGrid::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (!_dragging) {
			return;
		}
		_dragging = false;

		std::int32_t x1 = std::min(_dragStart.X, _dragEnd.X), y1 = std::min(_dragStart.Y, _dragEnd.Y);
		std::int32_t x2 = std::max(_dragStart.X, _dragEnd.X), y2 = std::max(_dragStart.Y, _dragEnd.Y);
		TileStamp stamp = TileStamp::FromTileSet(_source, GetColumns(), Recti(x1, y1, x2 - x1 + 1, y2 - y1 + 1));
		std::int32_t count = GetTileCount();
		for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
			if (stamp.Tiles[i].Index >= count) {
				stamp.Present[i] = false;
			}
		}
		_session.SetStamp(std::move(stamp));

		// Picking tiles means painting with them
		EditorToolType tool = _session.GetTools().GetChosenType();
		if (tool != EditorToolType::Brush && tool != EditorToolType::Rectangle && tool != EditorToolType::Fill) {
			_session.GetTools().SetActive(EditorToolType::Brush);
		}
	}

	bool TileGrid::OnWheel(const UiPointerEvent& event)
	{
		if ((event.Modifiers & KeyMod::Ctrl) == 0) {
			return false;
		}
		// Up for fewer, so larger tiles
		SetColumns(GetColumns() + (event.Wheel.Y > 0.0f ? -1 : 1));
		return true;
	}

	StringView TileGrid::GetTooltip(Vector2f pos) const
	{
		Vector2i cell = GetCellAt(pos);
		std::int32_t columns = GetColumns();
		std::int32_t index = cell.Y * columns + cell.X;
		if (cell.X < 0 || cell.X >= columns || cell.Y < 0 || index >= GetTileCount()) {
			return {};
		}
		if (ShowUsage) {
			UpdateUsage();
			std::int32_t used = (index < std::int32_t(_usage.size()) ? _usage[index] : 0);
			_tooltip = (used == 0 ? _f("Tile {}, not used", index) : _fn("Tile {}, used once", "Tile {}, used {} times", used, index, used));
		} else {
			_tooltip = _f("Tile {}", index);
		}
		return _tooltip;
	}

	namespace
	{
		/** Tiles the levels have next to the tiles around the cell under the pointer, a click makes one the brush */
		class SuggestionStrip : public UiElement
		{
		public:
			static constexpr std::int32_t MaxTiles = 8;

			explicit SuggestionStrip(EditorSession& session)
				: _session(session)
			{
				// Always there, so the tiles below don't move when something is suggested
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(available.X, context.Px(UiTheme::ButtonHeight + 4.0f));
			}

			void Update(float timeMult) override
			{
				UiElement::Update(timeMult);
				// While the pointer is over the view, it keeps what it suggested when it moves here to pick one
				ToolPointer pointer;
				if (_session.IsPlaying() || !_session.GetHoverPointer(pointer)) {
					return;
				}
				EditorToolType tool = _session.GetTools().GetActiveType();
				if (tool != EditorToolType::Brush && tool != EditorToolType::Rectangle) {
					_tiles.clear();
					return;
				}
				std::int32_t layerIndex = _session.GetCurrentLayer();
				LayerView view = _session.GetLayerView(layerIndex);
				Vector2i cell;
				LevelDocument& document = _session.GetDocument();
				if (!view.Wrap(pointer.Cell, cell) || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
					return;
				}
				if (cell == _cell && layerIndex == _layer && _session.GetHistory().GetRevision() == _revision) {
					return;
				}
				_cell = cell;
				_layer = layerIndex;
				_revision = _session.GetHistory().GetRevision();
				const DocumentLayer& layer = document.Layers[layerIndex];
				auto at = [&layer](std::int32_t x, std::int32_t y) -> const TileRef* {
					return (layer.Contains(x, y) && !layer.GetTile(x, y).IsEmpty() ? &layer.GetTile(x, y) : nullptr);
				};
				_session.GetSmartTiles().Suggest(at(cell.X - 1, cell.Y), at(cell.X, cell.Y - 1), at(cell.X + 1, cell.Y), at(cell.X, cell.Y + 1), MaxTiles, _tiles);
			}

			void Draw(UiPainter& painter) override
			{
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap == nullptr) {
					return;
				}
				float size = Bounds.H - painter.Px(4.0f);
				// TRANSLATORS: Before tiles suggested to place next to the tiles around the cell under the pointer, keep it short
				float label = painter.MeasureText(_("Next:")) + painter.Px(UiTheme::Spacing);
				painter.TextInRect(_("Next:"), Rectf(Bounds.X, Bounds.Y, label, Bounds.H), _tiles.empty() ? UiTheme::TextDisabled : UiTheme::TextDim);
				if (_tiles.empty()) {
					// TRANSLATORS: In the row of suggested tiles while there's nothing to suggest, keep it short
					String hint = _("point at the level");
					if (label + painter.MeasureText(hint) <= Bounds.W) {
						painter.TextInRect(hint, Rectf(Bounds.X + label, Bounds.Y, Bounds.W - label, Bounds.H), UiTheme::TextDisabled);
					}
				}
				std::int32_t hovered = (_hovered && GetSurface() != nullptr ? GetIndexAt(GetSurface()->GetPointerPos()) : -1);
				for (std::int32_t i = 0; i < std::int32_t(_tiles.size()); i++) {
					Rectf rect(Bounds.X + label + i * (size + painter.Px(2.0f)), Bounds.Y + painter.Px(2.0f), size, size);
					if (rect.X + rect.W > Bounds.X + Bounds.W) {
						break;
					}
					painter.Tile(*tileMap, _session.GetDocument().ToTileId(_tiles[i]), _tiles[i].Flags, rect, 1.0f, UiLayer::Content);
					if (i == hovered) {
						painter.Frame(rect, UiTheme::Accent, std::max(1.0f, std::round(painter.GetScale())), UiLayer::Overlay);
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (index >= 0) {
					_session.SetStamp(TileStamp::FromTile(_tiles[index]));
				}
				return false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				return _("Tiles the levels have next to the tiles around the cell under the pointer, click one to paint with it");
			}

		private:
			EditorSession& _session;
			SmallVector<TileRef, 8> _tiles;
			Vector2i _cell{ -1, -1 };
			std::int32_t _layer = -1;
			std::uint32_t _revision = ~0u;

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				UiContext context = (GetSurface() != nullptr ? GetSurface()->GetContext() : UiContext{ 1.0f });
				float size = Bounds.H - context.Px(4.0f);
				float label = context.MeasureText(_("Next:")) + context.Px(UiTheme::Spacing);
				if (!Bounds.Contains(pos) || pos.X < Bounds.X + label) {
					return -1;
				}
				std::int32_t index = std::int32_t((pos.X - Bounds.X - label) / (size + context.Px(2.0f)));
				return (index >= 0 && index < std::int32_t(_tiles.size()) ? index : -1);
			}
		};
	}

	TilePickerPanel::TilePickerPanel(EditorSession& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _grid(nullptr)
	{
		auto* header = Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto* previous = header->Add<UiButton>(StringView(), EditorIcon::ChevronLeft, [this]() { NextSource(-1); });
		previous->Style = UiButtonStyle::Flat;
		previous->Tooltip = _("Previous tile set");
		auto* name = header->Add<UiLabel>(StringView(), UiTheme::Text, Alignment::Center);
		name->Flex = 1.0f;
		name->TextFunc = [this]() { return GetSourceName(); };
		auto* next = header->Add<UiButton>(StringView(), EditorIcon::ChevronRight, [this]() { NextSource(1); });
		next->Style = UiButtonStyle::Flat;
		next->Tooltip = _("Next tile set");
		header->Add<UiSeparator>(UiOrientation::Vertical);
		auto* zoomOut = header->Add<UiButton>(StringView(), EditorIcon::ZoomOut, [this]() { _grid->SetColumns(_grid->GetColumns() + 1); });
		zoomOut->Style = UiButtonStyle::Flat;
		zoomOut->Tooltip = _("More tiles in a row, smaller");
		auto* zoomIn = header->Add<UiButton>(StringView(), EditorIcon::ZoomIn, [this]() { _grid->SetColumns(_grid->GetColumns() - 1); });
		zoomIn->Style = UiButtonStyle::Flat;
		zoomIn->Tooltip = _("Fewer tiles in a row, larger");
		auto* usage = header->Add<UiButton>(StringView(), EditorIcon::Usage, [this]() { _grid->ShowUsage = !_grid->ShowUsage; });
		usage->Style = UiButtonStyle::Flat;
		usage->IsCheckedFunc = [this]() { return _grid->ShowUsage; };
		usage->Tooltip = _("Show how often the level uses each tile, unused ones are dark");

		Add<SuggestionStrip>(session);

		auto* scroll = Add<UiScrollView>();
		scroll->Flex = 1.0f;
		_grid = scroll->SetContent<TileGrid>(session);
	}

	void TilePickerPanel::Update(float timeMult)
	{
		// An extra tile set that was removed, or animated tiles that are all gone, can't be shown anymore
		LevelDocument& document = _session.GetDocument();
		std::uint8_t source = _grid->GetSource();
		if ((source == TileRef::Animated && document.Animations.empty()) ||
			(source != TileRef::Animated && source != TileRef::DefaultTileSet && source > document.ExtraTileSets.size())) {
			_grid->SetSource(TileRef::DefaultTileSet);
		}
		UiPanel::Update(timeMult);
	}

	String TilePickerPanel::GetSourceName() const
	{
		LevelDocument& document = _session.GetDocument();
		std::uint8_t source = _grid->GetSource();
		if (source == TileRef::Animated) {
			return _("Animated tiles");
		}
		if (source == TileRef::DefaultTileSet) {
			return document.Properties.DefaultTileSet;
		}
		return (source - 1 < std::int32_t(document.ExtraTileSets.size()) ? GetTileSetLabel(document.ExtraTileSets[source - 1].Name) : String());
	}

	void TilePickerPanel::NextSource(std::int32_t direction)
	{
		LevelDocument& document = _session.GetDocument();
		SmallVector<std::uint8_t, 8> sources;
		sources.push_back(TileRef::DefaultTileSet);
		for (std::size_t i = 0; i < document.ExtraTileSets.size() && i < TileRef::LastExtraTileSet; i++) {
			sources.push_back(std::uint8_t(i + 1));
		}
		if (!document.Animations.empty()) {
			sources.push_back(TileRef::Animated);
		}

		std::int32_t current = 0;
		for (std::size_t i = 0; i < sources.size(); i++) {
			if (sources[i] == _grid->GetSource()) {
				current = std::int32_t(i);
			}
		}
		std::int32_t count = std::int32_t(sources.size());
		_grid->SetSource(sources[((current + direction) % count + count) % count]);
	}

	StampPreview::StampPreview(EditorSession& session)
		: _session(session)
	{
	}

	Vector2f StampPreview::Measure(const UiContext& context, Vector2f available)
	{
		return Vector2f(available.X, available.X);
	}

	void StampPreview::Draw(UiPainter& painter)
	{
		painter.RoundRect(Bounds, UiTheme::InputFill);
		const TileStamp& stamp = _session.GetStamp();
		Tiles::TileMap* tileMap = _session.GetTileMap();
		if (stamp.IsEmpty() || tileMap == nullptr) {
			return;
		}

		float inset = painter.Px(2.0f);
		float available = Bounds.W - inset * 2.0f;
		float tileSize = std::floor(std::min(available / stamp.Width, available / stamp.Height));
		if (tileSize < 1.0f) {
			return;
		}
		float x0 = std::round(Bounds.X + (Bounds.W - tileSize * stamp.Width) * 0.5f);
		float y0 = std::round(Bounds.Y + (Bounds.H - tileSize * stamp.Height) * 0.5f);
		LevelDocument& document = _session.GetDocument();
		for (std::int32_t y = 0; y < stamp.Height; y++) {
			for (std::int32_t x = 0; x < stamp.Width; x++) {
				if (stamp.IsPresent(x, y) && !stamp.GetTile(x, y).IsEmpty()) {
					const TileRef& tile = stamp.GetTile(x, y);
					painter.Tile(*tileMap, document.ToTileId(tile), tile.Flags, Rectf(x0 + x * tileSize, y0 + y * tileSize, tileSize, tileSize));
				}
			}
		}
	}

	bool StampPreview::AcceptsPointer() const
	{
		return true;
	}

	StringView StampPreview::GetTooltip(Vector2f pos) const
	{
		const TileStamp& stamp = _session.GetStamp();
		_tooltip = _f("Brush: {} × {} tiles", stamp.Width, stamp.Height);
		return _tooltip;
	}

	StatusBar::StatusBar(EditorSession& session)
		: _session(session)
	{
	}

	Vector2f StatusBar::Measure(const UiContext& context, Vector2f available)
	{
		return Vector2f(available.X, context.Px(UiTheme::StatusBarHeight));
	}

	void StatusBar::Draw(UiPainter& painter)
	{
		painter.FillRect(Bounds, UiTheme::PanelHeader);
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
		painter.FillRect(Rectf(Bounds.X, Bounds.Y, Bounds.W, thickness), UiTheme::PanelBorderDim);

		float padding = painter.Px(UiTheme::Padding * 1.5f);
		Rectf inner(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f, Bounds.H);

		String info;
		LevelDocument& document = _session.GetDocument();
		std::int32_t layerIndex = _session.GetCurrentLayer();
		if (layerIndex >= 0 && layerIndex < std::int32_t(document.Layers.size())) {
			info = GetLayerTitle(document.Layers[layerIndex], layerIndex);
		}

		ToolPointer pointer;
		if (_session.GetHoverPointer(pointer)) {
			LayerView view = _session.GetLayerView(layerIndex);
			Vector2i tile;
			if (view.Wrap(pointer.Cell, tile)) {
				const TileRef& ref = document.Layers[layerIndex].GetTile(tile.X, tile.Y);
				info += "   "_s + FormatInteger(tile.X) + ", "_s + FormatInteger(tile.Y) + "   "_s;
				info += (ref.IsAnimated() ? _f("Animation {}", ref.Index) : _f("Tile {}", document.ToTileId(ref)));
			}

			// The event under the pointer, whichever layer is edited
			std::int32_t spriteLayer = document.GetSpriteLayerIndex();
			if (spriteLayer >= 0 && (_session.AreEventsShown() || _session.IsEditingEvents())) {
				LayerView spriteView = _session.GetLayerView(spriteLayer);
				Vector2i cell = spriteView.WorldToCell(spriteView.ScreenToWorld(pointer.Screen));
				Vector2i gridSize = document.GetEventGridSize();
				if (cell.X >= 0 && cell.Y >= 0 && cell.X < gridSize.X && cell.Y < gridSize.Y) {
					const Formats::EventRecord& event = document.GetEvent(cell.X, cell.Y);
					if (!LevelDocument::IsEmptyEvent(event)) {
						info += "   "_s + EventPreviews::GetEventName(event.Type);
					}
				}
			}
		}
		if (EditorCamera* camera = _session.GetCamera()) {
			info += "   "_s + FormatInteger(std::int32_t(std::round(camera->GetScale() / camera->GetGameplayScale() * 100.0f))) + "%"_s;
		}
		painter.TextInRect(info, inner, UiTheme::Text, Alignment::Right);

		// The hint gets what the information leaves
		float hintWidth = inner.W - painter.MeasureText(info) - padding * 2.0f;
		if (hintWidth > 0.0f) {
			Rectf hint(inner.X, inner.Y, hintWidth, inner.H);
			painter.PushClip(hint);
			// What the buttons of the gamepad do while it's used, how the tool is used by a mouse otherwise
			// What the buttons of the gamepad or the fingers do while they're used, how the tool is used by a mouse otherwise
			StringView text;
			switch (_session.GetInputModality()) {
				case InputModality::Gamepad:
					text = _("A: use - B: erase - X: pick - Y: next tool, hold for all - LB/RB: layer - LT/RT: zoom - Right stick: move view - Back: undo - Start: play");
					break;
				case InputModality::Touch:
					text = _("One finger: use the tool - Two: move and zoom - Tap with two: undo, three: redo - Hold: tools");
					break;
				default:
					text = _session.GetTools().GetActive()->GetHint();
					break;
			}
			painter.TextInRect(text, hint, UiTheme::TextDim);
			painter.PopClip();
		}
	}

	bool StatusBar::AcceptsPointer() const
	{
		return true;
	}
}

#endif
