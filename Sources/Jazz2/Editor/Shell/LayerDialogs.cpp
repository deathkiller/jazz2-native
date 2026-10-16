#include "LayerDialogs.h"

#if defined(WITH_EDITOR)

#include "ShellPanels.h"
#include "../EditorSession.h"
#include "../Document/Annotations.h"
#include "../Document/Commands.h"
#include "../Document/DocumentSnapshot.h"
#include "../Document/TerrainSplines.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		struct ResizeState
		{
			std::int32_t Layer = 0;
			Vector2i OldSize;
			Vector2i Size;
			// Which column and row of the 3 × 3 the layer stays fixed at, the middle one shares the change on both sides
			Vector2i Anchor = Vector2i(0, 0);
			LevelDocument::ResizeFill Fill = LevelDocument::ResizeFill::Empty;

			Vector2i GetOffset() const {
				auto offset = [](std::int32_t anchor, std::int32_t oldSize, std::int32_t size) {
					return (anchor == 0 ? 0 : (anchor == 1 ? (size - oldSize) / 2 : size - oldSize));
				};
				return Vector2i(offset(Anchor.X, OldSize.X, Size.X), offset(Anchor.Y, OldSize.Y, Size.Y));
			}
		};

		/** One of the 3 × 3 points the layer can stay fixed at, with arrows to where it grows */
		class AnchorButton : public UiElement
		{
		public:
			AnchorButton(std::shared_ptr<ResizeState> state, Vector2i anchor)
				: _state(std::move(state)), _anchor(anchor)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float size = context.Px(16.0f);
				return Vector2f(size, size);
			}

			void Draw(UiPainter& painter) override
			{
				bool checked = (_state->Anchor == _anchor);
				painter.RoundRect(Bounds, checked ? UiTheme::ButtonChecked : (_hoverAmount > 0.0f ? UiTheme::ButtonHover : UiTheme::ButtonFill));
				Vector2f center(Bounds.X + Bounds.W * 0.5f, Bounds.Y + Bounds.H * 0.5f);
				if (checked) {
					painter.Circle(center, std::max(2.0f, Bounds.W * 0.18f), UiTheme::TextOnAccent);
					return;
				}
				// Points away from the fixed point, which way the layer grows
				Vector2f direction(float(_anchor.X - _state->Anchor.X), float(_anchor.Y - _state->Anchor.Y));
				float length = direction.Length();
				if (length > 0.0f && length < 1.5f) {
					direction /= length;
					float reach = Bounds.W * 0.28f;
					Vector2f tip = center + direction * reach;
					float thickness = std::max(1.0f, std::round(painter.GetScale()));
					painter.Line(center - direction * reach, tip, thickness, UiTheme::IconColor, UiLayer::Content);
					Vector2f side(-direction.Y, direction.X);
					painter.Line(tip, tip - direction * reach * 0.6f + side * reach * 0.6f, thickness, UiTheme::IconColor, UiLayer::Content);
					painter.Line(tip, tip - direction * reach * 0.6f - side * reach * 0.6f, thickness, UiTheme::IconColor, UiLayer::Content);
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				if (event.Button == MouseButton::Left) {
					_state->Anchor = _anchor;
				}
				return false;
			}

		private:
			std::shared_ptr<ResizeState> _state;
			Vector2i _anchor;
		};
	}

	void ShowResizeLayerDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		LevelDocument& document = session.GetDocument();
		std::int32_t layerIndex = session.GetCurrentLayer();
		if (surface == nullptr || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}

		const DocumentLayer& layer = document.Layers[layerIndex];
		auto state = std::make_shared<ResizeState>();
		state->Layer = layerIndex;
		state->OldSize = Vector2i(layer.GetWidth(), layer.GetHeight());
		state->Size = state->OldSize;
		// A repeated layer is most likely to be wanted repeated further
		bool repeats = (layer.Record.Flags & (Formats::LayerRecord::RepeatX | Formats::LayerRecord::RepeatY)) != 0;
		state->Fill = (repeats ? LevelDocument::ResizeFill::Repeat : LevelDocument::ResizeFill::Empty);

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_f("Resize Layer - {}", GetLayerTitle(layer, layerIndex)));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);

		{
			auto* row = content->AddRow(_("Now"));
			row->Add<UiLabel>(_f("{} × {} tiles", state->OldSize.X, state->OldSize.Y), UiTheme::TextDim);
		}
		for (std::int32_t axis = 0; axis < 2; axis++) {
			auto* row = content->AddRow(axis == 0 ? _("Width") : _("Height"));
			auto* number = row->Add<UiNumberBox>([state, axis]() {
				return float(axis == 0 ? state->Size.X : state->Size.Y);
			}, [state, axis](float value, bool final) {
				std::int32_t size = std::clamp(std::int32_t(std::round(value)), 1, std::int32_t(UINT16_MAX));
				(axis == 0 ? state->Size.X : state->Size.Y) = size;
			}, 1.0f, float(UINT16_MAX), 1.0f, 0);
			number->Width = 60.0f;
			auto* change = row->Add<UiLabel>(StringView(), UiTheme::TextDim);
			change->TextFunc = [state, axis]() {
				std::int32_t delta = (axis == 0 ? state->Size.X - state->OldSize.X : state->Size.Y - state->OldSize.Y);
				return (delta == 0 ? String() : (delta > 0 ? _f("+{}", delta) : _f("{}", delta)));
			};
		}
		{
			// The point of the layer that stays where it is, it grows or shrinks away from it
			auto* row = content->AddRow(_("Fixed at"));
			auto* grid = row->Add<UiStack>(UiOrientation::Vertical, 1.0f);
			for (std::int32_t y = 0; y < 3; y++) {
				auto* line = grid->Add<UiStack>(UiOrientation::Horizontal, 1.0f);
				for (std::int32_t x = 0; x < 3; x++) {
					line->Add<AnchorButton>(state, Vector2i(x, y));
				}
			}
		}
		{
			struct FillOption { const char* Label; LevelDocument::ResizeFill Fill; };
			static const FillOption FillOptions[] = {
				{ N_("Empty tiles"), LevelDocument::ResizeFill::Empty },
				{ N_("Edge tiles extended"), LevelDocument::ResizeFill::ExtendEdges },
				{ N_("Layer repeated"), LevelDocument::ResizeFill::Repeat },
			};
			auto* row = content->AddRow(_("New tiles"));
			auto* dropdown = row->Add<UiDropdown>([state]() {
				for (const FillOption& option : FillOptions) {
					if (option.Fill == state->Fill) {
						return String(_(option.Label));
					}
				}
				return String();
			}, [state](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const FillOption& option : FillOptions) {
					LevelDocument::ResizeFill fill = option.Fill;
					UiMenuEntry entry;
					entry.Label = _(option.Label);
					entry.IsChecked = [state, fill]() { return (state->Fill == fill); };
					entry.Execute = [state, fill]() { state->Fill = fill; };
					entries.push_back(std::move(entry));
				}
			});
			dropdown->Width = 130.0f;
			row->Tooltip = _("What the layer gets where it grows - the edge tiles go on to the edge, the corner tiles fill the corners");
		}
		if (layer.IsSpriteLayer()) {
			content->Add<UiLabel>(_("Events and the water level move with the tiles"), UiTheme::TextDim);
		}

		dialog->AddButton(_("Resize"), [s, state, dialogPtr]() {
			LevelDocument& document = s->GetDocument();
			if (state->Layer >= std::int32_t(document.Layers.size())) {
				dialogPtr->Close();
				return;
			}
			if (state->Size == state->OldSize && state->GetOffset() == Vector2i(0, 0)) {
				dialogPtr->Close();
				return;
			}
			bool spriteLayer = document.Layers[state->Layer].IsSpriteLayer();
			// The water level is a property of the level
			auto command = SnapshotCommand::Begin(document, (spriteLayer ? (SnapshotParts::Layers | SnapshotParts::Properties) : SnapshotParts::Layers) | SnapshotParts::Chunks, _("Resize layer"));
			if (!document.ResizeLayer(state->Layer, state->Size, state->GetOffset(), state->Fill)) {
				s->ShowMessage(_("The layer can't be that large"));
				return;
			}
			// Ground lines of the layer move with its tiles, and bookmarks and notes with the sprite layer
			Vector2i offset = state->GetOffset();
			TerrainSpline::ResizeLayer(document, state->Layer, state->Size, offset);
			if (spriteLayer && offset != Vector2i(0, 0)) {
				SmallVector<Annotation, 0> annotations;
				Annotation::Read(document, annotations);
				for (Annotation& annotation : annotations) {
					annotation.X += offset.X * 32;
					annotation.Y += offset.Y * 32;
				}
				Annotation::Write(document, annotations);
				// The play-start marker too, it's not undone with the resize, as it isn't by itself either
				Vector2i playStart;
				if (s->GetPlayStart(playStart)) {
					s->SetPlayStart(true, playStart + offset * 32);
				}
			}
			DocumentChanges changes;
			command->End(document, changes);
			s->Execute(std::move(command));
			dialogPtr->Close();
		}, true);
		dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
