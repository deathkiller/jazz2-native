#include "ActionRegistry.h"

#if defined(WITH_EDITOR)

#include "../../../Main.h"
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
				case Keys::F1: return "F1"_s; case Keys::F2: return "F2"_s; case Keys::F3: return "F3"_s; case Keys::F4: return "F4"_s;
				case Keys::F5: return "F5"_s; case Keys::F6: return "F6"_s; case Keys::F7: return "F7"_s; case Keys::F8: return "F8"_s;
				case Keys::F9: return "F9"_s; case Keys::F10: return "F10"_s; case Keys::F11: return "F11"_s; case Keys::F12: return "F12"_s;
				case Keys::Delete: return "Del"_s;
				case Keys::Backspace: return "Backspace"_s;
				case Keys::Return: return "Enter"_s;
				case Keys::Escape: return "Esc"_s;
				case Keys::Space: return "Space"_s;
				case Keys::Tab: return "Tab"_s;
				case Keys::Home: return "Home"_s;
				case Keys::End: return "End"_s;
				case Keys::PageUp: return "PgUp"_s;
				case Keys::PageDown: return "PgDn"_s;
				case Keys::Plus: case Keys::NumPadPlus: case Keys::Equals: return "+"_s;
				case Keys::Minus: case Keys::NumPadMinus: return "-"_s;
				case Keys::Left: return "Left"_s;
				case Keys::Right: return "Right"_s;
				case Keys::Up: return "Up"_s;
				case Keys::Down: return "Down"_s;
				default: return {};
			}
		}

		// Keys that type the same character are the same key for a shortcut
		bool AreSameKey(Keys a, Keys b)
		{
			auto normalize = [](Keys key) {
				switch (key) {
					case Keys::Equals: case Keys::NumPadPlus: return Keys::Plus;
					case Keys::NumPadMinus: return Keys::Minus;
					case Keys::NumPadEnter: return Keys::Return;
					case Keys::NumPad0: return Keys::D0;
					default: return key;
				}
			};
			return (normalize(a) == normalize(b));
		}
	}

	bool Shortcut::Matches(const KeyboardEvent& event) const
	{
		if (Key == Keys::Unknown || !AreSameKey(Key, event.sym)) {
			return false;
		}
		std::int32_t mod = 0;
		if ((event.mod & KeyMod::Ctrl) != 0) {
			mod |= KeyMod::Ctrl;
		}
		if ((event.mod & KeyMod::Shift) != 0) {
			mod |= KeyMod::Shift;
		}
		if ((event.mod & KeyMod::Alt) != 0) {
			mod |= KeyMod::Alt;
		}
		return (mod == Modifiers);
	}

	String Shortcut::ToString() const
	{
		StringView name = GetKeyName(Key);
		if (name.empty()) {
			return {};
		}
		String result;
		if ((Modifiers & KeyMod::Ctrl) != 0) {
			result += "Ctrl+"_s;
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
		action->Execute = std::move(execute);
		return *action;
	}

	EditorAction* ActionRegistry::Find(StringView id)
	{
		for (auto& action : _actions) {
			if (action->Id == id) {
				return action.get();
			}
		}
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
			if (action.get() != except && ((AreSameKey(action->Key.Key, shortcut.Key) && action->Key.Modifiers == shortcut.Modifiers) ||
				(AreSameKey(action->AlternateKey.Key, shortcut.Key) && action->AlternateKey.Modifiers == shortcut.Modifiers))) {
				return action.get();
			}
		}
		return nullptr;
	}

	void ActionRegistry::CaptureDefaults()
	{
		for (auto& action : _actions) {
			action->DefaultKey = action->Key;
		}
	}

	void ActionRegistry::LoadOverrides(StringView path)
	{
		auto s = fs::Open(path, FileAccess::Read);
		std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
		if (size <= 0 || size > 1024 * 1024) {
			return;
		}
		std::unique_ptr<char[]> buffer = std::make_unique<char[]>(std::size_t(size));
		s->Read(buffer.get(), size);

		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		Json::Value doc; std::string errors;
		if (!reader->parse(buffer.get(), buffer.get() + size, &doc, &errors) || !doc.isObject()) {
			LOGW("Editor shortcuts cannot be read: {}", errors.c_str());
			return;
		}
		for (auto& action : _actions) {
			const Json::Value& value = doc[std::string(action->Id.data(), action->Id.size())];
			if (value.isNull() && !doc.isMember(std::string(action->Id.data(), action->Id.size()))) {
				continue;
			}
			if (value.isArray() && value.size() == 2 && value[0].isInt() && value[1].isInt()) {
				action->Key.Key = Keys(value[0].asInt());
				action->Key.Modifiers = value[1].asInt() & (KeyMod::Ctrl | KeyMod::Shift | KeyMod::Alt);
			} else {
				// No shortcut at all
				action->Key = Shortcut();
			}
		}
	}

	bool ActionRegistry::SaveOverrides(StringView path) const
	{
		Json::Value doc(Json::objectValue);
		for (const auto& action : _actions) {
			if (action->Key.Key == action->DefaultKey.Key && action->Key.Modifiers == action->DefaultKey.Modifiers) {
				continue;
			}
			std::string id(action->Id.data(), action->Id.size());
			if (action->Key.Key == Keys::Unknown) {
				doc[id] = Json::Value(Json::nullValue);
			} else {
				Json::Value pair(Json::arrayValue);
				pair.append(std::int32_t(action->Key.Key));
				pair.append(action->Key.Modifiers);
				doc[id] = pair;
			}
		}
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "\t";
		std::string text = Json::writeString(builder, doc);

		auto s = fs::Open(path, FileAccess::Write);
		if (!s->IsValid()) {
			return false;
		}
		s->Write(text.data(), std::int64_t(text.size()));
#if defined(DEATH_TARGET_EMSCRIPTEN)
		s = nullptr;
		fs::SyncToPersistent();
#endif
		return true;
	}

	bool ActionRegistry::HandleKey(const KeyboardEvent& event)
	{
		for (auto& action : _actions) {
			if (action->Key.Matches(event) || action->AlternateKey.Matches(event)) {
				if (action->CanExecute()) {
					action->Execute();
				}
				return true;
			}
		}
		return false;
	}
}

#endif
