#include "ShellActions.h"

#if defined(WITH_EDITOR)

#include "EventOutline.h"
#include "ShellCommon.h"
#include "ShellPanels.h"
#include "../Tools/EditorContext.h"
#include "../Document/LevelDocument.h"
#include "../Files/EditorSettings.h"
#include "../Input/ActionRegistry.h"
#include "../Document/Commands.h"
#include "../Document/DocumentChanges.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Utf8.h>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	bool DuplicateCurrentLayer(IEditorContext& session)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t current = session.GetCurrentLayer();
		if (current < 0 || current >= std::int32_t(document.Layers.size())) {
			return false;
		}
		// The file stores the number of layers in a byte
		if (document.Layers.size() >= UINT8_MAX) {
			session.ShowMessage(_("A level can't have more layers"));
			return false;
		}

		DocumentLayer copy = document.Layers[current];
		// A layer of its own, only the sprite layer is the one the game plays on
		copy.Id = MakeLayerId();
		if (copy.IsSpriteLayer()) {
			copy.Record.Kind = Formats::LayerKind::Other;
		}
		copy.Name = _f("{} (copy)", GetLayerTitle(document, current));
		if (copy.Name.size() > 64) {
			// Not in the middle of a character of UTF-8
			copy.Name = String(copy.Name.prefix(Death::Utf8::FitPrefix(copy.Name, 64)));
		}
		copy.EditorFlags &= ~DocumentLayer::EditorLocked;

		std::int32_t index = current + 1;
		session.Execute(InsertLayerCommand::Add(document, index, copy, _("Duplicate Layer")));
		session.SetCurrentLayer(index);
		return true;
	}

	void SoloLayer(IEditorContext& session, std::int32_t layer)
	{
		LevelDocument& document = session.GetDocument();
		if (layer < 0 || layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		bool alreadySolo = ((document.Layers[layer].EditorFlags & DocumentLayer::EditorHidden) == 0);
		for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()) && alreadySolo; i++) {
			alreadySolo = (i == layer || (document.Layers[i].EditorFlags & DocumentLayer::EditorHidden) != 0);
		}
		// Shown alone already, all layers are shown again, otherwise all others are hidden
		for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()); i++) {
			bool hidden = (document.Layers[i].EditorFlags & DocumentLayer::EditorHidden) != 0;
			bool shouldHide = (!alreadySolo && i != layer);
			if (hidden != shouldHide) {
				session.ToggleLayerFlag(i, DocumentLayer::EditorHidden);
			}
		}
	}

	void SetLayerEditorOpacity(IEditorContext& session, std::int32_t layer, std::uint8_t opacity)
	{
		LevelDocument& document = session.GetDocument();
		if (layer < 0 || layer >= std::int32_t(document.Layers.size()) || document.Layers[layer].EditorOpacity == opacity) {
			return;
		}
		// Editor state as whether it's hidden, not a change of the level that is undone
		session.SetEditorState([layer, opacity](LevelDocument& document) {
			document.Layers[layer].EditorOpacity = opacity;
		});
	}

	void AddShellActions(IEditorContext& session, ActionRegistry& actions)
	{
		IEditorContext* s = &session;
		EditorSettings* settings = &session.GetSettings();

		{
			auto& duplicate = actions.Add(ActionIds::LayerDuplicate,_("Duplicate Layer"), EditorIcon::Copy, [s]() { DuplicateCurrentLayer(*s); });
			duplicate.IsEnabled = [s]() {
				std::int32_t layer = s->GetCurrentLayer();
				return (layer >= 0 && layer < std::int32_t(s->GetDocument().Layers.size()) && s->GetDocument().Layers.size() < UINT8_MAX);
			};
			auto& solo = actions.Add(ActionIds::LayerSolo,_("Show Only This Layer"), EditorIcon::Eye, [s]() { SoloLayer(*s, s->GetCurrentLayer()); });
			solo.IsEnabled = [s]() {
				std::int32_t layer = s->GetCurrentLayer();
				return (layer >= 0 && layer < std::int32_t(s->GetDocument().Layers.size()));
			};
		}

		// The frame of what the game shows around the pointer, in a few sizes
		{
			auto& frame = actions.Add(ActionIds::ViewScreenFrame,_("Show Game Screen Frame"), EditorIcon::Fit, [settings]() {
				settings->ShowScreenFrame = !settings->ShowScreenFrame;
				settings->Save();
			});
			frame.IsChecked = [settings]() { return settings->ShowScreenFrame; };
			struct FrameKind { const char* Id; const char* Label; };
			static const FrameKind Kinds[] = {
				{ ActionIds::ViewScreenFrameGame, N_("Screen Frame: Game View") },
				{ ActionIds::ViewScreenFrame4x3, N_("Screen Frame: 4:3") },
				{ ActionIds::ViewScreenFrame16x9, N_("Screen Frame: 16:9") },
				{ ActionIds::ViewScreenFrameSplit, N_("Screen Frame: Split Screen") }
			};
			for (std::int32_t i = 0; i < std::int32_t(sizeof(Kinds) / sizeof(Kinds[0])); i++) {
				auto& kind = actions.Add(Kinds[i].Id, _(Kinds[i].Label), EditorIcon::None, [settings, i]() {
					settings->ScreenFrameKind = i;
					settings->ShowScreenFrame = true;
					settings->Save();
				});
				kind.IsChecked = [settings, i]() { return (settings->ScreenFrameKind == i); };
			}
		}

		// All events in a list
		actions.Add(ActionIds::EventsList,_("Event List"), EditorIcon::Event, [s]() { ShowEventOutline(*s); }).Key = { Keys::F3, 0 };
	}
}

#endif
