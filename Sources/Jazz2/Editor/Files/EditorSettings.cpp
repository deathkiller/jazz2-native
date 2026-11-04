#include "EditorSettings.h"

#if defined(WITH_EDITOR)

#include "../../Utils/JsonAccess.h"
#include "../../Utils/JsonFile.h"
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
		// The file can be edited by hand, so no value is trusted to have the expected type
		void ReadPlacement(const Json::Value& value, EditorSettings::PanelPlacement& placement)
		{
			if (!value.isObject()) {
				return;
			}
			placement.Floating = Utils::JsonAccess::GetBool(value, "Floating", false);
			placement.X = std::clamp<float>(Utils::JsonAccess::GetFloat(value, "X", 0.0f), -10000.0f, 10000.0f);
			placement.Y = std::clamp<float>(Utils::JsonAccess::GetFloat(value, "Y", 0.0f), -10000.0f, 10000.0f);
			placement.Width = std::clamp<float>(Utils::JsonAccess::GetFloat(value, "Width", 0.0f), 0.0f, 4000.0f);
			placement.Height = std::clamp<float>(Utils::JsonAccess::GetFloat(value, "Height", 0.0f), 0.0f, 4000.0f);
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

		// A list of small numbers, those out of range or twice are left out
		void ReadNumbers(const Json::Value& value, SmallVectorImpl<std::uint16_t>& numbers, std::int32_t maxCount)
		{
			numbers.clear();
			if (!value.isArray()) {
				return;
			}
			for (const Json::Value& item : value) {
				std::int32_t number = Utils::JsonAccess::ToInt(item, -1);
				if (number >= 0 && number <= UINT16_MAX && std::int32_t(numbers.size()) < maxCount &&
					std::find(numbers.begin(), numbers.end(), std::uint16_t(number)) == numbers.end()) {
					numbers.push_back(std::uint16_t(number));
				}
			}
		}

		Json::Value WriteNumbers(const SmallVectorImpl<std::uint16_t>& numbers)
		{
			Json::Value value(Json::arrayValue);
			for (std::uint16_t number : numbers) {
				value.append(Json::Int(number));
			}
			return value;
		}

		// Moves a number to the front, the oldest ones are dropped
		void PushRecent(SmallVectorImpl<std::uint16_t>& numbers, std::uint16_t number, std::int32_t maxCount)
		{
			auto it = std::find(numbers.begin(), numbers.end(), number);
			if (it != numbers.end()) {
				numbers.erase(it);
			}
			numbers.insert(numbers.begin(), number);
			if (std::int32_t(numbers.size()) > maxCount) {
				numbers.truncate(std::size_t(maxCount));
			}
		}
	}

	void EditorSettings::Load()
	{
		Json::Value doc;
		if (Utils::JsonFile::Read(GetPath(), doc, 1024 * 1024, "Editor settings"_s) != Utils::JsonFile::ReadResult::Ok) {
			return;
		}

		UiScale = std::clamp<float>(Utils::JsonAccess::GetFloat(doc, "UiScale", 0.0f), 0.0f, 8.0f);
		ShowGrid = Utils::JsonAccess::GetBool(doc, "ShowGrid", true);
		PauseLayerMovement = Utils::JsonAccess::GetBool(doc, "PauseLayerMovement", false);
		EventsOnTop = Utils::JsonAccess::GetBool(doc, "EventsOnTop", false);
		PreviewTexturedLayers = Utils::JsonAccess::GetBool(doc, "PreviewTexturedLayers", false);
		OverriddenAssets = Utils::JsonAccess::GetBool(doc, "OverriddenAssets", true);
		Tool = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "Tool", 5), 0, 255);
		PickerEvents = Utils::JsonAccess::GetBool(doc, "PickerEvents", false);
		// The original tile sets have 10 tiles in a row, the key was renamed when the default became 10
		TilePickerColumns = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "TileColumns", 10), 1, 32);
		RightPanelWidth = std::clamp<float>(Utils::JsonAccess::GetFloat(doc, "RightPanelWidth", 165.0f), 80.0f, 600.0f);
		LayerListHeight = std::clamp<float>(Utils::JsonAccess::GetFloat(doc, "LayerListHeight", 96.0f), 32.0f, 600.0f);
		PickerPanelWidth = std::clamp<float>(Utils::JsonAccess::GetFloat(doc, "PickerPanelWidth", 180.0f), 80.0f, 800.0f);
		Layout = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "Layout", 0), 0, 2);
		Expert = Utils::JsonAccess::GetBool(doc, "Expert", true);
		ReadPlacement(Utils::JsonAccess::Member(doc, "PickerPanelPlacement"), PickerPanelPlacement);
		ReadPlacement(Utils::JsonAccess::Member(doc, "RightPanelPlacement"), RightPanelPlacement);
		PlayCharacter = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "PlayCharacter", 1), 1, 3);
		PlayDifficulty = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "PlayDifficulty", 2), 1, 3);
		PlayGameMode = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "PlayGameMode", 0), 0, 255);
		PlayPlayerCount = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "PlayPlayerCount", 2), 1, 4);
		Theme = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "Theme", 0), 0, 1);
		ReducedMotion = Utils::JsonAccess::GetBool(doc, "ReducedMotion", false);
		ReadableFont = Utils::JsonAccess::GetBool(doc, "ReadableFont", true);
		ScriptEditor = Utils::JsonAccess::ToString(Utils::JsonAccess::Member(doc, "ScriptEditor"));
		{
			const Json::Value& tools = Utils::JsonAccess::Member(doc, "Tools");
			Tools.AcrossSeams = Utils::JsonAccess::GetBool(tools, "AcrossSeams", true);
			Tools.IgnoreFlips = Utils::JsonAccess::GetBool(tools, "IgnoreFlips", false);
			Tools.OnlyEmpty = Utils::JsonAccess::GetBool(tools, "OnlyEmpty", false);
			Tools.WandGlobal = Utils::JsonAccess::GetBool(tools, "WandGlobal", false);
			Tools.Shape = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(tools, "Shape", 0), 0, 3);
			Tools.EventMode = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(tools, "EventMode", 0), 0, 3);
			Tools.MeasureArcs = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(tools, "MeasureArcs", 0x07), 0, 0xFF);
			Tools.MeasureCollisions = Utils::JsonAccess::GetBool(tools, "MeasureCollisions", true);
			Tools.MirrorSwapTeams = Utils::JsonAccess::GetBool(tools, "MirrorSwapTeams", false);
		}
		{
			const Json::Value& playTest = Utils::JsonAccess::Member(doc, "PlayTest");
			PlayTest.TimeScale = std::clamp<float>(Utils::JsonAccess::GetFloat(playTest, "TimeScale", 1.0f), 0.05f, 4.0f);
			PlayTest.ShowHitboxes = Utils::JsonAccess::GetBool(playTest, "ShowHitboxes", false);
			PlayTest.ShowEventZones = Utils::JsonAccess::GetBool(playTest, "ShowEventZones", false);
			PlayTest.ShowDeaths = Utils::JsonAccess::GetBool(playTest, "ShowDeaths", false);
			PlayTest.ReachCharacter = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(playTest, "ReachCharacter", 0), 0, 3);
			PlayTest.ReachFromMpStarts = Utils::JsonAccess::GetBool(playTest, "ReachFromMpStarts", false);
		}
		ShowScreenFrame = Utils::JsonAccess::GetBool(doc, "ShowScreenFrame", false);
		ScreenFrameKind = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "ScreenFrameKind", 0), 0, 3);
		CheckLevelFilter = std::clamp<std::int32_t>(Utils::JsonAccess::GetInt(doc, "CheckLevelFilter", 7), 0, 7);
		ReadNumbers(Utils::JsonAccess::Member(doc, "RecentEvents"), RecentEvents, MaxRecentItems);
		ReadNumbers(Utils::JsonAccess::Member(doc, "FavoriteEvents"), FavoriteEvents, MaxFavoriteItems);
		TileFavorites.clear();
		const Json::Value& tileFavorites = Utils::JsonAccess::Member(doc, "TileFavorites");
		if (tileFavorites.isObject()) {
			for (auto it = tileFavorites.begin(); it != tileFavorites.end(); ++it) {
				if (!it->isObject()) {
					continue;
				}
				auto name = it.name();
				TileSetFavorites& favorites = TileFavorites.emplace_back();
				favorites.TileSet = String(name.data(), name.size());
				ReadNumbers(Utils::JsonAccess::Member(*it, "Recent"), favorites.Recent, MaxRecentItems);
				ReadNumbers(Utils::JsonAccess::Member(*it, "Favorites"), favorites.Favorites, MaxFavoriteItems);
			}
		}
		RecentFiles.clear();
		const Json::Value& recent = Utils::JsonAccess::Member(doc, "RecentFiles");
		if (recent.isArray()) {
			for (const Json::Value& item : recent) {
				String path = Utils::JsonAccess::ToString(item);
				if (!path.empty() && std::int32_t(RecentFiles.size()) < MaxRecentFiles) {
					RecentFiles.push_back(Death::move(path));
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
		doc["EventsOnTop"] = EventsOnTop;
		doc["PreviewTexturedLayers"] = PreviewTexturedLayers;
		doc["OverriddenAssets"] = OverriddenAssets;
		doc["Tool"] = Json::Int(Tool);
		doc["PickerEvents"] = PickerEvents;
		doc["TileColumns"] = Json::Int(TilePickerColumns);
		doc["RightPanelWidth"] = RightPanelWidth;
		doc["LayerListHeight"] = LayerListHeight;
		doc["PickerPanelWidth"] = PickerPanelWidth;
		doc["Layout"] = Json::Int(Layout);
		doc["Expert"] = Expert;
		doc["PickerPanelPlacement"] = WritePlacement(PickerPanelPlacement);
		doc["RightPanelPlacement"] = WritePlacement(RightPanelPlacement);
		doc["PlayCharacter"] = Json::Int(PlayCharacter);
		doc["PlayDifficulty"] = Json::Int(PlayDifficulty);
		doc["PlayGameMode"] = Json::Int(PlayGameMode);
		doc["PlayPlayerCount"] = Json::Int(PlayPlayerCount);
		doc["Theme"] = Json::Int(Theme);
		doc["ReducedMotion"] = ReducedMotion;
		doc["ReadableFont"] = ReadableFont;
		// Written even when empty, so it can be found in the file, it's set only there
		doc["ScriptEditor"] = Json::Value(ScriptEditor.data(), ScriptEditor.data() + ScriptEditor.size());
		{
			Json::Value tools(Json::objectValue);
			tools["AcrossSeams"] = Tools.AcrossSeams;
			tools["IgnoreFlips"] = Tools.IgnoreFlips;
			tools["OnlyEmpty"] = Tools.OnlyEmpty;
			tools["WandGlobal"] = Tools.WandGlobal;
			tools["Shape"] = Json::Int(Tools.Shape);
			tools["EventMode"] = Json::Int(Tools.EventMode);
			tools["MeasureArcs"] = Json::Int(Tools.MeasureArcs);
			tools["MeasureCollisions"] = Tools.MeasureCollisions;
			tools["MirrorSwapTeams"] = Tools.MirrorSwapTeams;
			doc["Tools"] = tools;
		}
		{
			Json::Value playTest(Json::objectValue);
			playTest["TimeScale"] = PlayTest.TimeScale;
			playTest["ShowHitboxes"] = PlayTest.ShowHitboxes;
			playTest["ShowEventZones"] = PlayTest.ShowEventZones;
			playTest["ShowDeaths"] = PlayTest.ShowDeaths;
			playTest["ReachCharacter"] = Json::Int(PlayTest.ReachCharacter);
			playTest["ReachFromMpStarts"] = PlayTest.ReachFromMpStarts;
			doc["PlayTest"] = playTest;
		}
		doc["ShowScreenFrame"] = ShowScreenFrame;
		doc["ScreenFrameKind"] = Json::Int(ScreenFrameKind);
		doc["CheckLevelFilter"] = Json::Int(CheckLevelFilter);
		doc["RecentEvents"] = WriteNumbers(RecentEvents);
		doc["FavoriteEvents"] = WriteNumbers(FavoriteEvents);
		Json::Value& tileFavorites = doc["TileFavorites"];
		tileFavorites = Json::Value(Json::objectValue);
		for (const TileSetFavorites& favorites : TileFavorites) {
			if (favorites.TileSet.empty() || (favorites.Recent.empty() && favorites.Favorites.empty())) {
				continue;
			}
			Json::Value& item = tileFavorites[std::string(favorites.TileSet.data(), favorites.TileSet.size())];
			item = Json::Value(Json::objectValue);
			item["Recent"] = WriteNumbers(favorites.Recent);
			item["Favorites"] = WriteNumbers(favorites.Favorites);
		}
		Json::Value& recent = doc["RecentFiles"];
		recent = Json::Value(Json::arrayValue);
		for (const String& path : RecentFiles) {
			recent.append(Json::Value(path.data(), path.data() + path.size()));
		}

		// Through a temporary file, a crash or a full disk while it's written would lose all settings otherwise
		if (!Utils::JsonFile::Write(GetPath(), doc)) {
			return false;
		}
#if defined(DEATH_TARGET_EMSCRIPTEN)
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

	void EditorSettings::RemoveRecentFile(StringView path)
	{
		for (std::size_t i = 0; i < RecentFiles.size(); i++) {
			if (RecentFiles[i] == path) {
				RecentFiles.erase(RecentFiles.begin() + i);
				break;
			}
		}
	}

	void EditorSettings::AddRecentEvent(std::uint16_t type)
	{
		PushRecent(RecentEvents, type, MaxRecentItems);
	}

	EditorSettings::TileSetFavorites& EditorSettings::GetTileFavorites(StringView tileSet)
	{
		for (TileSetFavorites& favorites : TileFavorites) {
			if (favorites.TileSet == tileSet) {
				return favorites;
			}
		}
		TileSetFavorites& favorites = TileFavorites.emplace_back();
		favorites.TileSet = tileSet;
		return favorites;
	}

	void EditorSettings::AddRecentTile(StringView tileSet, std::uint16_t index)
	{
		if (!tileSet.empty()) {
			PushRecent(GetTileFavorites(tileSet).Recent, index, MaxRecentItems);
		}
	}

	String EditorSettings::GetPath()
	{
		return fs::CombinePath(PreferencesCache::GetDirectory(), "Jazz2.Editor.json"_s);
	}
}

#endif
