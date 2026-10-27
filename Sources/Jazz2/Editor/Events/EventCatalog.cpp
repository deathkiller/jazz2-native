#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../Files/JsonAccess.h"
#include "../Files/JsonFile.h"
#include "../../Compatibility/EventConverter.h"
#include "../../Compatibility/JJ2Level.h"
// Before I18n.h, its _f() formats through Death::format()
#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/I18n.h"

#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
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
			// Pairs of values swapped by mirroring, FieldMirror::Pairs points into them
			SmallVector<SmallVector<std::int32_t, 0>, 0> MirrorPairs;
			SmallVector<EventPreviewCase, 0> PreviewCases;
			// One of the original game the level describes as it's offered in the list, see EventCatalog::GetPickedOriginal()
			EventDescriptor PickDescriptor;
			// One of the original game the game turns into its own event (see PrepareNative()), how it's made and described then
			EventType ConvertedType = EventType::Empty;
			std::uint8_t ConvertedParams[Formats::EventRecord::OriginalParamsOffset] = {};
			EventDescriptor NativeDescriptor;
			SmallVector<EventParamField, 0> NativeFields;

			bool IsNative() const {
				return (ConvertedType != EventType::Empty && ConvertedType != EventType::PassthroughEvent);
			}
		};

		// An event the script of a level hides from the list, see EventCatalog::LoadMlleEvents()
		struct HiddenEvent
		{
			EventExtensionScope Scope;
			EventType Type;
		};

		// The event an event of the original game becomes in the game, as the converter makes it of a level with nothing
		// particular about it
		Compatibility::ConversionResult ConvertOriginal(std::int32_t original)
		{
			static Compatibility::EventConverter converter;
			Compatibility::JJ2Level level;
			level.LightingMin = level.LightingStart = 0;
			return converter.TryConvert(&level, Compatibility::JJ2Event(original), 0);
		}

		struct CatalogIndex
		{
			SmallVector<const EventDescriptor*, 0> BuiltIn;
			SmallVector<std::unique_ptr<ExtensionEvent>, 0> Extensions;
			SmallVector<HiddenEvent, 0> Hidden;
			SmallVector<const EventDescriptor*, 0> All;
			bool HiddenByType[std::size_t(EventType::Count)];
			const EventDescriptor* ByType[std::size_t(EventType::Count)];
			// The descriptions of the game itself, whether something replaces them or not
			const EventDescriptor* BuiltInByType[std::size_t(EventType::Count)];
			// Extensions of types the game doesn't know
			std::unordered_map<std::uint16_t, const EventDescriptor*> Others;
			// Extensions of events of the original game that EventType::PassthroughEvent keeps, by their number there
			const EventDescriptor* Originals[256];
			// Those of them the game turns into its own events, see FindNative()
			SmallVector<const ExtensionEvent*, 0> Natives;
			SmallVector<EventExtensionError, 0> Errors;
			std::uint32_t Revision;

			CatalogIndex()
				: Revision(0)
			{
				std::fill(std::begin(BuiltInByType), std::end(BuiltInByType), nullptr);
				for (auto part : { EventCatalogParts::GetBasicEvents(), EventCatalogParts::GetAreaEvents(), EventCatalogParts::GetEnvironmentEvents(),
						EventCatalogParts::GetEnemyEvents(), EventCatalogParts::GetItemEvents() }) {
					for (const EventDescriptor& descriptor : part) {
						BuiltIn.push_back(&descriptor);
						if (std::size_t(descriptor.Type) < std::size_t(EventType::Count)) {
							BuiltInByType[std::size_t(descriptor.Type)] = &descriptor;
						}
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
				std::fill(std::begin(Originals), std::end(Originals), nullptr);
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
						if (extension->Descriptor.OriginalEvent != 0) {
							Originals[extension->Descriptor.OriginalEvent] = &extension->Descriptor;
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
				Natives.clear();
				for (const auto& extension : Extensions) {
					// One of the original game is placed as the passthrough event or as the event of the game it becomes, only
					// those of the level are offered, by types of their own
					std::uint8_t original = extension->Descriptor.OriginalEvent;
					if (original != 0) {
						if (Originals[original] != &extension->Descriptor) {
							continue;
						}
						if (extension->IsNative()) {
							Natives.push_back(extension.get());
						}
						if (extension->Scope == EventExtensionScope::Level && extension->ConvertedType != EventType::Empty) {
							extension->PickDescriptor = (extension->IsNative() ? extension->NativeDescriptor : extension->Descriptor);
							extension->PickDescriptor.Type = EventType(EventCatalog::FirstOriginalPickType + original);
							Others[std::uint16_t(extension->PickDescriptor.Type)] = &extension->PickDescriptor;
							All.push_back(&extension->PickDescriptor);
						}
						continue;
					}
					std::size_t type = std::size_t(extension->Descriptor.Type);
					const EventDescriptor* shown = (type < std::size_t(EventType::Count) ? ByType[type] : Others[std::uint16_t(type)]);
					if (shown == &extension->Descriptor) {
						All.push_back(shown);
					}
				}

				std::fill(std::begin(HiddenByType), std::end(HiddenByType), false);
				for (const HiddenEvent& hidden : Hidden) {
					HiddenByType[std::size_t(hidden.Type)] = true;
				}
			}

			// The description of an event of the original game a placed event of the game was made of, as the converter
			// makes it - the parameters of the game must be those, the original ones are kept after them
			const ExtensionEvent* FindNative(EventType type, const std::uint8_t* params) const
			{
				for (const ExtensionEvent* extension : Natives) {
					if (extension->ConvertedType == type && std::memcmp(extension->ConvertedParams, params, sizeof(extension->ConvertedParams)) == 0) {
						return extension;
					}
				}
				return nullptr;
			}
		};

		CatalogIndex& GetIndex()
		{
			static CatalogIndex index;
			return index;
		}

		// Copies of translated texts of the descriptions of the game, they outlive the language they were translated by
		SmallVector<std::unique_ptr<char[]>, 0>& GetKeptTexts()
		{
			static SmallVector<std::unique_ptr<char[]>, 0> texts;
			return texts;
		}

		bool ParseCategory(StringView name, EventCategory& category)
		{
			static const char* const Names[] = { "Basic", "Scenery", "Modifier", "Area", "Trigger", "Warp", "Light",
				"Environment", "Enemy", "Boss", "Collectible", "Container", "Other" };
			static_assert(std::size(Names) == std::size_t(EventCategory::Count), "Names of categories don't match");
			for (std::size_t i = 0; i < std::size(Names); i++) {
				if (StringUtils::equalsIgnoreCase(name, Names[i])) {
					category = EventCategory(i);
					return true;
				}
			}
			return false;
		}

		bool ParseParamType(StringView name, ParamType& type)
		{
			static const std::pair<const char*, ParamType> Types[] = {
				{ "UInt", ParamType::UInt }, { "Int", ParamType::Int }, { "Bool", ParamType::Bool }, { "Enum", ParamType::Enum },
				{ "Flags", ParamType::Flags }, { "Event", ParamType::EventType }, { "Text", ParamType::TextId },
				{ "Trigger", ParamType::TriggerId }, { "Warp", ParamType::WarpId }, { "String", ParamType::String },
				{ "Contents", ParamType::Contents }
			};
			for (const auto& entry : Types) {
				if (StringUtils::equalsIgnoreCase(name, entry.first)) {
					type = entry.second;
					return true;
				}
			}
			return false;
		}

		ParamUnit ParseUnit(StringView name)
		{
			static const std::pair<const char*, ParamUnit> Units[] = {
				{ "Pixels", ParamUnit::Pixels }, { "Tiles", ParamUnit::Tiles }, { "Frames", ParamUnit::Frames },
				{ "Seconds", ParamUnit::Seconds }, { "Percent", ParamUnit::Percent }, { "Degrees", ParamUnit::Degrees }
			};
			for (const auto& entry : Units) {
				if (StringUtils::equalsIgnoreCase(name, entry.first)) {
					return entry.second;
				}
			}
			return ParamUnit::None;
		}

		// Keeps what's wrong with a description, so the editor can show it, and logs it
		void ReportError(EventExtensionScope scope, StringView source, String message)
		{
			LOGW("Event descriptions in \"{}\": {}", source, message);
			CatalogIndex& catalog = GetIndex();
			// A broken file shouldn't fill the list with the same complaint over and over
			if (catalog.Errors.size() < 256) {
				catalog.Errors.push_back(EventExtensionError{ scope, String(source), Death::move(message) });
			}
			catalog.Revision++;
		}

		// A path of metadata in the content, names of letters, digits, dashes and underscores separated by slashes, so a
		// description that came with a level can't reach outside of the content
		bool IsValidMetadataPath(StringView path)
		{
			if (path.empty() || path[0] == '/' || path[path.size() - 1] == '/') {
				return false;
			}
			char prev = '\0';
			for (char c : path) {
				bool valid = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/');
				if (!valid || (c == '/' && prev == '/')) {
					return false;
				}
				prev = c;
			}
			return true;
		}

		// Reads one event, returns nullptr if it can't be used, why is logged and kept. The JSON comes from files and
		// level scripts of anyone, so every value is read through JsonAccess, a value of another type is taken as missing.
		std::unique_ptr<ExtensionEvent> ParseExtension(const Json::Value& value, StringView source, EventExtensionScope scope)
		{
			using namespace JsonAccess;

			if (!value.isObject()) {
				// TRANSLATORS: Error in event descriptions from a file or a level script, object as in a JSON object
				ReportError(scope, source, _x("editor", "An event description isn't an object"));
				return nullptr;
			}
			// An event of the original game the game has no use for is kept as the passthrough event (see
			// Compatibility::EventConverter::MakePassthroughEvent()), its number first and its parameters from the next byte
			std::int32_t original = GetInt(value, "Original", 0);
			std::int32_t type = (original != 0 ? std::int32_t(EventType::PassthroughEvent) : GetInt(value, "Type", 0));
			// The types from EventCatalog::FirstOriginalPickType on stand for the events of the original game in the list
			if (original < 0 || original > 255 || type <= 0 || (original == 0 && type >= EventCatalog::FirstOriginalPickType)) {
				// TRANSLATORS: Error in event descriptions, keep "Type" and "Original" as they are, they're keys in the JSON
				ReportError(scope, source, _x("editor", "An event description has no valid \"Type\" or \"Original\""));
				return nullptr;
			}
			String name = GetString(value, "Name");
			if (name.empty()) {
				// TRANSLATORS: Error in event descriptions, {} is the number of the event type, keep "Name" as it is, it's a key in the JSON
				ReportError(scope, source, _fx("editor", "Event {} has no \"Name\"", original != 0 ? original : type));
				return nullptr;
			}

			auto extension = std::make_unique<ExtensionEvent>();
			extension->Scope = scope;
			EventDescriptor& descriptor = extension->Descriptor;
			descriptor = {};
			descriptor.Type = EventType(type);
			descriptor.OriginalEvent = std::uint8_t(original);
			descriptor.Category = EventCategory::Other;
			descriptor.Traits = EventTraits::Object;
			String category = GetString(value, "Category", "Other"_s);
			if (!ParseCategory(category, descriptor.Category)) {
				ReportError(scope, source, _fx("editor", "Event \"{}\" has an unknown category \"{}\"", name, category));
			}
			const Json::Value& traits = Member(value, "Traits");
			if (traits.isArray()) {
				descriptor.Traits = EventTraits::None;
				for (const Json::Value& trait : traits) {
					String traitName = ToString(trait);
					if (StringUtils::equalsIgnoreCase(traitName, "Object"_s)) {
						descriptor.Traits |= EventTraits::Object;
					} else if (StringUtils::equalsIgnoreCase(traitName, "TileModifier"_s)) {
						descriptor.Traits |= EventTraits::TileModifier;
					} else if (StringUtils::equalsIgnoreCase(traitName, "Area"_s)) {
						descriptor.Traits |= EventTraits::Area;
					} else if (StringUtils::equalsIgnoreCase(traitName, "MultiplayerOnly"_s)) {
						descriptor.Traits |= EventTraits::MultiplayerOnly;
					}
				}
			}

			// All texts first, the description points into them once they don't move anymore
			auto& texts = extension->Texts;
			String key = GetString(value, "Key");
			if (key.empty()) {
				char buffer[16];
				std::int32_t length = (original != 0 ? std::snprintf(buffer, sizeof(buffer), "Original%i", original)
					: std::snprintf(buffer, sizeof(buffer), "Ext%i", type));
				key = String(buffer, std::size_t(std::max<std::int32_t>(length, 0)));
			}
			texts.push_back(Death::move(key));														// 0: key
			texts.push_back(name);																	// 1: name
			texts.push_back(GetString(value, "Description"));										// 2: description
			const Json::Value& preview = Member(value, "Preview");
			String metadata = GetString(preview, "Metadata");
			if (!metadata.empty() && !IsValidMetadataPath(metadata)) {
				// TRANSLATORS: Error in event descriptions, with the name of the event and the path of its preview sprite
				ReportError(scope, source, _fx("editor", "Event \"{}\" has an invalid preview \"{}\", it's shown as a box", name, metadata));
				metadata = {};
			}
			bool hasPreviewMetadata = !metadata.empty();
			texts.push_back(Death::move(metadata));													// 3: preview
			descriptor.PreviewAnimation = GetUInt(preview, "Animation", 0);

			// Looks by the value of a parameter, "PreviewByParam", and a mirrored or moved sprite of "Preview" itself
			struct PendingCase
			{
				EventPreviewCase Case;
				std::size_t MetadataText;
			};
			SmallVector<PendingCase, 0> pendingCases;
			bool hasAnyValueCase = false;
			auto addCase = [&](const Json::Value& item, std::int32_t caseValue, std::size_t metadataText) {
				if (pendingCases.size() >= UINT8_MAX) {
					return;
				}
				PendingCase& pendingCase = pendingCases.emplace_back();
				pendingCase.Case.Value = caseValue;
				pendingCase.Case.Metadata = nullptr;
				pendingCase.Case.Animation = GetUInt(item, "Animation", 0);
				pendingCase.Case.FlipX = GetBool(item, "FlipX", false);
				const Json::Value& offsetValue = Member(item, "Offset");
				if (offsetValue.isArray() && offsetValue.size() >= 2) {
					pendingCase.Case.OffsetX = std::int16_t(std::clamp<std::int32_t>(ToInt(offsetValue[0], 0), -1024, 1024));
					pendingCase.Case.OffsetY = std::int16_t(std::clamp<std::int32_t>(ToInt(offsetValue[1], 0), -1024, 1024));
				}
				pendingCase.MetadataText = metadataText;
				hasAnyValueCase |= (caseValue == EventPreviewCase::AnyValue);
			};
			const Json::Value* previewFieldRef = nullptr;
			const Json::Value& previewByParam = Member(value, "PreviewByParam");
			if (previewByParam.isObject()) {
				previewFieldRef = &Member(previewByParam, "Param");
				const Json::Value& cases = Member(previewByParam, "Cases");
				if (cases.isArray()) {
					for (const Json::Value& item : cases) {
						if (!item.isObject()) {
							continue;
						}
						const Json::Value& caseValue = Member(item, "Value");
						bool isDefault = (caseValue.isString() && StringUtils::equalsIgnoreCase(ToString(caseValue), "Default"_s));
						if (!isDefault && !caseValue.isNumeric()) {
							// TRANSLATORS: Error in event descriptions, {} is the name of the event, keep "Value" as it is, it's a key in the JSON
							ReportError(scope, source, _fx("editor", "A preview case of event \"{}\" has no \"Value\", it's left out", name));
							continue;
						}
						String caseMetadata = GetString(item, "Metadata");
						if (!caseMetadata.empty() && !IsValidMetadataPath(caseMetadata)) {
							ReportError(scope, source, _fx("editor", "Event \"{}\" has an invalid preview \"{}\", it's shown as a box", name, caseMetadata));
							caseMetadata = {};
						}
						std::size_t metadataText = (caseMetadata.empty() ? SIZE_MAX : texts.size());
						if (!caseMetadata.empty()) {
							texts.push_back(Death::move(caseMetadata));
						}
						addCase(item, isDefault ? EventPreviewCase::AnyValue : ToInt(caseValue, 0), metadataText);
					}
				}
			}
			if (hasPreviewMetadata && !hasAnyValueCase && (Member(preview, "FlipX").isBool() || Member(preview, "Offset").isArray())) {
				addCase(preview, EventPreviewCase::AnyValue, 3);
			}

			struct PendingField
			{
				EventParamField Field;
				std::size_t NameText;
				std::size_t DescriptionText;
				std::size_t OptionsIndex;
				SmallVector<std::size_t, 0> OptionTexts;
				// Other parameters referred to by their names or indices, found once all of them are known
				const Json::Value* VisibleIfRef = nullptr;
				const Json::Value* MirrorXRef = nullptr;
				const Json::Value* MirrorYRef = nullptr;
				const Json::Value* ContentsTypeRef = nullptr;
				std::size_t MirrorXPairs = SIZE_MAX;
				std::size_t MirrorYPairs = SIZE_MAX;
				std::size_t LinkChannelText = SIZE_MAX;
			};
			SmallVector<PendingField, 0> pending;
			// Of an event of the original game, its number first, so it can be changed, and its parameters are the bits of
			// its parameters there, one after another unless "shift" says where
			std::int32_t nextOriginalShift = 0;
			if (original != 0) {
				PendingField& numberField = pending.emplace_back();
				numberField.Field = UIntField(nullptr, 0, 8, 1, 255, original);
				numberField.NameText = texts.size();
				// TRANSLATORS: Parameter of an event of the original game, its number there
				texts.push_back(_x("editor", "Event"));
				numberField.DescriptionText = SIZE_MAX;
				numberField.OptionsIndex = SIZE_MAX;
			}
			const Json::Value& params = Member(value, "Params");
			if (params.isArray()) {
				for (const Json::Value& param : params) {
					String paramName = GetString(param, "Name");
					ParamType paramType = ParamType::UInt;
					if (paramName.empty() || !ParseParamType(GetString(param, "Type", "UInt"_s), paramType)) {
						ReportError(scope, source, _fx("editor", "A parameter of event \"{}\" has no name or an unknown type, it's left out", name));
						continue;
					}
					std::int32_t offset = GetInt(param, "Offset", original != 0 ? 1 : -1);
					std::int32_t bits = GetInt(param, "Bits", paramType == ParamType::Bool ? 1 : (paramType == ParamType::EventType ? 16 : 8));
					std::int32_t shift = GetInt(param, "Shift", original != 0 ? nextOriginalShift : 0);
					nextOriginalShift = shift + bits;
					// Texts and the parameters of an event inside take whole bytes
					bool isBytes = (paramType == ParamType::String || paramType == ParamType::Contents);
					if (isBytes) {
						std::int32_t length = GetInt(param, "Length", paramType == ParamType::Contents ? Formats::LevelFile::EventParamsSize - offset : bits / 8);
						bits = (length > 0 && length <= Formats::LevelFile::EventParamsSize ? length * 8 : 0);
						shift = 0;
					}
					// Each part checked on its own first, the sum of two huge values wouldn't fit
					bool fits = (offset >= 0 && offset < Formats::LevelFile::EventParamsSize && bits > 0 && shift >= 0 &&
						(isBytes || (bits <= 32 && shift < 32 && shift + bits <= 32)));
					if (fits) {
						std::int32_t storage = (isBytes ? bits / 8 : ((shift + bits) <= 8 ? 1 : ((shift + bits) <= 16 ? 2 : 4)));
						fits = (offset + storage <= Formats::LevelFile::EventParamsSize);
					}
					if (!fits) {
						ReportError(scope, source, _fx("editor", "Parameter \"{}\" of event \"{}\" doesn't fit into the parameters, it's left out", paramName, name));
						continue;
					}

					// The values a field of the bits can hold, within the 32-bit numbers the editor works with, an
					// unsigned field of 32 bits can't go higher than the largest signed one then
					bool isSigned = (paramType == ParamType::Int);
					std::int64_t lowest = 0, highest = 0;
					if (!isBytes) {
						if (isSigned) {
							lowest = -(std::int64_t(1) << (bits - 1));
							highest = (std::int64_t(1) << (bits - 1)) - 1;
						} else {
							highest = std::min<std::int64_t>((std::int64_t(1) << bits) - 1, INT32_MAX);
						}
					}
					std::int32_t minValue = std::clamp<std::int32_t>(GetInt(param, "Min", std::int32_t(lowest)), std::int32_t(lowest), std::int32_t(highest));
					std::int32_t maxValue = std::clamp<std::int32_t>(GetInt(param, "Max", std::int32_t(highest)), std::int32_t(lowest), std::int32_t(highest));
					if (minValue > maxValue) {
						// TRANSLATORS: Error in event descriptions, with the name of a parameter and of its event, keep "Min" and "Max" as they are, they are keys in the JSON
						ReportError(scope, source, _fx("editor", "Parameter \"{}\" of event \"{}\" has \"Min\" above \"Max\", the whole range is used", paramName, name));
						minValue = std::int32_t(lowest);
						maxValue = std::int32_t(highest);
					}
					// A number is edited as a float, which holds whole numbers exactly only up to 2^24, a wider one would be
					// changed by just being shown, so the range is cut to that; only one asked for is worth a word
					bool isNumber = (paramType == ParamType::UInt || paramType == ParamType::Int || paramType == ParamType::TextId ||
						paramType == ParamType::TriggerId || paramType == ParamType::WarpId);
					constexpr std::int32_t ExactLimit = (1 << 24);
					if (isNumber && (minValue < -ExactLimit || maxValue > ExactLimit)) {
						minValue = std::clamp<std::int32_t>(minValue, -ExactLimit, ExactLimit);
						maxValue = std::clamp<std::int32_t>(maxValue, -ExactLimit, ExactLimit);
						if (Member(param, "Min").isNumeric() || Member(param, "Max").isNumeric()) {
							// TRANSLATORS: Warning in event descriptions, with the name of a parameter, of its event, and the lowest and the highest value it can have
							ReportError(scope, source, _fx("editor", "Parameter \"{}\" of event \"{}\" can be edited only from {} to {}", paramName, name, minValue, maxValue));
						}
					}

					PendingField& field = pending.emplace_back();
					field.Field = {};
					field.Field.Type = paramType;
					field.Field.Offset = std::uint8_t(offset);
					field.Field.Bits = std::uint8_t(bits);
					field.Field.Shift = std::uint8_t(shift);
					field.Field.Signed = isSigned;
					field.Field.Min = minValue;
					field.Field.Max = maxValue;
					field.Field.Default = GetInt(param, "Default", 0);
					field.Field.Unit = ParseUnit(GetString(param, "Unit"));
					field.NameText = texts.size();
					texts.push_back(Death::move(paramName));
					String paramDescription = GetString(param, "Description");
					field.DescriptionText = (paramDescription.empty() ? SIZE_MAX : texts.size());
					if (!paramDescription.empty()) {
						texts.push_back(Death::move(paramDescription));
					}
					field.OptionsIndex = SIZE_MAX;
					const Json::Value& options = Member(param, "Options");
					if (options.isArray() && (paramType == ParamType::Enum || paramType == ParamType::Flags)) {
						field.OptionsIndex = extension->Options.size();
						auto& list = extension->Options.emplace_back();
						std::uint32_t nextValue = (paramType == ParamType::Flags ? 1 : 0);
						for (const Json::Value& option : options) {
							// The number of options is kept in a byte
							if (list.size() >= UINT8_MAX) {
								break;
							}
							String label;
							std::int32_t optionValue = std::int32_t(nextValue);
							if (option.isObject()) {
								label = GetString(option, "Label");
								optionValue = GetInt(option, "Value", optionValue);
							} else {
								label = ToString(option);
							}
							if (label.empty()) {
								char buffer[16];
								std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%i", optionValue);
								label = String(buffer, std::size_t(std::max<std::int32_t>(length, 0)));
							}
							// Unsigned, so the bit of the next flag can't overflow into undefined behavior
							nextValue = (paramType == ParamType::Flags ? std::uint32_t(optionValue) << 1 : std::uint32_t(optionValue) + 1);
							field.OptionTexts.push_back(texts.size());
							texts.push_back(Death::move(label));
							list.push_back(ParamOption{ nullptr, optionValue });
						}
					}

					// Shown only while another parameter has a value, or never
					const Json::Value& showIf = Member(param, "ShowIf");
					if (showIf.isObject()) {
						field.VisibleIfRef = &Member(showIf, "Param");
						if (Member(showIf, "NotEquals").isNumeric()) {
							field.Field.VisibleIfMode = FieldCondition::NotEquals;
							field.Field.VisibleIfValue = GetInt(showIf, "NotEquals", 0);
						} else if (Member(showIf, "AnyBits").isNumeric()) {
							field.Field.VisibleIfMode = FieldCondition::AnyBits;
							field.Field.VisibleIfValue = GetInt(showIf, "AnyBits", 0);
						} else {
							field.Field.VisibleIfMode = FieldCondition::Equals;
							field.Field.VisibleIfValue = GetInt(showIf, "Equals", 0);
						}
					}
					field.Field.Hidden = GetBool(param, "Hidden", false);
					field.Field.DisplayScale = std::clamp<std::int32_t>(GetInt(param, "Scale", 1), 1, 65536);

					// What mirroring the event does to the value, a name or a table of pairs to swap
					auto parseMirror = [&](const char* key, FieldMirror& mirror, const Json::Value*& fieldRef, std::size_t& pairsIndex) {
						const Json::Value& mirrorValue = Member(param, key);
						if (mirrorValue.isString()) {
							String mode = ToString(mirrorValue);
							if (StringUtils::equalsIgnoreCase(mode, "Negate"_s)) {
								mirror.Mode = MirrorMode::Negate;
							} else if (StringUtils::equalsIgnoreCase(mode, "Toggle"_s)) {
								mirror.Mode = MirrorMode::Toggle;
							}
						} else if (mirrorValue.isObject()) {
							const Json::Value& pairs = Member(mirrorValue, "Swap");
							if (pairs.isArray() && pairs.size() >= 2) {
								mirror.Mode = MirrorMode::Swap;
								pairsIndex = extension->MirrorPairs.size();
								auto& list = extension->MirrorPairs.emplace_back();
								for (const Json::Value& pairValue : pairs) {
									if (list.size() >= UINT8_MAX - 1) {
										break;
									}
									list.push_back(ToInt(pairValue, 0));
								}
								// Only whole pairs
								if ((list.size() & 1) != 0) {
									list.pop_back();
								}
							} else if (!Member(mirrorValue, "SwapWith").isNull()) {
								mirror.Mode = MirrorMode::SwapField;
								fieldRef = &Member(mirrorValue, "SwapWith");
							}
						}
					};
					parseMirror("MirrorX", field.Field.MirrorX, field.MirrorXRef, field.MirrorXPairs);
					parseMirror("MirrorY", field.Field.MirrorY, field.MirrorYRef, field.MirrorYPairs);
					field.Field.IsTeam = GetBool(param, "Team", false);

					// A handle in the level
					String gizmo = GetString(param, "Gizmo");
					if (!gizmo.empty()) {
						if (StringUtils::equalsIgnoreCase(gizmo, "Radius"_s)) {
							field.Field.Gizmo = FieldGizmo::Radius;
						} else if (StringUtils::equalsIgnoreCase(gizmo, "Length"_s)) {
							field.Field.Gizmo = FieldGizmo::LengthDown;
						} else if (StringUtils::equalsIgnoreCase(gizmo, "Width"_s)) {
							field.Field.Gizmo = FieldGizmo::WidthRight;
						} else if (StringUtils::equalsIgnoreCase(gizmo, "OffsetX"_s)) {
							field.Field.Gizmo = FieldGizmo::OffsetX;
						} else if (StringUtils::equalsIgnoreCase(gizmo, "OffsetY"_s)) {
							field.Field.Gizmo = FieldGizmo::OffsetY;
						} else {
							// TRANSLATORS: Error in event descriptions, with the name of a parameter, of its event and the unknown value, keep "Gizmo" as it is, it's a key in the JSON
							ReportError(scope, source, _fx("editor", "Parameter \"{}\" of event \"{}\" has an unknown \"Gizmo\" \"{}\"", texts[field.NameText], name, gizmo));
						}
						field.Field.GizmoStep = std::int16_t(std::clamp<std::int32_t>(GetInt(param, "GizmoStep", 1), 1, 1024));
					}

					// Events with the same value in the same channel are linked by lines
					const Json::Value& link = Member(param, "Link");
					if (link.isObject()) {
						String channel = GetString(link, "Channel");
						String role = GetString(link, "Role", "Peer"_s);
						if (!channel.empty()) {
							field.Field.LinkRole = (StringUtils::equalsIgnoreCase(role, "Source"_s) ? EventLinkRole::Source : (StringUtils::equalsIgnoreCase(role, "Target"_s) ? EventLinkRole::Target : EventLinkRole::Peer));
							field.LinkChannelText = texts.size();
							texts.push_back(Death::move(channel));
						}
					}

					// An arrow, the values of a group summed along their axes
					const Json::Value& direction = Member(param, "Direction");
					if (direction.isObject()) {
						String axis = GetString(direction, "Axis");
						field.Field.Direction = (StringUtils::equalsIgnoreCase(axis, "X"_s) ? DirectionAxis::X : (StringUtils::equalsIgnoreCase(axis, "Y"_s) ? DirectionAxis::Y : DirectionAxis::None));
						field.Field.DirectionNegative = GetBool(direction, "Negative", false);
						field.Field.DirectionGroup = std::uint8_t(std::clamp<std::int32_t>(GetInt(direction, "Group", 0), 0, 3));
					}

					if (paramType == ParamType::Contents) {
						field.ContentsTypeRef = &Member(param, "TypeParam");
					}
				}
			}

			// Parameters referred to by others, by their name or index
			auto resolveField = [&](const Json::Value* ref, const char* what, const String& owner) -> std::int8_t {
				if (ref == nullptr || ref->isNull()) {
					return -1;
				}
				if (ref->isString()) {
					String target = ToString(*ref);
					for (std::size_t i = 0; i < pending.size() && i < 128; i++) {
						if (texts[pending[i].NameText] == target) {
							return std::int8_t(i);
						}
					}
				} else {
					std::int32_t index = ToInt(*ref, -1);
					if (index >= 0 && index < std::int32_t(pending.size()) && index < 128) {
						return std::int8_t(index);
					}
				}
				// TRANSLATORS: Error in event descriptions, with a key in the JSON (kept as it is), the name of a parameter and the name of its event
				ReportError(scope, source, _fx("editor", "\"{}\" of \"{}\" in event \"{}\" refers to no parameter", what, owner, name));
				return -1;
			};
			for (PendingField& field : pending) {
				const String& paramName = texts[field.NameText];
				field.Field.VisibleIfField = resolveField(field.VisibleIfRef, "ShowIf", paramName);
				if (field.Field.MirrorX.Mode == MirrorMode::SwapField) {
					field.Field.MirrorX.Field = resolveField(field.MirrorXRef, "MirrorX", paramName);
					if (field.Field.MirrorX.Field < 0) {
						field.Field.MirrorX.Mode = MirrorMode::None;
					}
				}
				if (field.Field.MirrorY.Mode == MirrorMode::SwapField) {
					field.Field.MirrorY.Field = resolveField(field.MirrorYRef, "MirrorY", paramName);
					if (field.Field.MirrorY.Field < 0) {
						field.Field.MirrorY.Mode = MirrorMode::None;
					}
				}
				if (field.Field.Type == ParamType::Contents) {
					field.Field.ContentsTypeField = resolveField(field.ContentsTypeRef, "TypeParam", paramName);
					if (field.Field.ContentsTypeField >= 0) {
						// The type of the event inside can't be among its own parameters, it would be inside of itself
						const EventParamField& typeField = pending[field.Field.ContentsTypeField].Field;
						bool typeIsBytes = (typeField.Type == ParamType::String || typeField.Type == ParamType::Contents);
						std::uint32_t typeLastBit = std::uint32_t(typeField.Shift) + typeField.Bits;
						std::uint32_t typeStart = typeField.Offset;
						std::uint32_t typeEnd = typeStart + (typeIsBytes ? typeField.Bits / 8u : (typeLastBit <= 8 ? 1u : (typeLastBit <= 16 ? 2u : 4u)));
						std::uint32_t contentsStart = field.Field.Offset;
						std::uint32_t contentsEnd = contentsStart + field.Field.Bits / 8u;
						if (typeStart < contentsEnd && contentsStart < typeEnd) {
							// TRANSLATORS: Error in event descriptions, with the name of a parameter and of its event, keep "TypeParam" as it is, it's a key in the JSON
							ReportError(scope, source, _fx("editor", "Parameter \"{}\" of event \"{}\" contains its own \"TypeParam\", the event inside is left out", paramName, name));
							field.Field.ContentsTypeField = -1;
						}
					}
				}
			}
			descriptor.PreviewField = resolveField(previewFieldRef, "PreviewByParam", String(name));

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
				if (field.MirrorXPairs != SIZE_MAX) {
					const auto& pairs = extension->MirrorPairs[field.MirrorXPairs];
					result.MirrorX.Pairs = pairs.data();
					result.MirrorX.PairCount = std::uint8_t(pairs.size());
				}
				if (field.MirrorYPairs != SIZE_MAX) {
					const auto& pairs = extension->MirrorPairs[field.MirrorYPairs];
					result.MirrorY.Pairs = pairs.data();
					result.MirrorY.PairCount = std::uint8_t(pairs.size());
				}
				result.LinkChannel = (field.LinkChannelText != SIZE_MAX ? texts[field.LinkChannelText].data() : nullptr);
			}
			descriptor.Fields = extension->Fields.data();
			descriptor.FieldCount = std::uint8_t(std::min<std::size_t>(extension->Fields.size(), UINT8_MAX));
			for (PendingCase& pendingCase : pendingCases) {
				EventPreviewCase& result = extension->PreviewCases.emplace_back(pendingCase.Case);
				result.Metadata = (pendingCase.MetadataText != SIZE_MAX && !texts[pendingCase.MetadataText].empty() ? texts[pendingCase.MetadataText].data() : nullptr);
			}
			if (!extension->PreviewCases.empty()) {
				descriptor.PreviewCases = extension->PreviewCases.data();
				descriptor.PreviewCaseCount = std::uint8_t(extension->PreviewCases.size());
			}
			return extension;
		}

		// How an event of the original game is made in the game, and described then if the game turns it into its own event:
		// its parameters are the original ones the converter keeps in the last bytes (see Compatibility::EventConverter::
		// KeepOriginalParams()), without its number, the type stands for that
		void PrepareOriginal(ExtensionEvent& extension)
		{
			Compatibility::ConversionResult converted = ConvertOriginal(extension.Descriptor.OriginalEvent);
			extension.ConvertedType = converted.Type;
			std::memcpy(extension.ConvertedParams, converted.Params, sizeof(extension.ConvertedParams));
			if (!extension.IsNative()) {
				return;
			}
			constexpr std::int32_t Kept = Formats::EventRecord::OriginalParamsOffset;
			const std::uint8_t* tail = converted.Params + Kept;
			if (Compatibility::EventConverter::CanKeepOriginalParams(converted.Type) && tail[0] == 0 && tail[1] == 0 && tail[2] == 0) {
				// The first field is the number, the others refer to each other by their indices
				auto withoutNumber = [](std::int8_t index) -> std::int8_t {
					return (index > 0 ? std::int8_t(index - 1) : std::int8_t(-1));
				};
				for (std::uint8_t i = 1; i < extension.Descriptor.FieldCount; i++) {
					EventParamField field = extension.Descriptor.Fields[i];
					std::int32_t offset = std::int32_t(field.Offset) - 1 + Kept;
					if (field.Type == ParamType::String || field.Type == ParamType::Contents || field.Type == ParamType::Raw ||
						offset >= Formats::LevelFile::EventParamsSize) {
						continue;
					}
					field.Offset = std::uint8_t(offset);
					field.VisibleIfField = withoutNumber(field.VisibleIfField);
					field.MirrorX.Field = withoutNumber(field.MirrorX.Field);
					field.MirrorY.Field = withoutNumber(field.MirrorY.Field);
					extension.NativeFields.push_back(field);
				}
			}
			extension.NativeDescriptor = extension.Descriptor;
			extension.NativeDescriptor.Type = converted.Type;
			extension.NativeDescriptor.Fields = extension.NativeFields.data();
			extension.NativeDescriptor.FieldCount = std::uint8_t(extension.NativeFields.size());
			// The looks by a parameter are of the fields of the passthrough event
			extension.NativeDescriptor.PreviewCases = nullptr;
			extension.NativeDescriptor.PreviewCaseCount = 0;
			extension.NativeDescriptor.PreviewField = -1;
		}

		// Adds a description of an event, a later one of the same event in the same scope replaces the earlier one
		std::int32_t AddExtension(const Json::Value& value, StringView source, EventExtensionScope scope)
		{
			auto extension = ParseExtension(value, source, scope);
			if (extension == nullptr) {
				return 0;
			}
			if (extension->Descriptor.OriginalEvent != 0) {
				PrepareOriginal(*extension);
			}
			CatalogIndex& catalog = GetIndex();
			for (std::size_t i = 0; i < catalog.Extensions.size(); i++) {
				if (catalog.Extensions[i]->Scope == scope && catalog.Extensions[i]->Descriptor.Type == extension->Descriptor.Type &&
						catalog.Extensions[i]->Descriptor.OriginalEvent == extension->Descriptor.OriginalEvent) {
					catalog.Extensions.erase(catalog.Extensions.begin() + i);
					break;
				}
			}
			catalog.Extensions.push_back(Death::move(extension));
			return 1;
		}

		// A whole number of a description of MLLE, 0 if there's none
		std::int32_t ParseMlleNumber(StringView text)
		{
			std::int32_t number = 0;
			for (std::size_t i = 0; i < text.size() && text[i] >= '0' && text[i] <= '9' && number < 100000; i++) {
				number = number * 10 + (text[i] - '0');
			}
			return number;
		}

		// The category of an event of MLLE (as in JCS.ini) among those of the editor
		const char* MapMlleCategory(StringView category)
		{
			static const std::pair<const char*, const char*> Categories[] = {
				{ "Area", "Area" }, { "Enemy", "Enemy" }, { "Boss", "Boss" }, { "Modifier", "Modifier" }, { "Scenery", "Scenery" },
				{ "Platform", "Scenery" }, { "Trigger", "Trigger" }, { "Warp", "Warp" }, { "Light", "Light" }, { "Ammo", "Collectible" },
				{ "Food", "Collectible" }, { "Gem", "Collectible" }, { "Gems", "Collectible" }, { "Goodies", "Collectible" },
				{ "Pickup", "Collectible" }, { "Powerup", "Collectible" }, { "Morph", "Modifier" }, { "Sound", "Environment" }
			};
			category = category.trimmed();
			for (const auto& entry : Categories) {
				if (StringUtils::equalsIgnoreCase(category, entry.first)) {
					return entry.second;
				}
			}
			return "Other";
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

	CatalogText::CatalogText(StringView translated)
	{
		// The descriptions are built once, while the index is, so each text is copied only once
		std::unique_ptr<char[]> copy = std::make_unique<char[]>(translated.size() + 1);
		std::memcpy(copy.get(), translated.data(), translated.size());
		copy[translated.size()] = '\0';
		Text = copy.get();
		GetKeptTexts().push_back(std::move(copy));
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

	const EventDescriptor* EventCatalog::Find(EventType type, const std::uint8_t* params)
	{
		if (params != nullptr) {
			const CatalogIndex& catalog = GetIndex();
			if (type == EventType::PassthroughEvent) {
				if (const EventDescriptor* original = catalog.Originals[params[0]]) {
					return original;
				}
			} else if (const ExtensionEvent* native = catalog.FindNative(type, params)) {
				return &native->NativeDescriptor;
			}
		}
		return Find(type);
	}

	std::int32_t EventCatalog::LoadExtensions(StringView json, StringView source, EventExtensionScope scope)
	{
		Json::Value doc; std::string errors;
		if (!JsonFile::Parse(json, doc, &errors)) {
			// TRANSLATORS: Error in event descriptions, {} is the error message of the JSON parser
			ReportError(scope, source, _fx("editor", "The event descriptions can't be read - {}", StringView(errors.data(), errors.size()).trimmed()));
			return 0;
		}
		const Json::Value* list = &doc;
		const Json::Value& events = JsonAccess::Member(doc, "Events");
		if (events.isArray()) {
			list = &events;
		}

		std::int32_t added = 0;
		if (list->isArray()) {
			for (const Json::Value& value : *list) {
				added += AddExtension(value, source, scope);
			}
		} else {
			added += AddExtension(*list, source, scope);
		}
		if (added > 0) {
			GetIndex().Rebuild();
		}
		return added;
	}

	std::int32_t EventCatalog::LoadMlleEvents(StringView script, StringView source, EventExtensionScope scope)
	{
		constexpr StringView Prefix = "///@Event"_s;
		CatalogIndex& catalog = GetIndex();
		std::int32_t added = 0;
		bool anyHidden = false;
		StringView rest = script;
		while (!rest.empty()) {
			auto lineParts = rest.partition('\n');
			StringView line = lineParts[0].trimmed();
			rest = lineParts[2];
			if (!line.hasPrefix(Prefix)) {
				continue;
			}

			// `<number>=<name>|<+ or ->|<category>|<label>|<label>|<parameters>...`, as in JCS.ini
			auto definition = line.exceptPrefix(Prefix.size()).partition('=');
			std::int32_t number = ParseMlleNumber(definition[0].trimmed());
			SmallVector<StringView, 16> fields;
			for (StringView field = definition[2]; !field.empty() || fields.empty(); ) {
				auto fieldParts = field.partition('|');
				fields.push_back(fieldParts[0].trimmed());
				if (fieldParts[1].empty()) {
					break;
				}
				field = fieldParts[2];
			}

			if (fields.size() == 1 && fields[0].empty()) {
				// Nothing after `=` hides the event of the original game from the list of MLLE, here the event it becomes
				EventType hidden = (number > 0 && number <= 255 ? ConvertOriginal(number).Type : EventType::Empty);
				if (hidden != EventType::Empty && hidden != EventType::PassthroughEvent && std::size_t(hidden) < std::size_t(EventType::Count)) {
					catalog.Hidden.push_back(HiddenEvent{ scope, hidden });
					anyHidden = true;
				}
				continue;
			}

			Json::Value value(Json::objectValue);
			value["Original"] = Json::Int(number);
			value["Name"] = std::string_view(fields[0].data(), fields[0].size());
			if (fields.size() > 2) {
				value["Category"] = MapMlleCategory(fields[2]);
			}
			Json::Value& params = value["Params"];
			params = Json::Value(Json::arrayValue);
			for (std::size_t i = 5; i < fields.size(); i++) {
				// `Name:8`, `Name:-8` signed, `Name:c1` a check box, `Name:{A,B,C}2` named values
				auto paramParts = fields[i].partition(':');
				StringView name = paramParts[0].trimmed();
				StringView spec = paramParts[2].trimmed();
				if (name.empty() || spec.empty()) {
					continue;
				}
				Json::Value& param = params.append(Json::Value(Json::objectValue));
				param["Name"] = std::string_view(name.data(), name.size());
				if (spec[0] == '{') {
					auto options = spec.exceptPrefix(1).partition('}');
					param["Type"] = "Enum";
					Json::Value& list = param["Options"];
					list = Json::Value(Json::arrayValue);
					for (StringView option = options[0]; ; ) {
						auto optionParts = option.partition(',');
						StringView label = optionParts[0].trimmed();
						list.append(std::string_view(label.data(), label.size()));
						if (optionParts[1].empty()) {
							break;
						}
						option = optionParts[2];
					}
					spec = options[2];
				} else if (spec[0] == '-') {
					param["Type"] = "Int";
					spec = spec.exceptPrefix(1);
				} else if (spec[0] == 'c' || spec[0] == 'C') {
					param["Type"] = "Bool";
					spec = spec.exceptPrefix(1);
				}
				std::int32_t bits = ParseMlleNumber(spec.trimmed());
				if (bits > 0) {
					param["Bits"] = Json::Int(bits);
				}
			}
			added += AddExtension(value, source, scope);
		}
		if (added > 0 || anyHidden) {
			catalog.Rebuild();
		}
		return added;
	}

	void EventCatalog::ClearExtensions(EventExtensionScope scope)
	{
		CatalogIndex& catalog = GetIndex();
		std::size_t errorCount = catalog.Errors.size();
		catalog.Errors.erase(std::remove_if(catalog.Errors.begin(), catalog.Errors.end(),
			[scope](const EventExtensionError& error) { return error.Scope == scope; }), catalog.Errors.end());
		std::size_t count = catalog.Extensions.size() + catalog.Hidden.size();
		catalog.Extensions.erase(std::remove_if(catalog.Extensions.begin(), catalog.Extensions.end(),
			[scope](const std::unique_ptr<ExtensionEvent>& extension) { return extension->Scope == scope; }), catalog.Extensions.end());
		catalog.Hidden.erase(std::remove_if(catalog.Hidden.begin(), catalog.Hidden.end(),
			[scope](const HiddenEvent& hidden) { return hidden.Scope == scope; }), catalog.Hidden.end());
		if (catalog.Extensions.size() + catalog.Hidden.size() != count) {
			catalog.Rebuild();
		} else if (catalog.Errors.size() != errorCount) {
			catalog.Revision++;
		}
	}

	ArrayView<const EventExtensionError> EventCatalog::GetExtensionErrors()
	{
		const auto& errors = GetIndex().Errors;
		return ArrayView<const EventExtensionError>(errors.data(), errors.size());
	}

	std::uint32_t EventCatalog::GetRevision()
	{
		return GetIndex().Revision;
	}

	bool EventCatalog::IsExtension(const EventDescriptor* descriptor)
	{
		// The descriptions of the game are all known by their type, anything else came from outside of the game
		if (descriptor == nullptr) {
			return false;
		}
		std::size_t type = std::size_t(descriptor->Type);
		return (type >= std::size_t(EventType::Count) || GetIndex().BuiltInByType[type] != descriptor);
	}

	std::uint8_t EventCatalog::GetPickedOriginal(EventType type)
	{
		if (std::uint16_t(type) < FirstOriginalPickType) {
			return 0;
		}
		const EventDescriptor* descriptor = Find(type);
		return (descriptor != nullptr ? descriptor->OriginalEvent : 0);
	}

	bool EventCatalog::IsHidden(const EventDescriptor* descriptor)
	{
		return (descriptor != nullptr && std::size_t(descriptor->Type) < std::size_t(EventType::Count) &&
			GetIndex().HiddenByType[std::size_t(descriptor->Type)]);
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
			result = result.prefix(Death::Utf8::FitPrefix(result, maxLength)) + "..."_s;
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
			// TRANSLATORS: Names of categories of events in the list of events
			case EventCategory::Basic: return _x("editor", "Basic");
			case EventCategory::Scenery: return _x("editor", "Scenery");
			case EventCategory::Modifier: return _x("editor", "Tile modifiers");
			case EventCategory::Area: return _x("editor", "Areas");
			case EventCategory::Trigger: return _x("editor", "Triggers");
			case EventCategory::Warp: return _x("editor", "Warps");
			case EventCategory::Light: return _x("editor", "Lights");
			case EventCategory::Environment: return _x("editor", "Environment");
			case EventCategory::Enemy: return _x("editor", "Enemies");
			case EventCategory::Boss: return _x("editor", "Bosses");
			case EventCategory::Collectible: return _x("editor", "Collectibles");
			case EventCategory::Container: return _x("editor", "Containers");
			default: return _x("editor", "Other");
		}
	}

	std::int32_t EventCatalog::ReadField(const Formats::EventRecord& event, const EventParamField& field)
	{
		if (field.Type == ParamType::String || field.Type == ParamType::Raw || field.Type == ParamType::Contents) {
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
		if (field.Type == ParamType::String || field.Type == ParamType::Raw || field.Type == ParamType::Contents) {
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
		if (std::uint8_t original = GetPickedOriginal(type)) {
			// Placed as the converter makes it, the event of the game it becomes, or the passthrough event that keeps it
			const ExtensionEvent* extension = nullptr;
			for (const auto& candidate : GetIndex().Extensions) {
				if (&candidate->PickDescriptor == Find(type)) {
					extension = candidate.get();
					break;
				}
			}
			if (extension != nullptr && extension->IsNative()) {
				event.Type = std::uint16_t(extension->ConvertedType);
				std::memcpy(event.Params, extension->ConvertedParams, sizeof(extension->ConvertedParams));
			} else {
				event.Type = std::uint16_t(EventType::PassthroughEvent);
				event.Params[0] = original;
			}
		}
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

	const EventPreviewCase* EventCatalog::FindPreviewCase(const EventDescriptor& descriptor, const Formats::EventRecord& event)
	{
		if (descriptor.PreviewCases == nullptr) {
			return nullptr;
		}
		const EventPreviewCase* anyValue = nullptr;
		bool hasField = (descriptor.PreviewField >= 0 && std::size_t(descriptor.PreviewField) < descriptor.FieldCount);
		std::int32_t value = (hasField ? ReadField(event, descriptor.Fields[descriptor.PreviewField]) : 0);
		for (std::uint8_t i = 0; i < descriptor.PreviewCaseCount; i++) {
			const EventPreviewCase& previewCase = descriptor.PreviewCases[i];
			if (previewCase.Value == EventPreviewCase::AnyValue) {
				anyValue = &previewCase;
			} else if (hasField && previewCase.Value == value) {
				return &previewCase;
			}
		}
		return anyValue;
	}

	bool EventCatalog::IsFieldVisible(const EventDescriptor& descriptor, std::size_t fieldIndex, const Formats::EventRecord& event)
	{
		if (fieldIndex >= descriptor.FieldCount) {
			return false;
		}
		const EventParamField& field = descriptor.Fields[fieldIndex];
		if (field.Hidden) {
			return false;
		}
		// A condition on a field that isn't there, or on itself, is no condition
		if (field.VisibleIfField < 0 || std::size_t(field.VisibleIfField) >= descriptor.FieldCount || std::size_t(field.VisibleIfField) == fieldIndex) {
			return true;
		}
		std::int32_t value = ReadField(event, descriptor.Fields[field.VisibleIfField]);
		switch (field.VisibleIfMode) {
			case FieldCondition::NotEquals: return (value != field.VisibleIfValue);
			case FieldCondition::AnyBits: return ((value & field.VisibleIfValue) != 0);
			default: return (value == field.VisibleIfValue);
		}
	}

	std::int32_t EventCatalog::ToDisplayValue(const EventParamField& field, std::int32_t value)
	{
		std::int64_t shown = std::int64_t(value) * std::max<std::int32_t>(field.DisplayScale, 1);
		return std::int32_t(std::clamp<std::int64_t>(shown, INT32_MIN, INT32_MAX));
	}

	std::int32_t EventCatalog::FromDisplayValue(const EventParamField& field, std::int32_t displayValue)
	{
		std::int64_t scale = std::max<std::int32_t>(field.DisplayScale, 1);
		std::int64_t shown = displayValue;
		// To the nearest step, halves away from zero
		std::int64_t value = (shown >= 0 ? (shown + scale / 2) / scale : -((-shown + scale / 2) / scale));
		return std::int32_t(std::clamp<std::int64_t>(value, INT32_MIN, INT32_MAX));
	}

	const EventDescriptor* EventCatalog::GetContentsDescriptor(const EventDescriptor& descriptor, const EventParamField& contentsField, const Formats::EventRecord& event)
	{
		if (contentsField.Type != ParamType::Contents || contentsField.ContentsTypeField < 0 || std::size_t(contentsField.ContentsTypeField) >= descriptor.FieldCount) {
			return nullptr;
		}
		std::int32_t type = ReadField(event, descriptor.Fields[contentsField.ContentsTypeField]);
		return (type > 0 && type < FirstOriginalPickType ? Find(EventType(type)) : nullptr);
	}

	Formats::EventRecord EventCatalog::GetContents(const EventDescriptor& descriptor, const EventParamField& contentsField, const Formats::EventRecord& event)
	{
		Formats::EventRecord contents;
		std::memset(&contents, 0, sizeof(contents));
		contents.Flags = event.Flags;
		if (contentsField.Type != ParamType::Contents) {
			return contents;
		}
		if (contentsField.ContentsTypeField >= 0 && std::size_t(contentsField.ContentsTypeField) < descriptor.FieldCount) {
			contents.Type = std::uint16_t(ReadField(event, descriptor.Fields[contentsField.ContentsTypeField]));
		}
		std::size_t length = std::min<std::size_t>(contentsField.Bits / 8, std::size_t(Formats::LevelFile::EventParamsSize - std::min<std::size_t>(contentsField.Offset, Formats::LevelFile::EventParamsSize)));
		std::memcpy(contents.Params, &event.Params[contentsField.Offset], length);
		return contents;
	}

	void EventCatalog::SetContents(const EventParamField& contentsField, Formats::EventRecord& event, const Formats::EventRecord& contents)
	{
		if (contentsField.Type != ParamType::Contents || contentsField.Offset >= Formats::LevelFile::EventParamsSize) {
			return;
		}
		std::size_t length = std::min<std::size_t>(contentsField.Bits / 8, std::size_t(Formats::LevelFile::EventParamsSize - contentsField.Offset));
		std::memcpy(&event.Params[contentsField.Offset], contents.Params, length);
	}

	bool EventCatalog::FitsInContents(const EventParamField& contentsField, const EventParamField& innerField)
	{
		std::size_t length = contentsField.Bits / 8;
		bool isBytes = (innerField.Type == ParamType::String || innerField.Type == ParamType::Raw || innerField.Type == ParamType::Contents);
		std::size_t storage = (isBytes ? std::size_t(innerField.Bits / 8) : std::size_t(GetStorageBytes(innerField)));
		return (std::size_t(innerField.Offset) + storage <= length);
	}

	void EventCatalog::SetContentsType(const EventDescriptor& descriptor, const EventParamField& typeField, Formats::EventRecord& event, std::uint16_t type)
	{
		std::int32_t previous = ReadField(event, typeField);
		WriteField(event, typeField, std::int32_t(type));
		if (previous == std::int32_t(type)) {
			return;
		}
		for (std::uint8_t i = 0; i < descriptor.FieldCount; i++) {
			const EventParamField& field = descriptor.Fields[i];
			if (field.Type == ParamType::Contents && field.ContentsTypeField >= 0 && std::size_t(field.ContentsTypeField) < descriptor.FieldCount &&
				&descriptor.Fields[field.ContentsTypeField] == &typeField) {
				// What a new event of the type has, the parameters of the old one would mean something else
				Formats::EventRecord contents;
				if (type != 0) {
					contents = MakeDefault(EventType(type));
				} else {
					std::memset(&contents, 0, sizeof(contents));
				}
				SetContents(field, event, contents);
			}
		}
	}

	namespace
	{
		// Returns the value a field gets by mirroring
		std::int32_t MirrorValue(const EventDescriptor& descriptor, const EventParamField& field, const FieldMirror& mirror, const Formats::EventRecord& original)
		{
			std::int32_t value = EventCatalog::ReadField(original, field);
			switch (mirror.Mode) {
				case MirrorMode::Negate: {
					// The lowest value of a signed field has no positive counterpart, clamping it would make mirroring
					// twice lose it, so a value whose negation doesn't fit stays as it is
					std::int64_t negated = -std::int64_t(value);
					bool hasRange = (field.Max > field.Min);
					if (negated > INT32_MAX || (hasRange && (negated < field.Min || negated > field.Max))) {
						return value;
					}
					return std::int32_t(negated);
				}
				case MirrorMode::Toggle:
					return (value != 0 ? 0 : 1);
				case MirrorMode::Swap: {
					if (mirror.Pairs == nullptr) {
						return value;
					}
					if (field.Type == ParamType::Flags) {
						// Bits of flags, each one of a pair takes the other's place
						std::int32_t result = value;
						for (std::uint8_t i = 0; i + 1 < mirror.PairCount; i += 2) {
							std::int32_t a = mirror.Pairs[i], b = mirror.Pairs[i + 1];
							result &= ~(a | b);
							result |= ((value & a) != 0 ? b : 0) | ((value & b) != 0 ? a : 0);
						}
						return result;
					}
					for (std::uint8_t i = 0; i + 1 < mirror.PairCount; i += 2) {
						if (value == mirror.Pairs[i]) {
							return mirror.Pairs[i + 1];
						}
						if (value == mirror.Pairs[i + 1]) {
							return mirror.Pairs[i];
						}
					}
					return value;
				}
				case MirrorMode::SwapField:
					if (mirror.Field >= 0 && std::size_t(mirror.Field) < descriptor.FieldCount) {
						return EventCatalog::ReadField(original, descriptor.Fields[mirror.Field]);
					}
					return value;
				default:
					return value;
			}
		}
	}

	bool EventCatalog::MirrorParams(const EventDescriptor* descriptor, std::uint8_t* params, bool horizontal)
	{
		if (descriptor == nullptr || params == nullptr) {
			return false;
		}
		Formats::EventRecord event;
		std::memset(&event, 0, sizeof(event));
		event.Type = std::uint16_t(descriptor->Type);
		std::memcpy(event.Params, params, sizeof(event.Params));
		bool changed = MirrorParams(descriptor, event, horizontal);
		std::memcpy(params, event.Params, sizeof(event.Params));
		return changed;
	}

	bool EventCatalog::MirrorParams(const EventDescriptor* descriptor, Formats::EventRecord& event, bool horizontal)
	{
		if (descriptor == nullptr) {
			return false;
		}
		Formats::EventRecord original = event;
		// The event inside comes out of the crate mirrored as well
		ForEachContents(*descriptor, event, [horizontal](const EventDescriptor& d, Formats::EventRecord& e) {
			// Values are read from the event as it was, so two fields swapped with each other don't swap back
			Formats::EventRecord before = e;
			for (std::uint8_t i = 0; i < d.FieldCount; i++) {
				const EventParamField& field = d.Fields[i];
				if (field.Type == ParamType::Contents) {
					continue;
				}
				const FieldMirror& mirror = (horizontal ? field.MirrorX : field.MirrorY);
				if (mirror.Mode != MirrorMode::None) {
					WriteField(e, field, MirrorValue(d, field, mirror, before));
				}
			}
			return (std::memcmp(before.Params, e.Params, sizeof(e.Params)) != 0);
		});
		return (std::memcmp(original.Params, event.Params, sizeof(event.Params)) != 0);
	}

	bool EventCatalog::CanMirror(const EventDescriptor* descriptor)
	{
		if (descriptor == nullptr) {
			return false;
		}
		for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
			const EventParamField& field = descriptor->Fields[i];
			if (field.MirrorX.Mode != MirrorMode::None || field.MirrorY.Mode != MirrorMode::None || field.Type == ParamType::Contents) {
				return true;
			}
		}
		return false;
	}

	bool EventCatalog::SwapTeamParams(const EventDescriptor* descriptor, std::uint8_t* params, std::int32_t first, std::int32_t second)
	{
		if (descriptor == nullptr || params == nullptr || first == second) {
			return false;
		}
		Formats::EventRecord event;
		std::memset(&event, 0, sizeof(event));
		event.Type = std::uint16_t(descriptor->Type);
		std::memcpy(event.Params, params, sizeof(event.Params));
		bool changed = ForEachContents(*descriptor, event, [first, second](const EventDescriptor& d, Formats::EventRecord& e) {
			bool swapped = false;
			for (std::uint8_t i = 0; i < d.FieldCount; i++) {
				const EventParamField& field = d.Fields[i];
				if (!field.IsTeam || field.Type == ParamType::Contents) {
					continue;
				}
				std::int32_t value = ReadField(e, field);
				if (value == first || value == second) {
					WriteField(e, field, value == first ? second : first);
					swapped = true;
				}
			}
			return swapped;
		});
		std::memcpy(params, event.Params, sizeof(event.Params));
		return changed;
	}
}

#endif
