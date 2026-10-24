#include "ActionRegistry.h"

#if defined(WITH_EDITOR)

#include "../../../Main.h"
#include "../Files/AtomicFile.h"
#include "../Files/JsonAccess.h"
#include "../Files/JsonFile.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <jsoncpp/json.h>

#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		StringView GetKeyName(Keys key)
		{
			// The side buttons of a mouse are past all keys, they're not in the switch
			if (key == Shortcut::Mouse4) {
				return "Mouse 4"_s;
			} else if (key == Shortcut::Mouse5) {
				return "Mouse 5"_s;
			}
			switch (key) {
				case Keys::A: return "A"_s; case Keys::B: return "B"_s; case Keys::C: return "C"_s; case Keys::D: return "D"_s;
				case Keys::E: return "E"_s; case Keys::F: return "F"_s; case Keys::G: return "G"_s; case Keys::H: return "H"_s;
				case Keys::I: return "I"_s; case Keys::J: return "J"_s; case Keys::K: return "K"_s; case Keys::L: return "L"_s;
				case Keys::M: return "M"_s; case Keys::N: return "N"_s; case Keys::O: return "O"_s; case Keys::P: return "P"_s;
				case Keys::Q: return "Q"_s; case Keys::R: return "R"_s; case Keys::S: return "S"_s; case Keys::T: return "T"_s;
				case Keys::U: return "U"_s; case Keys::V: return "V"_s; case Keys::W: return "W"_s; case Keys::X: return "X"_s;
				case Keys::Y: return "Y"_s; case Keys::Z: return "Z"_s;
				case Keys::D0: return "0"_s; case Keys::D1: return "1"_s; case Keys::D2: return "2"_s; case Keys::D3: return "3"_s;
				case Keys::D4: return "4"_s; case Keys::D5: return "5"_s; case Keys::D6: return "6"_s; case Keys::D7: return "7"_s;
				case Keys::D8: return "8"_s; case Keys::D9: return "9"_s;
				// The keys of the numeric keypad are keys of their own, they can be given to other actions than the digits
				case Keys::NumPad0: return "Num 0"_s; case Keys::NumPad1: return "Num 1"_s; case Keys::NumPad2: return "Num 2"_s;
				case Keys::NumPad3: return "Num 3"_s; case Keys::NumPad4: return "Num 4"_s; case Keys::NumPad5: return "Num 5"_s;
				case Keys::NumPad6: return "Num 6"_s; case Keys::NumPad7: return "Num 7"_s; case Keys::NumPad8: return "Num 8"_s;
				case Keys::NumPad9: return "Num 9"_s;
				case Keys::NumPadPeriod: return "Num ."_s; case Keys::NumPadDivide: return "Num /"_s; case Keys::NumPadMultiply: return "Num *"_s;
				case Keys::F1: return "F1"_s; case Keys::F2: return "F2"_s; case Keys::F3: return "F3"_s; case Keys::F4: return "F4"_s;
				case Keys::F5: return "F5"_s; case Keys::F6: return "F6"_s; case Keys::F7: return "F7"_s; case Keys::F8: return "F8"_s;
				case Keys::F9: return "F9"_s; case Keys::F10: return "F10"_s; case Keys::F11: return "F11"_s; case Keys::F12: return "F12"_s;
				case Keys::Delete: return "Del"_s;
				case Keys::Backspace: return "Backspace"_s;
				case Keys::Return: case Keys::NumPadEnter: return "Enter"_s;
				case Keys::Escape: return "Esc"_s;
				case Keys::Space: return "Space"_s;
				case Keys::Tab: return "Tab"_s;
				case Keys::Home: return "Home"_s;
				case Keys::End: return "End"_s;
				case Keys::PageUp: return "PgUp"_s;
				case Keys::PageDown: return "PgDn"_s;
				case Keys::Plus: case Keys::NumPadPlus: case Keys::Equals: return "+"_s;
				case Keys::Minus: case Keys::NumPadMinus: return "-"_s;
				// The comma picks the tile under the pointer in the Multi-Layer Level Editor
				case Keys::Comma: return ","_s;
				case Keys::Period: return "."_s;
				case Keys::Left: return "Left"_s;
				case Keys::Right: return "Right"_s;
				case Keys::Up: return "Up"_s;
				case Keys::Down: return "Down"_s;
				default: return {};
			}
		}

		// Plus, minus and Enter of the numeric keypad are the same keys for a shortcut as the others, there's a shortcut
		// of only one of each; its digits are keys of their own, each of them can be given to an action, see GetKeyName()
		bool AreSameKey(Keys a, Keys b)
		{
			auto normalize = [](Keys key) {
				switch (key) {
					case Keys::Equals: case Keys::NumPadPlus: return Keys::Plus;
					case Keys::NumPadMinus: return Keys::Minus;
					case Keys::NumPadEnter: return Keys::Return;
					default: return key;
				}
			};
			return (normalize(a) == normalize(b));
		}
	}

	Shortcut Shortcut::FromEvent(const KeyboardEvent& event)
	{
		Shortcut shortcut;
		shortcut.Key = event.sym;
		if ((event.mod & KeyMod::Ctrl) != 0) {
			shortcut.Modifiers |= KeyMod::Ctrl;
		}
#if defined(DEATH_TARGET_APPLE)
		// Cmd is what Ctrl is elsewhere
		if ((event.mod & KeyMod::Super) != 0) {
			shortcut.Modifiers |= KeyMod::Ctrl;
		}
#endif
		if ((event.mod & KeyMod::Shift) != 0) {
			shortcut.Modifiers |= KeyMod::Shift;
		}
		if ((event.mod & KeyMod::Alt) != 0) {
			shortcut.Modifiers |= KeyMod::Alt;
		}
		return shortcut;
	}

	Keys Shortcut::GetMouseButtonKey(MouseButton button)
	{
		switch (button) {
			case MouseButton::Fourth: return Mouse4;
			case MouseButton::Fifth: return Mouse5;
			default: return Keys::Unknown;
		}
	}

	bool Shortcut::Parse(StringView text, Shortcut& result)
	{
		struct Prefix { StringView Text; std::int32_t Modifier; };
		static const Prefix Prefixes[] = {
			{ "Ctrl+"_s, KeyMod::Ctrl }, { "Cmd+"_s, KeyMod::Ctrl }, { "Shift+"_s, KeyMod::Shift }, { "Alt+"_s, KeyMod::Alt }
		};

		// Small and capital letters are the same, "ctrl+s" is typed into a search too
		auto equalsIgnoreCase = [](StringView a, StringView b) {
			if (a.size() != b.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.size(); i++) {
				char ca = (a[i] >= 'A' && a[i] <= 'Z' ? char(a[i] - 'A' + 'a') : a[i]);
				char cb = (b[i] >= 'A' && b[i] <= 'Z' ? char(b[i] - 'A' + 'a') : b[i]);
				if (ca != cb) {
					return false;
				}
			}
			return true;
		};

		std::int32_t modifiers = 0;
		StringView rest = text.trimmed();
		bool found = true;
		while (found) {
			found = false;
			for (const Prefix& prefix : Prefixes) {
				// "Ctrl++" is Ctrl with the plus key, a prefix is only taken while something is left after it
				if (rest.size() > prefix.Text.size() && equalsIgnoreCase(rest.prefix(prefix.Text.size()), prefix.Text)) {
					modifiers |= prefix.Modifier;
					rest = rest.exceptPrefix(prefix.Text.size());
					found = true;
					break;
				}
			}
		}
		if (rest.empty()) {
			return false;
		}
		for (std::int32_t i = 0; i < std::int32_t(Keys::Count); i++) {
			if (equalsIgnoreCase(GetKeyName(Keys(i)), rest)) {
				result.Key = Keys(i);
				result.Modifiers = modifiers;
				return true;
			}
		}
		for (Keys key : { Mouse4, Mouse5 }) {
			if (equalsIgnoreCase(GetKeyName(key), rest)) {
				result.Key = key;
				result.Modifiers = modifiers;
				return true;
			}
		}
		return false;
	}

	bool Shortcut::Matches(const KeyboardEvent& event) const
	{
		if (Key == Keys::Unknown || !AreSameKey(Key, event.sym)) {
			return false;
		}
#if !defined(DEATH_TARGET_APPLE)
		// A key pressed with the system key is meant for the system, Win+S isn't S
		if ((event.mod & KeyMod::Super) != 0) {
			return false;
		}
#endif
		return (FromEvent(event).Modifiers == Modifiers);
	}

	bool Shortcut::IsSameAs(const Shortcut& other) const
	{
		if (Key == Keys::Unknown || other.Key == Keys::Unknown) {
			return (Key == other.Key);
		}
		return (AreSameKey(Key, other.Key) && Modifiers == other.Modifiers);
	}

	String Shortcut::ToString() const
	{
		StringView name = GetKeyName(Key);
		if (name.empty()) {
			return {};
		}
		String result;
		if ((Modifiers & KeyMod::Ctrl) != 0) {
#if defined(DEATH_TARGET_APPLE)
			result += "Cmd+"_s;
#else
			result += "Ctrl+"_s;
#endif
		}
		if ((Modifiers & KeyMod::Shift) != 0) {
			result += "Shift+"_s;
		}
		if ((Modifiers & KeyMod::Alt) != 0) {
			result += "Alt+"_s;
		}
		result += name;
		return result;
	}

	bool EditorAction::CanExecute()
	{
		return (Execute && (!IsEnabled || IsEnabled()));
	}

	String EditorAction::GetTooltip() const
	{
		String shortcut = Key.ToString();
		return (shortcut.empty() ? Label : String(Label + " ("_s + shortcut + ")"_s));
	}

	EditorAction& ActionRegistry::Add(StringView id, StringView label, EditorIcon icon, Function<void()>&& execute)
	{
		auto& action = _actions.emplace_back(std::make_unique<EditorAction>());
		action->Id = id;
		action->Label = label;
		action->Icon = icon;
		action->Execute = Death::move(execute);
		return *action;
	}

	EditorAction* ActionRegistry::Find(StringView id)
	{
		for (auto& action : _actions) {
			if (action->Id == id) {
				return action.get();
			}
		}
#if !defined(WITH_MULTIPLAYER)
		// The menus ask for the game modes and the reachability without multiplayer too, they're just left out then
		if (id.hasPrefix("play."_s)) {
			StringView mode = id.exceptPrefix("play."_s);
			if (mode.hasPrefix("players"_s) || mode == "alone"_s || mode == "cooperation"_s || mode == "battle"_s ||
				mode == "teamBattle"_s || mode == "race"_s || mode == "treasureHunt"_s || mode == "captureTheFlag"_s) {
				return nullptr;
			}
		} else if (id.hasPrefix("view.reach"_s)) {
			return nullptr;
		}
#endif
		// A typo in an identifier would only leave a menu item or a button out, so it's said once
		for (const String& missing : _missingIds) {
			if (missing == id) {
				return nullptr;
			}
		}
		_missingIds.emplace_back(id);
		LOGW("Editor action \"{}\" doesn't exist", id);
		return nullptr;
	}

	bool ActionRegistry::Execute(StringView id)
	{
		EditorAction* action = Find(id);
		if (action == nullptr || !action->CanExecute()) {
			return false;
		}
		action->Execute();
		return true;
	}

	EditorAction* ActionRegistry::FindByShortcut(const Shortcut& shortcut, const EditorAction* except)
	{
		if (shortcut.Key == Keys::Unknown) {
			return nullptr;
		}
		for (auto& action : _actions) {
			if (action.get() == except || (except != nullptr && !ScopesOverlap(action->Scope, except->Scope))) {
				continue;
			}
			if (action->Key.IsSameAs(shortcut) || action->AlternateKey.IsSameAs(shortcut)) {
				return action.get();
			}
		}
		return nullptr;
	}

	bool ActionRegistry::HasConflict(const EditorAction& action) const
	{
		for (const auto& other : _actions) {
			// The key of an action of the game and one of the editor are never pressed for both
			if (other.get() == &action || !ScopesOverlap(other->Scope, action.Scope)) {
				continue;
			}
			for (const Shortcut* shortcut : { &action.Key, &action.AlternateKey }) {
				if (shortcut->Key != Keys::Unknown && (other->Key.IsSameAs(*shortcut) || other->AlternateKey.IsSameAs(*shortcut))) {
					return true;
				}
			}
		}
		return false;
	}

	void ActionRegistry::CaptureDefaults()
	{
		for (auto& action : _actions) {
			action->DefaultKey = action->Key;
			action->DefaultAlternateKey = action->AlternateKey;
		}
	}

	void ActionRegistry::ResetToDefaults()
	{
		for (auto& action : _actions) {
			action->Key = action->DefaultKey;
			action->AlternateKey = action->DefaultAlternateKey;
		}
	}

	namespace
	{
		// Reads a shortcut as the file has it: text, null for none, or the [key, modifiers] numbers of older versions,
		// returns false if it can't be read
		bool ReadShortcut(const Json::Value& value, Shortcut& result)
		{
			if (value.isNull()) {
				result = Shortcut();
				return true;
			}
			if (value.isString()) {
				return Shortcut::Parse(JsonAccess::ToString(value), result);
			}
			if (value.isArray() && value.size() == 2) {
				// The number of a key changes when a key is added to the list, it's only taken if it still has a name
				std::int32_t key = JsonAccess::ToInt(value[0], -1);
				std::int32_t modifiers = JsonAccess::ToInt(value[1], -1);
				if (key < 0 || key >= std::int32_t(Keys::Count) || modifiers < 0 || GetKeyName(Keys(key)).empty()) {
					return false;
				}
				result.Key = Keys(key);
				result.Modifiers = modifiers & (KeyMod::Ctrl | KeyMod::Shift | KeyMod::Alt);
				return true;
			}
			return false;
		}

		Json::Value WriteShortcut(const Shortcut& shortcut)
		{
			String text = shortcut.ToString();
			if (shortcut.Key == Keys::Unknown || text.empty()) {
				return Json::Value(Json::nullValue);
			}
			return Json::Value(std::string(text.data(), text.size()));
		}
	}

	bool ActionRegistry::LoadOverrides(StringView path)
	{
		Json::Value doc;
		if (JsonFile::Read(path, doc, 1024 * 1024, "Editor shortcuts"_s) != JsonFile::ReadResult::Ok) {
			return false;
		}
		for (auto& action : _actions) {
			std::string id(action->Id.data(), action->Id.size());
			if (!doc.isMember(id)) {
				continue;
			}
			const Json::Value& value = JsonAccess::Member(doc, id.c_str());
			if (value.isObject()) {
				// Both shortcuts, each may be missing, it keeps its default then
				const Json::Value& key = JsonAccess::Member(value, "Key");
				if (!key.isNull() || value.isMember("Key")) {
					Shortcut shortcut;
					if (ReadShortcut(key, shortcut)) {
						action->Key = shortcut;
					} else {
						LOGW("Editor shortcut of \"{}\" cannot be read", action->Id);
					}
				}
				const Json::Value& alternate = JsonAccess::Member(value, "Alternate");
				if (!alternate.isNull() || value.isMember("Alternate")) {
					Shortcut shortcut;
					if (ReadShortcut(alternate, shortcut)) {
						action->AlternateKey = shortcut;
					} else {
						LOGW("Editor shortcut of \"{}\" cannot be read", action->Id);
					}
				}
				continue;
			}
			Shortcut shortcut;
			if (ReadShortcut(value, shortcut)) {
				action->Key = shortcut;
			} else {
				LOGW("Editor shortcut of \"{}\" cannot be read", action->Id);
			}
		}
		return true;
	}

	bool ActionRegistry::SaveOverrides(StringView path) const
	{
		return WriteKeymap(path, false);
	}

	bool ActionRegistry::ExportKeymap(StringView path) const
	{
		return WriteKeymap(path, true);
	}

	bool ActionRegistry::ImportKeymap(StringView path)
	{
		// Read into a copy first, a file that can't be read leaves the shortcuts as they were
		SmallVector<std::pair<Shortcut, Shortcut>, 0> before;
		for (const auto& action : _actions) {
			before.push_back({ action->Key, action->AlternateKey });
		}
		ResetToDefaults();
		if (!LoadOverrides(path)) {
			for (std::size_t i = 0; i < _actions.size(); i++) {
				_actions[i]->Key = before[i].first;
				_actions[i]->AlternateKey = before[i].second;
			}
			return false;
		}
		return true;
	}

	void ActionRegistry::ApplyPreset(KeymapPreset preset, SmallVectorImpl<String>* cleared)
	{
		// With Alternate the other shortcut of the action is set, the one it has stays
		struct Binding { const char* Id; Keys Key; std::int32_t Modifiers; bool Alternate; };
		// Like Jazz Creation Station: F flips a tile horizontally and I vertically. The eyedropper leaves I, Alt picks
		// with any tool anyway. The number keys choose the layers, B grabs a block and Ctrl+E / Shift+E take and place the
		// event under the pointer as they do by default.
		static const Binding JcsBindings[] = {
			{ ActionIds::StampFlipX, Keys::F, 0, false }, { ActionIds::StampFlipY, Keys::I, 0, false }, { ActionIds::ToolEyedropper, Keys::Unknown, 0, false },
		};
		// Like the Multi-Layer Level Editor, as JCS and: Ctrl+C and Ctrl+X make the selection the brush (it has no other
		// clipboard), Ctrl+V shows the events, Ctrl+R plays, Ctrl+Shift+R from the pointer, Ctrl+L and Shift+L go
		// through the layers, Ctrl+plus and minus zoom, the comma picks the tile under the pointer (with Shift its event
		// too), Backspace makes the brush an empty tile, E chooses an event for the tile under the pointer and M held shows
		// the masks. It can't turn the stamp, R stays the rectangle.
		static const Binding MlleBindings[] = {
			{ ActionIds::EditCopyToBrush, Keys::C, KeyMod::Ctrl, false }, { ActionIds::EditCutToBrush, Keys::X, KeyMod::Ctrl, false },
			{ ActionIds::ViewEvents, Keys::V, KeyMod::Ctrl, true },
			{ ActionIds::PlayResume, Keys::R, KeyMod::Ctrl, true }, { ActionIds::PlayHere, Keys::R, KeyMod::Ctrl | KeyMod::Shift, true },
			{ ActionIds::LayerPrevious, Keys::L, KeyMod::Ctrl, true }, { ActionIds::LayerNext, Keys::L, KeyMod::Shift, true },
			{ ActionIds::ViewZoomIn, Keys::Plus, KeyMod::Ctrl, true }, { ActionIds::ViewZoomOut, Keys::Minus, KeyMod::Ctrl, true },
			{ ActionIds::StampPickTile, Keys::Comma, 0, false }, { ActionIds::StampPickTileEvent, Keys::Comma, KeyMod::Shift, false },
			{ ActionIds::StampBlank, Keys::Backspace, 0, false }, { ActionIds::EventsChooseAtPointer, Keys::E, 0, false },
			{ ActionIds::ViewPeekMasks, Keys::M, 0, false },
		};

		ResetToDefaults();
		auto apply = [this, cleared](const Binding* bindings, std::size_t count) {
			for (std::size_t i = 0; i < count; i++) {
				// Not Find(), a preset may name actions a build leaves out, which isn't worth a warning
				EditorAction* action = nullptr;
				for (auto& candidate : _actions) {
					if (candidate->Id == StringView(bindings[i].Id)) {
						action = candidate.get();
						break;
					}
				}
				if (action == nullptr) {
					continue;
				}
				Shortcut shortcut;
				shortcut.Key = bindings[i].Key;
				shortcut.Modifiers = bindings[i].Modifiers;
				if (shortcut.Key != Keys::Unknown) {
					// Whatever had the key before is left without it, as when a shortcut is changed by hand
					while (EditorAction* other = FindByShortcut(shortcut, action)) {
						if (other->Key.IsSameAs(shortcut)) {
							other->Key = Shortcut();
						}
						if (other->AlternateKey.IsSameAs(shortcut)) {
							other->AlternateKey = Shortcut();
						}
						if (cleared != nullptr) {
							cleared->push_back(other->Label);
						}
					}
				}
				if (bindings[i].Alternate) {
					action->AlternateKey = shortcut;
				} else {
					action->Key = shortcut;
				}
			}
		};
		if (preset == KeymapPreset::Jcs || preset == KeymapPreset::Mlle) {
			apply(JcsBindings, sizeof(JcsBindings) / sizeof(JcsBindings[0]));
		}
		if (preset == KeymapPreset::Mlle) {
			apply(MlleBindings, sizeof(MlleBindings) / sizeof(MlleBindings[0]));
		}
	}

	std::int32_t ActionRegistry::SelfTest()
	{
		std::int32_t problems = 0;

		// Shortcuts written as text are read back the same, in any case of the letters
		struct ParseCase { const char* Text; Keys Key; std::int32_t Modifiers; };
		static const ParseCase Cases[] = {
			{ "Ctrl+S", Keys::S, KeyMod::Ctrl }, { "ctrl+shift+z", Keys::Z, KeyMod::Ctrl | KeyMod::Shift }, { "Ctrl++", Keys::Plus, KeyMod::Ctrl },
			{ "F5", Keys::F5, 0 }, { "Alt+F5", Keys::F5, KeyMod::Alt }, { "Cmd+O", Keys::O, KeyMod::Ctrl }, { "PgDn", Keys::PageDown, 0 },
			{ "Num 0", Keys::NumPad0, 0 }, { "ctrl+num 7", Keys::NumPad7, KeyMod::Ctrl }, { "Num *", Keys::NumPadMultiply, 0 },
			{ ",", Keys::Comma, 0 }, { "Shift+,", Keys::Comma, KeyMod::Shift },
			{ "Mouse 4", Shortcut::Mouse4, 0 }, { "ctrl+mouse 5", Shortcut::Mouse5, KeyMod::Ctrl },
		};
		for (const ParseCase& c : Cases) {
			Shortcut shortcut;
			if (!Shortcut::Parse(StringView(c.Text), shortcut) || shortcut.Key != c.Key || shortcut.Modifiers != c.Modifiers) {
				LOGE("Shortcut \"{}\" isn't read right", c.Text);
				problems++;
				continue;
			}
			Shortcut again;
			if (!Shortcut::Parse(shortcut.ToString(), again) || !again.IsSameAs(shortcut)) {
				LOGE("Shortcut \"{}\" isn't written so it can be read again", c.Text);
				problems++;
			}
		}
		for (const char* bad : { "", "Ctrl+", "Ctrl+Nothing", "Shift", "Mouse 3" }) {
			Shortcut shortcut;
			if (Shortcut::Parse(StringView(bad), shortcut)) {
				LOGE("\"{}\" is read as a shortcut", bad);
				problems++;
			}
		}

		// A preset takes the key from what had it before, and starts from the defaults
		ActionRegistry registry;
		registry.Add("tool.eyedropper"_s, "Eyedropper"_s, EditorIcon::None, []() {}).Key = { Keys::I, 0 };
		registry.Add("stamp.flipX"_s, "Flip X"_s, EditorIcon::None, []() {}).Key = { Keys::X, 0 };
		registry.Add("stamp.flipY"_s, "Flip Y"_s, EditorIcon::None, []() {}).Key = { Keys::Y, 0 };
		registry.Add("layer.select1"_s, "Layer 1"_s, EditorIcon::None, []() {}).Key = { Keys::D1, 0 };
		registry.Add("file.save"_s, "Save"_s, EditorIcon::None, []() {}).Key = { Keys::S, KeyMod::Ctrl };
		registry.Find("file.save"_s)->WorksWhileTyping = true;
		registry.CaptureDefaults();
		SmallVector<String, 4> cleared;
		registry.ApplyPreset(KeymapPreset::Jcs, &cleared);
		if (registry.Find("stamp.flipY"_s)->Key.Key != Keys::I || registry.Find("tool.eyedropper"_s)->Key.Key != Keys::Unknown ||
			registry.Find("layer.select1"_s)->Key.Key != Keys::D1 || registry.HasConflict(*registry.Find("stamp.flipY"_s))) {
			LOGE("The JCS preset of shortcuts isn't applied right");
			problems++;
		}
		registry.ApplyPreset(KeymapPreset::Default);
		if (registry.Find("tool.eyedropper"_s)->Key.Key != Keys::I || registry.Find("stamp.flipY"_s)->Key.Key != Keys::Y ||
			registry.Find("layer.select1"_s)->Key.Key != Keys::D1) {
			LOGE("The default shortcuts aren't brought back by their preset");
			problems++;
		}
		KeyboardEvent save;
		save.sym = Keys::S;
		save.mod = KeyMod::LCtrl;
		KeyboardEvent other;
		other.sym = Keys::X;
		other.mod = 0;
		if (!registry.MatchesWhileTyping(save) || registry.MatchesWhileTyping(other)) {
			LOGE("Shortcuts that work while typing aren't told apart");
			problems++;
		}

		// The preset of MLLE gives the key of copying to the copy into the brush, and an action that keeps its key another
		// one; an action that ignores a key repeating runs once while the key is held
		{
			ActionRegistry mlle;
			mlle.Add(ActionIds::EditCopy, "Copy"_s, EditorIcon::None, []() {}).Key = { Keys::C, KeyMod::Ctrl };
			mlle.Add(ActionIds::EditCopyToBrush, "Copy into Brush"_s, EditorIcon::None, []() {});
			mlle.Add(ActionIds::PlayResume, "Play"_s, EditorIcon::None, []() {}).Key = { Keys::F5, 0 };
			std::int32_t grabbed = 0;
			EditorAction& grab = mlle.Add(ActionIds::EditGrabBlock, "Grab Block"_s, EditorIcon::None, [&grabbed]() { grabbed++; });
			grab.Key = { Keys::B, 0 };
			grab.IgnoresRepeat = true;
			mlle.CaptureDefaults();
			mlle.ApplyPreset(KeymapPreset::Mlle);
			const EditorAction* play = mlle.Find(ActionIds::PlayResume);
			if (mlle.Find(ActionIds::EditCopyToBrush)->Key.Key != Keys::C || mlle.Find(ActionIds::EditCopy)->Key.Key != Keys::Unknown ||
				play->Key.Key != Keys::F5 || play->AlternateKey.Key != Keys::R || play->AlternateKey.Modifiers != KeyMod::Ctrl) {
				LOGE("The MLLE preset of shortcuts isn't applied right");
				problems++;
			}
			KeyboardEvent b;
			b.sym = Keys::B;
			b.mod = 0;
			if (!mlle.HandleKey(b) || !mlle.HandleKey(b, ActionScope::Edit, true) || grabbed != 1) {
				LOGE("A key held runs an action that ignores repeating more than once");
				problems++;
			}
		}

		// One key runs an action while editing and another while playing, they don't conflict, and a key of the editor
		// isn't taken from the game
		ActionRegistry scoped;
		std::int32_t edited = 0, played = 0;
		EditorAction& editAction = scoped.Add("view.example"_s, "Edit Action"_s, EditorIcon::None, [&edited]() { edited++; });
		editAction.Key = { Keys::F6, 0 };
		EditorAction& teleport = scoped.Add("play.teleport"_s, "Teleport"_s, EditorIcon::None, [&played]() { played++; });
		teleport.Key = { Keys::F6, 0 };
		teleport.Scope = ActionScope::Play;
		EditorAction& undo = scoped.Add("edit.undo"_s, "Undo"_s, EditorIcon::None, []() {});
		undo.Key = { Keys::Z, KeyMod::Ctrl };
		EditorAction& help = scoped.Add("help.shortcuts"_s, "Shortcuts"_s, EditorIcon::None, []() {});
		help.Key = { Keys::F6, 0 };
		help.Scope = ActionScope::Both;
		KeyboardEvent f6;
		f6.sym = Keys::F6;
		f6.mod = 0;
		KeyboardEvent ctrlZ;
		ctrlZ.sym = Keys::Z;
		ctrlZ.mod = KeyMod::LCtrl;
		bool editRan = scoped.HandleKey(f6, ActionScope::Edit);
		bool playRan = scoped.HandleKey(f6, ActionScope::Play);
		if (!editRan || !playRan || edited != 1 || played != 1 || scoped.HandleKey(ctrlZ, ActionScope::Play) ||
			scoped.FindByShortcut(teleport.Key, &editAction) != &help || !scoped.HasConflict(teleport) || !scoped.HasConflict(help)) {
			LOGE("Shortcuts of editing and playing aren't told apart");
			problems++;
		}
		help.Key = Shortcut();
		if (scoped.HasConflict(editAction) || scoped.HasConflict(teleport) || scoped.FindByShortcut(teleport.Key, &editAction) != nullptr) {
			LOGE("Shortcuts of editing and playing conflict with each other");
			problems++;
		}

		// A side button of the mouse is passed on as the key that stands for it
		std::int32_t pressed = 0;
		EditorAction& byButton = scoped.Add("view.byButton"_s, "By Button"_s, EditorIcon::None, [&pressed]() { pressed++; });
		byButton.Key = { Shortcut::Mouse4, KeyMod::Shift };
		KeyboardEvent mouse4;
		mouse4.sym = Shortcut::GetMouseButtonKey(MouseButton::Fourth);
		mouse4.mod = KeyMod::LShift;
		KeyboardEvent mouse5 = mouse4;
		mouse5.sym = Shortcut::GetMouseButtonKey(MouseButton::Fifth);
		if (!scoped.HandleKey(mouse4) || scoped.HandleKey(mouse5) || pressed != 1 ||
			Shortcut::GetMouseButtonKey(MouseButton::Middle) != Keys::Unknown) {
			LOGE("Side buttons of the mouse don't run their actions");
			problems++;
		}
		return problems;
	}

	bool ActionRegistry::MatchesWhileTyping(const KeyboardEvent& event) const
	{
		for (const auto& action : _actions) {
			// Nothing is typed while the level is played
			if (action->Scope == ActionScope::Play) {
				continue;
			}
			if (action->WorksWhileTyping && (action->Key.Matches(event) || action->AlternateKey.Matches(event))) {
				return true;
			}
		}
		return false;
	}

	bool ActionRegistry::WriteKeymap(StringView path, bool all) const
	{
		Json::Value doc(Json::objectValue);
		for (const auto& action : _actions) {
			bool keyChanged = !(action->Key.Key == action->DefaultKey.Key && action->Key.Modifiers == action->DefaultKey.Modifiers);
			bool alternateChanged = !(action->AlternateKey.Key == action->DefaultAlternateKey.Key && action->AlternateKey.Modifiers == action->DefaultAlternateKey.Modifiers);
			std::string id(action->Id.data(), action->Id.size());
			if (all) {
				// Every shortcut, the file stands on its own, a key without a name keeps the default where it's read
				Json::Value both(Json::objectValue);
				if (action->Key.Key == Keys::Unknown || !action->Key.ToString().empty()) {
					both["Key"] = WriteShortcut(action->Key);
				}
				if (action->AlternateKey.Key == Keys::Unknown || !action->AlternateKey.ToString().empty()) {
					both["Alternate"] = WriteShortcut(action->AlternateKey);
				}
				doc[id] = both;
				continue;
			}
			if (!keyChanged && !alternateChanged) {
				continue;
			}
			if (!alternateChanged) {
				doc[id] = WriteShortcut(action->Key);
			} else {
				// Only what changed, the rest keeps its default, which may be a key without a name
				Json::Value both(Json::objectValue);
				if (keyChanged) {
					both["Key"] = WriteShortcut(action->Key);
				}
				both["Alternate"] = WriteShortcut(action->AlternateKey);
				doc[id] = both;
			}
		}
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "\t";
		std::string text = Json::writeString(builder, doc);

		if (!AtomicFile::Write(path, text.data(), std::int64_t(text.size()))) {
			return false;
		}
#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::SyncToPersistent();
#endif
		return true;
	}

	bool ActionRegistry::HandleKey(const KeyboardEvent& event, ActionScope scope, bool repeat, EditorAction** ran)
	{
		if (ran != nullptr) {
			*ran = nullptr;
		}
		for (auto& action : _actions) {
			if (!ScopesOverlap(action->Scope, scope)) {
				continue;
			}
			if (action->Key.Matches(event) || action->AlternateKey.Matches(event)) {
				if ((!repeat || !action->IgnoresRepeat) && action->CanExecute()) {
					action->Execute();
					if (ran != nullptr) {
						*ran = action.get();
					}
				}
				return true;
			}
		}
		return false;
	}
}

#endif
