#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../../nCine/I18n.h"

#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>

#include <jsoncpp/json.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		// An event described from outside of the game, it owns all the texts and parameters its description points to
		struct ExtensionEvent
		{
			EventDescriptor Descriptor;
			EventExtensionScope Scope;
			SmallVector<String, 0> Texts;
			SmallVector<EventParamField, 0> Fields;
			SmallVector<SmallVector<ParamOption, 0>, 0> Options;
		};

		struct CatalogIndex
		{
			SmallVector<const EventDescriptor*, 0> BuiltIn;
			SmallVector<std::unique_ptr<ExtensionEvent>, 0> Extensions;
			SmallVector<const EventDescriptor*, 0> All;
			const EventDescriptor* ByType[std::size_t(EventType::Count)];
			// Extensions of types the game doesn't know
			std::unordered_map<std::uint16_t, const EventDescriptor*> Others;
			std::uint32_t Revision;

			CatalogIndex()
				: Revision(0)
			{
				for (auto part : { EventCatalogParts::GetBasicEvents(), EventCatalogParts::GetAreaEvents(), EventCatalogParts::GetEnvironmentEvents(),
						EventCatalogParts::GetEnemyEvents(), EventCatalogParts::GetItemEvents() }) {
					for (const EventDescriptor& descriptor : part) {
						BuiltIn.push_back(&descriptor);
					}
				}
				Rebuild();
			}

			void Rebuild()
			{
				Revision++;
				All.clear();
				Others.clear();
				std::fill(std::begin(ByType), std::end(ByType), nullptr);
				for (const EventDescriptor* descriptor : BuiltIn) {
					std::size_t type = std::size_t(descriptor->Type);
					if (type < std::size_t(EventType::Count)) {
						DEATH_DEBUG_ASSERT(ByType[type] == nullptr, ("Event {} is described twice", type), );
						ByType[type] = descriptor;
					}
				}
				// Those of event packs replace those of the game, those of the level replace both
				for (EventExtensionScope scope : { EventExtensionScope::Global, EventExtensionScope::Level }) {
					for (const auto& extension : Extensions) {
						if (extension->Scope != scope) {
							continue;
						}
						std::size_t type = std::size_t(extension->Descriptor.Type);
						if (type < std::size_t(EventType::Count)) {
							ByType[type] = &extension->Descriptor;
						} else {
							Others[std::uint16_t(type)] = &extension->Descriptor;
						}
					}
				}
				for (const EventDescriptor* descriptor : BuiltIn) {
					if (ByType[std::size_t(descriptor->Type)] == descriptor) {
						All.push_back(descriptor);
					}
				}
				for (const auto& extension : Extensions) {
					std::size_t type = std::size_t(extension->Descriptor.Type);
					const EventDescriptor* shown = (type < std::size_t(EventType::Count) ? ByType[type] : Others[std::uint16_t(type)]);
					if (shown == &extension->Descriptor) {
						All.push_back(shown);
					}
				}
			}
		};

		CatalogIndex& GetIndex()
		{
			static CatalogIndex index;
			return index;
		}

		bool ParseCategory(const std::string& name, EventCategory& category)
		{
			static const char* const Names[] = { "basic", "scenery", "modifier", "area", "trigger", "warp", "light",
				"environment", "enemy", "boss", "collectible", "container", "other" };
			static_assert(std::size(Names) == std::size_t(EventCategory::Count), "Names of categories don't match");
			for (std::size_t i = 0; i < std::size(Names); i++) {
				if (name == Names[i]) {
					category = EventCategory(i);
					return true;
				}
			}
			return false;
		}

		bool ParseParamType(const std::string& name, ParamType& type)
		{
			static const std::pair<const char*, ParamType> Types[] = {
				{ "uint", ParamType::UInt }, { "int", ParamType::Int }, { "bool", ParamType::Bool }, { "enum", ParamType::Enum },
				{ "flags", ParamType::Flags }, { "event", ParamType::EventType }, { "text", ParamType::TextId },
				{ "trigger", ParamType::TriggerId }, { "warp", ParamType::WarpId }, { "string", ParamType::String }
			};
			for (const auto& entry : Types) {
				if (name == entry.first) {
					type = entry.second;
					return true;
				}
			}
			return false;
		}

		ParamUnit ParseUnit(const std::string& name)
		{
			static const std::pair<const char*, ParamUnit> Units[] = {
				{ "pixels", ParamUnit::Pixels }, { "tiles", ParamUnit::Tiles }, { "frames", ParamUnit::Frames },
				{ "seconds", ParamUnit::Seconds }, { "percent", ParamUnit::Percent }, { "degrees", ParamUnit::Degrees }
			};
			for (const auto& entry : Units) {
				if (name == entry.first) {
					return entry.second;
				}
			}
			return ParamUnit::None;
		}

		// Reads one event, returns nullptr if it can't be used, why is logged
		std::unique_ptr<ExtensionEvent> ParseExtension(const Json::Value& value, StringView source, EventExtensionScope scope)
		{
			if (!value.isObject()) {
				LOGW("Event description in \"{}\" isn't an object", source);
				return nullptr;
			}
			const Json::Value& typeValue = value["type"];
			if (!typeValue.isInt() || typeValue.asInt() <= 0 || typeValue.asInt() > 0xFFFF) {
				LOGW("Event description in \"{}\" has no valid \"type\"", source);
				return nullptr;
			}
			std::string name = value.get("name", "").asString();
			if (name.empty()) {
				LOGW("Event {} in \"{}\" has no \"name\"", typeValue.asInt(), source);
				return nullptr;
			}

			auto extension = std::make_unique<ExtensionEvent>();
			extension->Scope = scope;
			EventDescriptor& descriptor = extension->Descriptor;
			descriptor = {};
			descriptor.Type = EventType(typeValue.asInt());
			descriptor.Category = EventCategory::Other;
			descriptor.Traits = EventTraits::Object;
			std::string category = value.get("category", "other").asString();
			if (!ParseCategory(category, descriptor.Category)) {
				LOGW("Event \"{}\" in \"{}\" has unknown category \"{}\"", name.c_str(), source, category.c_str());
			}
			if (value["traits"].isArray()) {
				descriptor.Traits = EventTraits::None;
				for (const Json::Value& trait : value["traits"]) {
					std::string traitName = trait.asString();
					if (traitName == "object") {
						descriptor.Traits |= EventTraits::Object;
					} else if (traitName == "tileModifier") {
						descriptor.Traits |= EventTraits::TileModifier;
					} else if (traitName == "area") {
						descriptor.Traits |= EventTraits::Area;
					} else if (traitName == "multiplayerOnly") {
						descriptor.Traits |= EventTraits::MultiplayerOnly;
					}
				}
			}

			// All texts first, the description points into them once they don't move anymore
			auto& texts = extension->Texts;
			std::string key = value.get("key", "").asString();
			if (key.empty()) {
				key = "Ext" + std::to_string(typeValue.asInt());
			}
			texts.emplace_back(key.data(), key.size());												// 0: key
			texts.emplace_back(name.data(), name.size());											// 1: name
			std::string description = value.get("description", "").asString();
			texts.emplace_back(description.data(), description.size());								// 2: description
			std::string metadata = (value["preview"].isObject() ? value["preview"].get("metadata", "").asString() : std::string());
			texts.emplace_back(metadata.data(), metadata.size());									// 3: preview
			descriptor.PreviewAnimation = (value["preview"].isObject() ? value["preview"].get("animation", 0).asUInt() : 0);

			struct PendingField
			{
				EventParamField Field;
				std::size_t NameText;
				std::size_t DescriptionText;
				std::size_t OptionsIndex;
				SmallVector<std::size_t, 0> OptionTexts;
			};
			SmallVector<PendingField, 0> pending;
			const Json::Value& params = value["params"];
			if (params.isArray()) {
				for (const Json::Value& param : params) {
					std::string paramName = param.get("name", "").asString();
					ParamType paramType = ParamType::UInt;
					if (paramName.empty() || !ParseParamType(param.get("type", "uint").asString(), paramType)) {
						LOGW("Parameter of event \"{}\" in \"{}\" has no name or an unknown type, it's left out", name.c_str(), source);
						continue;
					}
					PendingField& field = pending.emplace_back();
					field.Field = {};
					field.Field.Type = paramType;
					std::int32_t offset = param.get("offset", -1).asInt();
					std::int32_t bits = param.get("bits", paramType == ParamType::Bool ? 1 : (paramType == ParamType::EventType ? 16 : 8)).asInt();
					std::int32_t shift = param.get("shift", 0).asInt();
					if (paramType == ParamType::String) {
						bits = param.get("length", bits / 8).asInt() * 8;
						shift = 0;
					}
					std::int32_t storage = (paramType == ParamType::String ? bits / 8 : ((shift + bits) <= 8 ? 1 : ((shift + bits) <= 16 ? 2 : 4)));
					if (offset < 0 || bits <= 0 || shift < 0 || (paramType != ParamType::String && shift + bits > 32) ||
						offset + storage > Formats::LevelFile::EventParamsSize) {
						LOGW("Parameter \"{}\" of event \"{}\" in \"{}\" doesn't fit into the parameters, it's left out", paramName.c_str(), name.c_str(), source);
						pending.pop_back();
						continue;
					}
					bool isSigned = (paramType == ParamType::Int);
					field.Field.Offset = std::uint8_t(offset);
					field.Field.Bits = std::uint8_t(bits);
					field.Field.Shift = std::uint8_t(shift);
					field.Field.Signed = isSigned;
					std::int64_t range = (bits >= 31 ? INT32_MAX : (std::int64_t(1) << bits) - 1);
					field.Field.Min = param.get("min", isSigned ? std::int32_t(-(range + 1) / 2) : 0).asInt();
					field.Field.Max = param.get("max", isSigned ? std::int32_t(range / 2) : std::int32_t(range)).asInt();
					field.Field.Default = param.get("default", 0).asInt();
					field.Field.Unit = ParseUnit(param.get("unit", "").asString());
					field.NameText = texts.size();
					texts.emplace_back(paramName.data(), paramName.size());
					std::string paramDescription = param.get("description", "").asString();
					field.DescriptionText = (paramDescription.empty() ? SIZE_MAX : texts.size());
					if (!paramDescription.empty()) {
						texts.emplace_back(paramDescription.data(), paramDescription.size());
					}
					field.OptionsIndex = SIZE_MAX;
					const Json::Value& options = param["options"];
					if (options.isArray() && (paramType == ParamType::Enum || paramType == ParamType::Flags)) {
						field.OptionsIndex = extension->Options.size();
						auto& list = extension->Options.emplace_back();
						std::int32_t nextValue = (paramType == ParamType::Flags ? 1 : 0);
						for (const Json::Value& option : options) {
							std::string label;
							std::int32_t optionValue = nextValue;
							if (option.isObject()) {
								label = option.get("label", "").asString();
								optionValue = option.get("value", nextValue).asInt();
							} else {
								label = option.asString();
							}
							nextValue = (paramType == ParamType::Flags ? optionValue << 1 : optionValue + 1);
							field.OptionTexts.push_back(texts.size());
							texts.emplace_back(label.data(), label.size());
							list.push_back(ParamOption{ nullptr, optionValue });
						}
					}
				}
			}

			// The texts don't move anymore
			descriptor.Key = texts[0].data();
			descriptor.Name = texts[1].data();
			descriptor.Description = (texts[2].empty() ? nullptr : texts[2].data());
			descriptor.PreviewMetadata = (texts[3].empty() ? nullptr : texts[3].data());
			for (PendingField& field : pending) {
				EventParamField& result = extension->Fields.emplace_back(field.Field);
				result.Name = texts[field.NameText].data();
				result.Description = (field.DescriptionText != SIZE_MAX ? texts[field.DescriptionText].data() : nullptr);
				if (field.OptionsIndex != SIZE_MAX) {
					auto& list = extension->Options[field.OptionsIndex];
					for (std::size_t i = 0; i < list.size(); i++) {
						list[i].Label = texts[field.OptionTexts[i]].data();
					}
					result.Options = list.data();
					result.OptionCount = std::uint8_t(std::min<std::size_t>(list.size(), UINT8_MAX));
				}
			}
			descriptor.Fields = extension->Fields.data();
			descriptor.FieldCount = std::uint8_t(std::min<std::size_t>(extension->Fields.size(), UINT8_MAX));
			return extension;
		}

		std::uint32_t ReadBytes(const std::uint8_t* params, std::uint8_t offset, std::uint8_t byteCount)
		{
			std::uint32_t value = 0;
			for (std::uint8_t i = 0; i < byteCount && offset + i < Formats::LevelFile::EventParamsSize; i++) {
				value |= std::uint32_t(params[offset + i]) << (i * 8);
			}
			return value;
		}

		void WriteBytes(std::uint8_t* params, std::uint8_t offset, std::uint8_t byteCount, std::uint32_t value)
		{
			for (std::uint8_t i = 0; i < byteCount && offset + i < Formats::LevelFile::EventParamsSize; i++) {
				params[offset + i] = std::uint8_t(value >> (i * 8));
			}
		}

		std::uint8_t GetStorageBytes(const EventParamField& field)
		{
			std::uint32_t lastBit = std::uint32_t(field.Shift) + field.Bits;
			return (lastBit <= 8 ? 1 : (lastBit <= 16 ? 2 : 4));
		}
	}

	const EventDescriptor* EventCatalog::Find(EventType type)
	{
		std::size_t index = std::size_t(type);
		const CatalogIndex& catalog = GetIndex();
		if (index < std::size_t(EventType::Count)) {
			return catalog.ByType[index];
		}
		if (catalog.Others.empty()) {
			return nullptr;
		}
		auto it = catalog.Others.find(std::uint16_t(index));
		return (it != catalog.Others.end() ? it->second : nullptr);
	}

	std::int32_t EventCatalog::LoadExtensions(StringView json, StringView source, EventExtensionScope scope)
	{
		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		Json::Value doc; std::string errors;
		if (!reader->parse(json.data(), json.data() + json.size(), &doc, &errors)) {
			LOGW("Event descriptions in \"{}\" cannot be read: {}", source, errors.c_str());
			return 0;
		}
		const Json::Value* list = &doc;
		if (doc.isObject() && doc["events"].isArray()) {
			list = &doc["events"];
		}

		CatalogIndex& catalog = GetIndex();
		std::int32_t added = 0;
		auto add = [&](const Json::Value& value) {
			if (auto extension = ParseExtension(value, source, scope)) {
				// A later description of the same event in the same scope replaces the earlier one
				for (std::size_t i = 0; i < catalog.Extensions.size(); i++) {
					if (catalog.Extensions[i]->Scope == scope && catalog.Extensions[i]->Descriptor.Type == extension->Descriptor.Type) {
						catalog.Extensions.erase(catalog.Extensions.begin() + i);
						break;
					}
				}
				catalog.Extensions.push_back(std::move(extension));
				added++;
			}
		};
		if (list->isArray()) {
			for (const Json::Value& value : *list) {
				add(value);
			}
		} else {
			add(*list);
		}
		if (added > 0) {
			catalog.Rebuild();
		}
		return added;
	}

	void EventCatalog::ClearExtensions(EventExtensionScope scope)
	{
		CatalogIndex& catalog = GetIndex();
		std::size_t count = catalog.Extensions.size();
		catalog.Extensions.erase(std::remove_if(catalog.Extensions.begin(), catalog.Extensions.end(),
			[scope](const std::unique_ptr<ExtensionEvent>& extension) { return extension->Scope == scope; }), catalog.Extensions.end());
		if (catalog.Extensions.size() != count) {
			catalog.Rebuild();
		}
	}

	std::uint32_t EventCatalog::GetRevision()
	{
		return GetIndex().Revision;
	}

	bool EventCatalog::IsExtension(const EventDescriptor* descriptor)
	{
		for (const auto& extension : GetIndex().Extensions) {
			if (&extension->Descriptor == descriptor) {
				return true;
			}
		}
		return false;
	}

	const EventDescriptor* EventCatalog::FindByKey(StringView key)
	{
		for (const EventDescriptor* descriptor : GetIndex().All) {
			if (key == descriptor->Key) {
				return descriptor;
			}
		}
		return nullptr;
	}

	String EventCatalog::MakeTextPreview(StringView text, std::size_t maxLength)
	{
		String result;
		for (char c : text) {
			if (c == '|') {
				result += " / "_s;
			} else if (c == '\n') {
				result += " "_s;
			} else if (std::uint8_t(c) >= 32 || c == '\f') {
				result += StringView(&c, 1);
			}
		}
		if (result.size() > maxLength) {
			// Not in the middle of a character of UTF-8
			std::size_t length = maxLength;
			while (length > 0 && (std::uint8_t(result[length]) & 0xC0) == 0x80) {
				length--;
			}
			result = result.prefix(length) + "..."_s;
		}
		return result;
	}

	ArrayView<const EventDescriptor* const> EventCatalog::GetAll()
	{
		const auto& all = GetIndex().All;
		return ArrayView<const EventDescriptor* const>(all.data(), all.size());
	}

	StringView EventCatalog::GetCategoryName(EventCategory category)
	{
		switch (category) {
			case EventCategory::Basic: return _x("Event category", "Basic");
			case EventCategory::Scenery: return _x("Event category", "Scenery");
			case EventCategory::Modifier: return _x("Event category", "Tile modifiers");
			case EventCategory::Area: return _x("Event category", "Areas");
			case EventCategory::Trigger: return _x("Event category", "Triggers");
			case EventCategory::Warp: return _x("Event category", "Warps");
			case EventCategory::Light: return _x("Event category", "Lights");
			case EventCategory::Environment: return _x("Event category", "Environment");
			case EventCategory::Enemy: return _x("Event category", "Enemies");
			case EventCategory::Boss: return _x("Event category", "Bosses");
			case EventCategory::Collectible: return _x("Event category", "Collectibles");
			case EventCategory::Container: return _x("Event category", "Containers");
			default: return _x("Event category", "Other");
		}
	}

	std::int32_t EventCatalog::ReadField(const Formats::EventRecord& event, const EventParamField& field)
	{
		if (field.Type == ParamType::String || field.Type == ParamType::Raw) {
			return 0;
		}
		std::uint32_t raw = ReadBytes(event.Params, field.Offset, GetStorageBytes(field));
		std::uint32_t mask = (field.Bits >= 32 ? 0xFFFFFFFFu : ((1u << field.Bits) - 1u));
		std::uint32_t value = (raw >> field.Shift) & mask;
		if (field.Signed && field.Bits < 32 && (value & (1u << (field.Bits - 1))) != 0) {
			// Sign-extended
			value |= ~mask;
		}
		return std::int32_t(value);
	}

	void EventCatalog::WriteField(Formats::EventRecord& event, const EventParamField& field, std::int32_t value)
	{
		if (field.Type == ParamType::String || field.Type == ParamType::Raw) {
			return;
		}
		if (field.Type != ParamType::Flags && field.Max > field.Min) {
			value = std::clamp(value, field.Min, field.Max);
		}
		std::uint8_t byteCount = GetStorageBytes(field);
		std::uint32_t raw = ReadBytes(event.Params, field.Offset, byteCount);
		std::uint32_t mask = (field.Bits >= 32 ? 0xFFFFFFFFu : ((1u << field.Bits) - 1u));
		raw &= ~(mask << field.Shift);
		raw |= (std::uint32_t(value) & mask) << field.Shift;
		WriteBytes(event.Params, field.Offset, byteCount, raw);
	}

	Formats::EventRecord EventCatalog::MakeDefault(EventType type)
	{
		Formats::EventRecord event;
		std::memset(&event, 0, sizeof(event));
		event.Type = std::uint16_t(type);
		event.Flags = Formats::EventRecord::DifficultyMask;
		if (const EventDescriptor* descriptor = Find(type)) {
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if (field.Default != 0) {
					WriteField(event, field, field.Default);
				}
			}
		}
		return event;
	}

	StringView EventCatalog::ReadString(const Formats::EventRecord& event, const EventParamField& field, char* buffer, std::size_t bufferSize)
	{
		std::size_t length = std::min<std::size_t>(field.Bits / 8, bufferSize);
		length = std::min<std::size_t>(length, std::size_t(Formats::LevelFile::EventParamsSize - field.Offset));
		std::size_t i = 0;
		for (; i < length && event.Params[field.Offset + i] != 0; i++) {
			buffer[i] = char(event.Params[field.Offset + i]);
		}
		return StringView(buffer, i);
	}
}

#endif
