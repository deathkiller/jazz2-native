#include "EditorSettings.h"

#if defined(WITH_EDITOR)

#include "../../PreferencesCache.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <jsoncpp/json.h>

#include <algorithm>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		void ReadPlacement(const Json::Value& value, EditorSettings::PanelPlacement& placement)
		{
			if (!value.isObject()) {
				return;
			}
			placement.Floating = value.get("Floating", false).asBool();
			placement.X = std::clamp(value.get("X", 0.0).asFloat(), -10000.0f, 10000.0f);
			placement.Y = std::clamp(value.get("Y", 0.0).asFloat(), -10000.0f, 10000.0f);
			placement.Width = std::clamp(value.get("Width", 0.0).asFloat(), 0.0f, 4000.0f);
			placement.Height = std::clamp(value.get("Height", 0.0).asFloat(), 0.0f, 4000.0f);
		}

		Json::Value WritePlacement(const EditorSettings::PanelPlacement& placement)
		{
			Json::Value value(Json::objectValue);
			value["Floating"] = placement.Floating;
			value["X"] = placement.X;
			value["Y"] = placement.Y;
			value["Width"] = placement.Width;
			value["Height"] = placement.Height;
			return value;
		}
	}

	void EditorSettings::Load()
	{
		auto s = fs::Open(GetPath(), FileAccess::Read);
		if (!s->IsValid()) {
			return;
		}
		std::int64_t size = s->GetSize();
		if (size <= 0 || size > 1024 * 1024) {
			return;
		}
		std::unique_ptr<char[]> buffer = std::make_unique<char[]>(std::size_t(size));
		s->Read(buffer.get(), size);

		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		Json::Value doc; std::string errors;
		if (!reader->parse(buffer.get(), buffer.get() + size, &doc, &errors) || !doc.isObject()) {
			LOGW("Editor settings cannot be read: {}", errors.c_str());
			return;
		}

		UiScale = std::clamp(doc.get("UiScale", 0.0).asFloat(), 0.0f, 8.0f);
		ShowGrid = doc.get("ShowGrid", true).asBool();
		PauseLayerMovement = doc.get("PauseLayerMovement", false).asBool();
		// The original tile sets have 10 tiles in a row, the key was renamed when the default became 10
		TilePickerColumns = std::clamp(doc.get("TileColumns", 10).asInt(), 1, 32);
		RightPanelWidth = std::clamp(doc.get("RightPanelWidth", 165.0).asFloat(), 80.0f, 600.0f);
		LayerListHeight = std::clamp(doc.get("LayerListHeight", 96.0).asFloat(), 32.0f, 600.0f);
		PickerPanelWidth = std::clamp(doc.get("PickerPanelWidth", 180.0).asFloat(), 80.0f, 800.0f);
		Layout = std::clamp(doc.get("Layout", 0).asInt(), 0, 2);
		Expert = doc.get("Expert", true).asBool();
		ReadPlacement(doc["PickerPanelPlacement"], PickerPanelPlacement);
		ReadPlacement(doc["RightPanelPlacement"], RightPanelPlacement);
		PlayCharacter = std::clamp(doc.get("PlayCharacter", 1).asInt(), 1, 3);
		PlayDifficulty = std::clamp(doc.get("PlayDifficulty", 2).asInt(), 1, 3);
		PlayGameMode = std::clamp(doc.get("PlayGameMode", 0).asInt(), 0, 255);
		PlayPlayerCount = std::clamp(doc.get("PlayPlayerCount", 2).asInt(), 1, 4);
		RecentFiles.clear();
		const Json::Value& recent = doc["RecentFiles"];
		if (recent.isArray()) {
			for (const Json::Value& item : recent) {
				if (item.isString() && std::int32_t(RecentFiles.size()) < MaxRecentFiles) {
					RecentFiles.push_back(String(item.asCString()));
				}
			}
		}
	}

	bool EditorSettings::Save() const
	{
		Json::Value doc(Json::objectValue);
		doc["UiScale"] = UiScale;
		doc["ShowGrid"] = ShowGrid;
		doc["PauseLayerMovement"] = PauseLayerMovement;
		doc["TileColumns"] = TilePickerColumns;
		doc["RightPanelWidth"] = RightPanelWidth;
		doc["LayerListHeight"] = LayerListHeight;
		doc["PickerPanelWidth"] = PickerPanelWidth;
		doc["Layout"] = Layout;
		doc["Expert"] = Expert;
		doc["PickerPanelPlacement"] = WritePlacement(PickerPanelPlacement);
		doc["RightPanelPlacement"] = WritePlacement(RightPanelPlacement);
		doc["PlayCharacter"] = PlayCharacter;
		doc["PlayDifficulty"] = PlayDifficulty;
		doc["PlayGameMode"] = PlayGameMode;
		doc["PlayPlayerCount"] = PlayPlayerCount;
		Json::Value& recent = doc["RecentFiles"];
		recent = Json::Value(Json::arrayValue);
		for (const String& path : RecentFiles) {
			recent.append(Json::Value(path.data(), path.data() + path.size()));
		}

		Json::StreamWriterBuilder builder;
		builder["indentation"] = "\t";
		std::string text = Json::writeString(builder, doc);

		auto s = fs::Open(GetPath(), FileAccess::Write);
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

	void EditorSettings::AddRecentFile(StringView path)
	{
		for (std::size_t i = 0; i < RecentFiles.size(); i++) {
			if (RecentFiles[i] == path) {
				RecentFiles.erase(RecentFiles.begin() + i);
				break;
			}
		}
		RecentFiles.insert(RecentFiles.begin(), String(path));
		if (std::int32_t(RecentFiles.size()) > MaxRecentFiles) {
			RecentFiles.truncate(MaxRecentFiles);
		}
	}

	String EditorSettings::GetPath()
	{
		return fs::CombinePath(PreferencesCache::GetDirectory(), "Jazz2.Editor.json"_s);
	}
}

#endif
