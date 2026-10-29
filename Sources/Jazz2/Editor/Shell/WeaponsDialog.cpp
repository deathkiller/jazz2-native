#include "WeaponsDialog.h"

#if defined(WITH_EDITOR)

#include "PropertyRows.h"
#include "ShellCommon.h"
#include "WeaponPreview.h"
#include "WeaponScripts.h"
#include "../Document/CommandHistory.h"
#include "../Document/Commands.h"
#include "../Document/LevelDocument.h"
#include "../Events/EventPreviews.h"
#include "../Files/AtomicFile.h"
#include "../Files/EditorSettings.h"
#include "../Imaging/PngCodec.h"
#include "../Imaging/Quantizer.h"
#include "../Tools/EditorContext.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"
#if defined(WITH_AUDIO)
#	include "../../../nCine/Audio/AudioBufferPlayer.h"
#endif

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t SlotCount = std::int32_t(WeaponType::Count);
		constexpr float DegreesPerRadian = 57.29578f;
		constexpr float LabelWidth = 120.0f;
		// Wide enough for the longest values and choices
		constexpr float NumberWidth = 140.0f;
		constexpr float ChoiceWidth = 160.0f;

		enum class WeaponsPage : std::uint8_t
		{
			General,
			Ammo,
			Firing,
			Shot,
			PoweredUp,
			Animations,
			Script
		};

		/** What the dialog shows, and the weapons of the level read again only when they changed */
		struct WeaponsState
		{
			std::int32_t Slot = 0;
			WeaponsPage Page = WeaponsPage::General;
			bool PreviewPowered = false;
			SmallVector<ScriptWeaponClass, 0> ScriptWeapons;
			bool ScriptWeaponsFound = false;
			// Descriptions of the weapons of scripts in "Editor/Weapons", read once
			SmallVector<ScriptWeaponTemplate, 0> Templates;
			bool TemplatesLoaded = false;

			void LoadTemplates()
			{
				if (TemplatesLoaded) {
					return;
				}
				TemplatesLoaded = true;
				auto& resolver = ContentResolver::Get();
				SmallVector<String, 2> directories;
				directories.push_back(fs::CombinePath({ resolver.GetContentPath(), "Editor"_s, "Weapons"_s }));
				StringView userContent = resolver.GetUserContentPath();
				if (!userContent.empty()) {
					directories.push_back(fs::CombinePath({ userContent, "Editor"_s, "Weapons"_s }));
				}
				FindScriptWeaponTemplates(directories, Templates);
			}

			void FindLibraries(IEditorContext& session)
			{
				if (ScriptWeaponsFound) {
					return;
				}
				ScriptWeaponsFound = true;
				SmallVector<String, 2> directories;
				StringView filePath = session.GetFilePath();
				if (!filePath.empty()) {
					directories.push_back(fs::GetDirectoryName(filePath));
				}
				directories.push_back(ContentResolver::Get().GetSourcePath());
				FindScriptWeapons(directories, ScriptWeapons);
			}

			// The description of a weapon of a script, the user content's first, or guessed from its library, `nullptr` if none
			const ScriptWeaponTemplate* FindTemplate(IEditorContext& session, StringView library, StringView constructor)
			{
				if (constructor.empty()) {
					return nullptr;
				}
				LoadTemplates();
				for (std::size_t i = Templates.size(); i > 0; i--) {
					if (IsScriptWeaponTemplateOf(Templates[i - 1], library, constructor)) {
						return &Templates[i - 1];
					}
				}
				for (const Guess& guess : _guesses) {
					if (guess.Constructor == constructor && StringUtils::equalsIgnoreCase(guess.Library, library)) {
						return guess.Template.get();
					}
				}
				Guess& guess = _guesses.emplace_back();
				guess.Library = library;
				guess.Constructor = constructor;
				String path = FindLibraryPath(session, library);
				String text = (path.empty() ? String() : ReadText(path));
				if (!text.empty()) {
					// What the weapons of MLLE pop into may be declared in its include of weapons
					if (!_includesRead) {
						_includesRead = true;
						String path = fs::FindPathCaseInsensitive(fs::CombinePath(ContentResolver::Get().GetSourcePath(), "MLLE-Weapons.asc"_s));
						_mlleWeaponsText = (path.empty() ? String() : ReadText(path));
					}
					const StringView includes[] = { _mlleWeaponsText };
					auto weaponTemplate = std::make_unique<ScriptWeaponTemplate>();
					if (GuessScriptWeaponTemplate(library, text, constructor, *weaponTemplate, includes)) {
						weaponTemplate->SourcePath = path;
						guess.Template = Death::move(weaponTemplate);
					}
				}
				return guess.Template.get();
			}

			// Whether the library of a weapon of a script is where the game looks for it, asked every frame, so remembered
			bool IsLibraryFound(IEditorContext& session, StringView library)
			{
				for (const auto& known : _libraries) {
					if (StringUtils::equalsIgnoreCase(known.first, library)) {
						return known.second;
					}
				}
				bool found = !FindLibraryPath(session, library).empty();
				_libraries.emplace_back(String(library), found);
				return found;
			}

			const ScriptWeaponTemplate* FindCurrentTemplate(IEditorContext& session)
			{
				const WeaponDefinition& weapon = GetCurrent(session);
				return (weapon.Kind == WeaponKind::Script ? FindTemplate(session, weapon.ScriptLibrary, weapon.ScriptConstructor) : nullptr);
			}

			// The text of the script of the level, see GetHandWrittenSetUp()
			StringView GetScriptText(IEditorContext& session)
			{
				GetHandWrittenSetUp(session, 0);
				return _scriptText;
			}

			~WeaponsState()
			{
				StopSound();
				if (_soundMetadata != nullptr) {
					ContentResolver::Get().PinMetadata(_soundMetadata, false);
				}
				for (const String& path : _images) {
					ContentResolver::Get().ReleaseWeaponImage(path);
				}
			}

			// The sound of firing heard as the game plays it, from the first metadata that has it, false if it can't be played
			bool PlaySound(ArrayView<const StringView> metadataPaths, StringView name, bool loop)
			{
				StopSound();
#if defined(WITH_AUDIO)
				float gain = PreferencesCache::MasterVolume * PreferencesCache::SfxVolume;
				if (name.empty() || gain <= 0.0f) {
					return false;
				}
				ContentResolver& resolver = ContentResolver::Get();
				for (StringView path : metadataPaths) {
					Metadata* metadata = (path.empty() ? nullptr : resolver.RequestMetadata(path));
					if (metadata == nullptr) {
						continue;
					}
					auto it = metadata->Sounds.find(String::nullTerminatedView(name));
					if (it == metadata->Sounds.end() || !resolver.ResolveSound(it->second) || it->second.Buffers.empty()) {
						continue;
					}
					// Kept loaded while it's heard, loading the level to play it would release the samples
					if (_soundMetadata != metadata) {
						resolver.PinMetadata(metadata, true);
						if (_soundMetadata != nullptr) {
							resolver.PinMetadata(_soundMetadata, false);
						}
						_soundMetadata = metadata;
					}
					_sound = std::make_shared<nCine::AudioBufferPlayer>(&it->second.Buffers[0]->Buffer);
					_sound->setSourceRelative(true);
					_sound->setPosition(nCine::Vector3f::Zero);
					_sound->setGain(gain);
					_sound->setLooping(loop);
					_sound->play();
					return true;
				}
#endif
				return false;
			}

			bool IsSoundPlaying() const
			{
#if defined(WITH_AUDIO)
				return (_sound != nullptr && _sound->isPlaying());
#else
				return false;
#endif
			}

			void StopSound()
			{
#if defined(WITH_AUDIO)
				if (_sound != nullptr) {
					_sound->stop();
					_sound = nullptr;
				}
#endif
			}

			const WeaponsDescription& Get(IEditorContext& session)
			{
				const LevelDocument& document = session.GetDocument();
				const Formats::ChunkRecord* chunk = document.FindChunk(Formats::WeaponsChunk::Tag);
				std::size_t size = (chunk != nullptr ? chunk->Data.size() : 0);
				std::uint32_t revision = session.GetHistory().GetRevision();
				if (revision != _revision || size != _size || &document != _document) {
					_revision = revision;
					_size = size;
					_document = &document;
					_result = ReadLevelWeapons(document, _weapons);
					SyncImages();
				}
				return _weapons;
			}

			const WeaponDefinition& GetCurrent(IEditorContext& session)
			{
				return Get(session).Weapons[std::clamp<std::int32_t>(Slot, 0, SlotCount - 1)];
			}

			// What the values of the weapon are compared with to tell they were changed, the weapon of the game, or the weapon of
			// the game a custom one was made from
			const WeaponDefinition& GetBaseline(IEditorContext& session)
			{
				const WeaponDefinition& current = GetCurrent(session);
				WeaponType slot = WeaponType(std::clamp<std::int32_t>(Slot, 0, SlotCount - 1));
				WeaponType basedOn = (current.BasedOn != WeaponType::Unknown ? current.BasedOn : slot);
				if (!_hasBaseline || _baselineSlot != slot || _baselineKind != current.Kind || _baselineBasedOn != basedOn) {
					_baseline = (current.Kind == WeaponKind::Custom ? WeaponDefinition::MakeCustom(slot, basedOn) : WeaponDefinition::MakeDefault(slot));
					_baselineSlot = slot;
					_baselineKind = current.Kind;
					_baselineBasedOn = basedOn;
					_hasBaseline = true;
				}
				return _baseline;
			}

			// What the script of the level sets a weapon up with by itself, outside of the lines of MLLE, empty for nothing
			StringView GetHandWrittenSetUp(IEditorContext& session, std::int32_t slot)
			{
				StringView filePath = session.GetFilePath();
				if (!_scriptScanned || _scriptFilePath != filePath) {
					_scriptScanned = true;
					_scriptFilePath = filePath;
					String scriptPath;
					if (!filePath.empty()) {
						scriptPath = fs::CombinePath(fs::GetDirectoryName(filePath), String(fs::GetFileNameWithoutExtension(filePath) + ".j2as"_s));
					}
					if ((scriptPath.empty() || !fs::IsReadableFile(scriptPath)) && !session.GetLevelName().empty()) {
						scriptPath = ContentResolver::Get().FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(session.GetLevelName()) + ".j2as"_s)));
					}
					String text;
					if (!scriptPath.empty() && fs::IsReadableFile(scriptPath)) {
						auto s = fs::Open(scriptPath, FileAccess::Read);
						std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
						if (size > 0 && size <= 16 * 1024 * 1024) {
							text = String{NoInit, std::size_t(size)};
							if (s->Read(text.data(), size) != size) {
								text = {};
							}
						}
					}
					FindHandWrittenScriptWeapons(text, _handWritten);
					_scriptText = Death::move(text);
				}
				return (slot >= 0 && slot < MaxScriptWeapons ? StringView(_handWritten[slot]) : StringView());
			}

			bool IsReadOnly() const
			{
				return (_result != Formats::WeaponsChunk::ReadResult::Success);
			}

			Formats::WeaponsChunk::ReadResult GetResult() const
			{
				return _result;
			}

		private:
			std::uint32_t _revision = UINT32_MAX;
			std::size_t _size = 0;
			const LevelDocument* _document = nullptr;
			WeaponsDescription _weapons;
			Formats::WeaponsChunk::ReadResult _result = Formats::WeaponsChunk::ReadResult::Success;
			WeaponDefinition _baseline;
			WeaponType _baselineSlot = WeaponType::Unknown;
			WeaponKind _baselineKind = WeaponKind::Stock;
			WeaponType _baselineBasedOn = WeaponType::Unknown;
			bool _hasBaseline = false;
			String _scriptFilePath;
			String _handWritten[MaxScriptWeapons];
			String _scriptText;
			bool _scriptScanned = false;

			// Weapons of scripts no description has, guessed from their libraries once, `nullptr` if they can't be
			struct Guess
			{
				String Library;
				String Constructor;
				std::unique_ptr<ScriptWeaponTemplate> Template;
			};
			SmallVector<Guess, 0> _guesses;
			String _mlleWeaponsText;
			bool _includesRead = false;
			SmallVector<std::pair<String, bool>, 0> _libraries;

			static String FindLibraryPath(IEditorContext& session, StringView library)
			{
				if (library.empty()) {
					return {};
				}
				// Where the libraries are looked for in the list, the game reads them from the files of the original game
				SmallVector<String, 2> directories;
				StringView filePath = session.GetFilePath();
				if (!filePath.empty()) {
					directories.push_back(fs::GetDirectoryName(filePath));
				}
				directories.push_back(ContentResolver::Get().GetSourcePath());
				for (const String& directory : directories) {
					String path = fs::FindPathCaseInsensitive(fs::CombinePath(directory, library));
					if (!path.empty() && fs::IsReadableFile(path)) {
						return path;
					}
				}
				return {};
			}

			static String ReadText(StringView path)
			{
				auto s = fs::Open(path, FileAccess::Read);
				std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
				if (size <= 0 || size > 16 * 1024 * 1024) {
					return {};
				}
				String text{NoInit, std::size_t(size)};
				return (s->Read(text.data(), size) == size ? text : String());
			}
			// The images are available to draw while the dialog is open, see ContentResolver::AcquireWeaponImage()
			SmallVector<String, 4> _images;
#if defined(WITH_AUDIO)
			std::shared_ptr<nCine::AudioBufferPlayer> _sound;
#endif
			Metadata* _soundMetadata = nullptr;

			void SyncImages()
			{
				SmallVector<String, 4> paths;
				for (const WeaponImage& image : _weapons.Images) {
					if (Metadata* metadata = ContentResolver::Get().AcquireWeaponImage(image)) {
						paths.push_back(metadata->Path);
					}
				}
				for (const String& path : _images) {
					ContentResolver::Get().ReleaseWeaponImage(path);
				}
				_images = Death::move(paths);
			}
		};

		using StatePtr = std::shared_ptr<WeaponsState>;

		void Change(IEditorContext& session, const StatePtr& state, Function<void(WeaponDefinition&)>&& change, bool final = true)
		{
			std::int32_t slot = state->Slot;
			// TRANSLATORS: Name of an undo step in the history, a change in the Weapons dialog
			ChangeLevelWeapons(session, _x("editor", "Weapons"), [slot, &change](WeaponsDescription& weapons) {
				change(weapons.Weapons[slot]);
			}, final);
		}

		const WeaponShotOptions& GetShot(const WeaponDefinition& weapon, bool powered)
		{
			return (powered ? weapon.ShotPowered : weapon.Shot);
		}

		WeaponShotOptions& GetShot(WeaponDefinition& weapon, bool powered)
		{
			return (powered ? weapon.ShotPowered : weapon.Shot);
		}

		UiDropdown* AddChoice(UiElement* row, ArrayView<const PropertyChoice> choices, Function<std::int32_t()>&& getter, Function<void(std::int32_t)>&& setter)
		{
			UiDropdown* dropdown = AddChoiceDropdown(row, choices, Death::move(getter), Death::move(setter));
			dropdown->Width = ChoiceWidth;
			return dropdown;
		}

		// Shown after a value while it isn't as the weapon of the game has it, see WeaponsState::GetBaseline()
		void AddChangedMark(UiStack* row, Function<bool()>&& isChanged)
		{
			// TRANSLATORS: Next to a value in the Weapons dialog that differs from the weapon of the game
			auto* mark = row->Add<UiLabel>(_x("editor", "(changed)"), UiTheme::TextDim);
			mark->IsVisibleFunc = Death::move(isChanged);
		}

		UiNumberBox* AddNumber(UiElement* form, IEditorContext& session, const StatePtr& state, StringView label, StringView tooltip,
			float (*get)(const WeaponDefinition&), void (*set)(WeaponDefinition&, float), float minValue, float maxValue, float step, std::int32_t decimals)
		{
			IEditorContext* s = &session;
			auto* row = AddLabeledRow(form, label, tooltip);
			auto* box = row->Add<UiNumberBox>([s, state, get]() {
				return get(state->GetCurrent(*s));
			}, [s, state, set](float value, bool final) {
				Change(*s, state, [set, value](WeaponDefinition& weapon) { set(weapon, value); }, final);
			}, minValue, maxValue, step, decimals);
			box->Width = NumberWidth;
			box->Tooltip = tooltip;
			AddChangedMark(row, [s, state, get]() { return get(state->GetCurrent(*s)) != get(state->GetBaseline(*s)); });
			return box;
		}

		UiNumberBox* AddShotNumber(UiElement* form, IEditorContext& session, const StatePtr& state, bool powered, StringView label, StringView tooltip,
			float (*get)(const WeaponShotOptions&), void (*set)(WeaponShotOptions&, float), float minValue, float maxValue, float step, std::int32_t decimals)
		{
			IEditorContext* s = &session;
			auto* row = AddLabeledRow(form, label, tooltip);
			auto* box = row->Add<UiNumberBox>([s, state, powered, get]() {
				return get(GetShot(state->GetCurrent(*s), powered));
			}, [s, state, powered, set](float value, bool final) {
				Change(*s, state, [powered, set, value](WeaponDefinition& weapon) { set(GetShot(weapon, powered), value); }, final);
			}, minValue, maxValue, step, decimals);
			box->Width = NumberWidth;
			box->Tooltip = tooltip;
			AddChangedMark(row, [s, state, powered, get]() {
				return get(GetShot(state->GetCurrent(*s), powered)) != get(GetShot(state->GetBaseline(*s), powered));
			});
			return box;
		}

		UiCheckBox* AddCheck(UiElement* form, IEditorContext& session, const StatePtr& state, StringView label, StringView tooltip,
			bool (*get)(const WeaponDefinition&), void (*set)(WeaponDefinition&, bool))
		{
			IEditorContext* s = &session;
			auto* row = form->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			auto* box = row->Add<UiCheckBox>(label, [s, state, get]() {
				return get(state->GetCurrent(*s));
			}, [s, state, set](bool value) {
				Change(*s, state, [set, value](WeaponDefinition& weapon) { set(weapon, value); });
			});
			box->Tooltip = tooltip;
			AddChangedMark(row, [s, state, get]() { return get(state->GetCurrent(*s)) != get(state->GetBaseline(*s)); });
			return box;
		}

		UiCheckBox* AddShotCheck(UiElement* form, IEditorContext& session, const StatePtr& state, bool powered, StringView label, StringView tooltip,
			bool (*get)(const WeaponShotOptions&), void (*set)(WeaponShotOptions&, bool))
		{
			IEditorContext* s = &session;
			auto* row = form->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			auto* box = row->Add<UiCheckBox>(label, [s, state, powered, get]() {
				return get(GetShot(state->GetCurrent(*s), powered));
			}, [s, state, powered, set](bool value) {
				Change(*s, state, [powered, set, value](WeaponDefinition& weapon) { set(GetShot(weapon, powered), value); });
			});
			box->Tooltip = tooltip;
			AddChangedMark(row, [s, state, powered, get]() {
				return get(GetShot(state->GetCurrent(*s), powered)) != get(GetShot(state->GetBaseline(*s), powered));
			});
			return box;
		}

		ArrayView<const PropertyChoice> GetStockChoices()
		{
			static const PropertyChoice Choices[] = {
				{ "Blaster", std::int32_t(WeaponType::Blaster) },
				{ "Bouncer", std::int32_t(WeaponType::Bouncer) },
				{ "Freezer", std::int32_t(WeaponType::Freezer) },
				{ "Seeker", std::int32_t(WeaponType::Seeker) },
				{ "RF", std::int32_t(WeaponType::RF) },
				{ "Toaster", std::int32_t(WeaponType::Toaster) },
				{ "TNT", std::int32_t(WeaponType::TNT) },
				{ "Pepper", std::int32_t(WeaponType::Pepper) },
				{ "Electro", std::int32_t(WeaponType::Electro) },
				{ "Thunderbolt", std::int32_t(WeaponType::Thunderbolt) },
			};
			return Choices;
		}

		// A custom weapon made from a weapon of the game keeps what the level set of it besides its shots
		void MakeCustomFrom(WeaponDefinition& weapon, WeaponType slot, WeaponType basedOn)
		{
			WeaponDefinition custom = WeaponDefinition::MakeCustom(slot, basedOn);
			custom.Name = weapon.Name;
			custom.Options = weapon.Options;
			custom.Options.Style = WeaponDefinition::MakeCustom(slot, basedOn).Options.Style;
			custom.StartAmmo = weapon.StartAmmo;
			custom.AmmoCrateEvent = weapon.AmmoCrateEvent;
			custom.HudIcon = weapon.HudIcon;
			custom.HudIconPowered = weapon.HudIconPowered;
			custom.Pickup = weapon.Pickup;
			custom.UnknownProperties = weapon.UnknownProperties;
			weapon = Death::move(custom);
		}

		// What's shown dimmed after the name of a weapon in the list, empty for a weapon of the game as it is
		StringView GetSlotSuffix(const WeaponsDescription& weapons, std::int32_t slot, bool setUpByScript)
		{
			const WeaponDefinition& weapon = weapons.Weapons[slot];
			// TRANSLATORS: After the name of a weapon in the list of the Weapons dialog, a weapon the script of the level sets up, as MLLE does or by its own code
			if ((setUpByScript && weapon.Kind != WeaponKind::Custom) || weapon.Kind == WeaponKind::Script) {
				return _x("editor", "(script)");
			}
			switch (weapon.Kind) {
				// TRANSLATORS: After the name of a weapon in the list of the Weapons dialog, a weapon the level describes itself
				case WeaponKind::Custom: return _x("editor", "(custom)");
				default:
					// TRANSLATORS: After the name of a weapon in the list of the Weapons dialog, a weapon of the game whose options the level changes
					return (weapon.IsDefault(WeaponType(slot)) ? StringView() : _x("editor", "(changed)"));
			}
		}

		// The ammo the script of the level gives a weapon it sets up, as Show Overridden Assets shows it, `nullptr` for none
		const GraphicResource* FindScriptAmmo(IEditorContext& session, const StatePtr& state, std::int32_t slot)
		{
			// The blaster has no ammo to pick up
			EventPreviews& previews = session.GetEventPreviews();
			if (!previews.ScriptSprite || slot == 0 || (state->Get(session).Weapons[slot].Kind != WeaponKind::Script &&
				state->GetHandWrittenSetUp(session, slot).empty())) {
				return nullptr;
			}
			Formats::EventRecord event = {};
			event.Type = std::uint16_t(EventType::Ammo);
			event.Params[0] = std::uint8_t(slot);
			return previews.ScriptSprite(event, true, true);
		}

		// The icon of a weapon on the screen in the game, the level's own, the one of the script of the level or the game's,
		// with the blaster of the character the level is played with, `nullptr` if there's none to draw
		const GraphicResource* FindWeaponIcon(IEditorContext& session, const StatePtr& state, std::int32_t slot, bool powered)
		{
			const WeaponsDescription& weapons = state->Get(session);
			const WeaponDefinition& weapon = weapons.Weapons[slot];
			const WeaponSprite& icon = (powered && !weapon.HudIconPowered.IsEmpty() ? weapon.HudIconPowered : weapon.HudIcon);
			if (!icon.IsEmpty()) {
				if (const GraphicResource* res = FindWeaponAnimation(weapons.GetSpriteMetadataPath(icon), icon.AnimState)) {
					return res;
				}
			}
			// A weapon of a script shows what its hook draws on the screen, or the ammo the script gives it
			EventPreviews& previews = session.GetEventPreviews();
			if (previews.ScriptWeaponIcon && weapon.Kind == WeaponKind::Script) {
				if (const GraphicResource* res = previews.ScriptWeaponIcon(slot, powered)) {
					return res;
				}
			}
			if (const GraphicResource* res = FindScriptAmmo(session, state, slot)) {
				return res;
			}
			// The states of the icons in the metadata of the screen, the blasters of Jazz, Spaz and Lori first, see HUD
			std::uint32_t character = std::uint32_t(std::clamp<std::int32_t>(session.GetSettings().PlayCharacter - std::int32_t(PlayerType::Jazz), 0, 2));
			std::uint32_t animState = (slot == 0 ? character : std::uint32_t(slot + 2));
			return FindWeaponAnimation("UI/HUD"_s, powered ? animState + 20 : animState);
		}

		// Shortened with dots at the end if it's wider
		String FitText(UiPainter& painter, StringView text, float width)
		{
			if (painter.MeasureText(text) <= width) {
				return text;
			}
			StringView dots = "..."_s;
			float available = width - painter.MeasureText(dots);
			std::size_t length = 0;
			while (length < text.size()) {
				std::size_t next = Utf8::NextChar(text, length).second();
				if (painter.MeasureText(text.prefix(next)) > available) {
					break;
				}
				length = next;
			}
			return text.prefix(length).trimmedSuffix() + dots;
		}

		/** A weapon in the list, its icon, its name and what the level makes of it */
		class WeaponSlotButton : public UiButton
		{
		public:
			WeaponSlotButton(IEditorContext& session, const StatePtr& state, std::int32_t slot)
				: UiButton(StringView(), EditorIcon::None), _session(session), _state(state), _slot(slot)
			{
				OnClick = [state, slot]() { state->Slot = slot; };
				IsCheckedFunc = [state, slot]() { return state->Slot == slot; };
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(std::min(context.Px(150.0f), available.X), context.Px(UiTheme::ButtonHeight + 6.0f));
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				return _tooltip;
			}

			void Draw(UiPainter& painter) override
			{
				// The background and the border of a button, with no text of its own
				UiButton::Draw(painter);

				const WeaponsDescription& weapons = _state->Get(_session);
				float padding = painter.Px(4.0f);
				Rectf iconRect(Bounds.X + padding, Bounds.Y + painter.Px(2.0f), painter.Px(26.0f), Bounds.H - painter.Px(4.0f));
				painter.PushClip(Bounds);
				if (const GraphicResource* res = FindWeaponIcon(_session, _state, _slot, false)) {
					DrawFittedFrame(painter, *res, res->GetFrameForTime(_session.GetAnimTime()), iconRect);
				}

				// What the level makes of it is shortened first, then the name, the whole of both is the tooltip then
				String name = FormatInteger(_slot + 1) + ". "_s + GetLevelWeaponName(weapons, WeaponType(_slot));
				StringView suffix = GetSlotSuffix(weapons, _slot, !_state->GetHandWrittenSetUp(_session, _slot).empty());
				float x = iconRect.X + iconRect.W + padding;
				Rectf textRect(x, Bounds.Y, std::max(Bounds.X + Bounds.W - x - padding, 0.0f), Bounds.H);
				float gap = painter.Px(3.0f);
				float nameWidth = painter.MeasureText(name);
				String shownSuffix;
				if (!suffix.empty()) {
					float suffixRoom = textRect.W - nameWidth - gap;
					// Shortened to less than a letter and the dots, it tells nothing anymore
					if (suffixRoom >= painter.MeasureText("(s..."_s)) {
						shownSuffix = FitText(painter, suffix, suffixRoom);
					}
				}
				String shownName = (shownSuffix.empty() ? FitText(painter, name, textRect.W) : String(name));
				_tooltip = (shownName != name || shownSuffix != suffix ? (suffix.empty() ? name : name + " "_s + suffix) : String());
				painter.TextInRect(shownName, textRect, IsChecked() ? UiTheme::TextOnAccent : UiTheme::Text);
				if (!shownSuffix.empty()) {
					float suffixX = textRect.X + nameWidth + gap;
					painter.TextInRect(shownSuffix, Rectf(suffixX, textRect.Y, std::max(textRect.X + textRect.W - suffixX, 0.0f), textRect.H), UiTheme::TextDim);
				}
				painter.PopClip();
			}

		private:
			IEditorContext& _session;
			StatePtr _state;
			std::int32_t _slot;
			String _tooltip;
		};

		void AddGeneralPage(UiForm* form, IEditorContext& session, const StatePtr& state)
		{
			IEditorContext* s = &session;
			{
				static const PropertyChoice KindChoices[] = {
					// TRANSLATORS: Choice of what a weapon is in the Weapons dialog, the weapon of the game
					{ _x("editor", "Weapon of the game"), std::int32_t(WeaponKind::Stock) },
					// TRANSLATORS: Choice of what a weapon is in the Weapons dialog, a weapon the level describes, which the game fires itself
					{ _x("editor", "Custom weapon"), std::int32_t(WeaponKind::Custom) },
					// TRANSLATORS: Choice of what a weapon is in the Weapons dialog, a weapon the script of the level sets up (JJ2+)
					{ _x("editor", "Script-driven weapon"), std::int32_t(WeaponKind::Script) },
				};
				// TRANSLATORS: Label in the Weapons dialog, what the weapon is: of the game, custom or of the script
				auto* row = AddLabeledRow(form, _x("editor", "Type"));
				auto* dropdown = AddChoiceDropdown(row, KindChoices, [s, state]() {
					return std::int32_t(state->GetCurrent(*s).Kind);
				}, [s, state](std::int32_t value) {
					WeaponType slot = WeaponType(state->Slot);
					Change(*s, state, [slot, value](WeaponDefinition& weapon) {
						WeaponKind kind = WeaponKind(value);
						if (kind == weapon.Kind) {
							return;
						}
						if (kind == WeaponKind::Custom) {
							MakeCustomFrom(weapon, slot, slot);
						} else {
							WeaponDefinition stock = WeaponDefinition::MakeDefault(slot);
							stock.Kind = kind;
							stock.Name = weapon.Name;
							stock.Options = weapon.Options;
							stock.StartAmmo = weapon.StartAmmo;
							stock.AmmoCrateEvent = weapon.AmmoCrateEvent;
							stock.HudIcon = weapon.HudIcon;
							stock.HudIconPowered = weapon.HudIconPowered;
							stock.Pickup = weapon.Pickup;
							stock.ScriptLibrary = weapon.ScriptLibrary;
							stock.ScriptConstructor = weapon.ScriptConstructor;
							stock.ScriptParams = weapon.ScriptParams;
							stock.UnknownProperties = weapon.UnknownProperties;
							weapon = Death::move(stock);
						}
					});
				}, {}, [state](std::int32_t value) {
					// MLLE sets up only the first nine weapons
					return (WeaponKind(value) != WeaponKind::Script || state->Slot < MaxScriptWeapons);
				});
				dropdown->Width = 180.0f;
				dropdown->Flex = 1.0f;
			}
			{
				// What the dialog can't show, the script changes it as it runs
				auto* note = form->Add<UiWrappedLabel>(StringView(), UiTheme::TextWarning);
				note->TextFunc = [s, state]() {
					// TRANSLATORS: In the Weapons dialog, the script of the level changes the weapon by its own code, which is shown after it
					return _fx("editor", "The script of the level also changes it by itself: {}", state->GetHandWrittenSetUp(*s, state->Slot));
				};
				note->IsVisibleFunc = [s, state]() { return !state->GetHandWrittenSetUp(*s, state->Slot).empty(); };
				note->Tooltip = _x("editor", "Its look and how it fires in the game come from the script then, they aren't shown here");
			}
			{
				// TRANSLATORS: Label of the name of a weapon in the Weapons dialog
				auto* row = AddLabeledRow(form, _x("editor", "Name"));
				auto* box = row->Add<UiTextBox>();
				box->Width = 160.0f;
				box->Flex = 1.0f;
				box->MaxLength = 64;
				box->PlaceholderFunc = [state]() { return String(GetStockWeaponName(WeaponType(state->Slot))); };
				box->TextFunc = [s, state]() { return state->GetCurrent(*s).Name; };
				box->OnCommit = [s, state](StringView text) {
					String name = text.trimmed();
					Change(*s, state, [&name](WeaponDefinition& weapon) { weapon.Name = name; });
				};
				box->Tooltip = _x("editor", "Shown when the weapon is taken in hand, the name of the weapon of the game if it's empty");
				AddChangedMark(row, [s, state]() { return !state->GetCurrent(*s).Name.empty(); });
			}
			{
				// TRANSLATORS: Label in the Weapons dialog, the weapon of the game a custom weapon was made from
				auto* row = AddLabeledRow(form, _x("editor", "Made from"));
				auto* dropdown = AddChoiceDropdown(row, GetStockChoices(), [s, state]() {
					const WeaponDefinition& weapon = state->GetCurrent(*s);
					return std::int32_t(weapon.BasedOn != WeaponType::Unknown ? weapon.BasedOn : WeaponType(state->Slot));
				}, [s, state](std::int32_t value) {
					WeaponType slot = WeaponType(state->Slot);
					Change(*s, state, [slot, value](WeaponDefinition& weapon) { MakeCustomFrom(weapon, slot, WeaponType(value)); });
				});
				dropdown->Width = 180.0f;
				dropdown->Flex = 1.0f;
				row->Tooltip = _x("editor", "Starts the weapon anew from a weapon of the game, crates and bosses treat its shots as that weapon's");
				ShowRowWhen(row, [s, state]() { return state->GetCurrent(*s).Kind == WeaponKind::Custom; });
			}
			{
				auto* row = form->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				// TRANSLATORS: Button in the Weapons dialog, the weapon becomes the weapon of the game again
				auto* reset = row->Add<UiButton>(_x("editor", "Reset to Default"), EditorIcon::Undo, [s, state]() {
					WeaponType slot = WeaponType(state->Slot);
					Change(*s, state, [slot](WeaponDefinition& weapon) { weapon = WeaponDefinition::MakeDefault(slot); });
				});
				reset->Tooltip = _x("editor", "Forgets all the level changes of this weapon");
			}
		}

		void AddAmmoPage(UiForm* form, IEditorContext& session, const StatePtr& state)
		{
			// TRANSLATORS: Label in the Weapons dialog, the most ammo of the weapon a player can have
			AddNumber(form, session, state, _x("editor", "Maximum ammo"), _x("editor", "The most ammo a player can have, 0 is as in the game (99)"),
				[](const WeaponDefinition& w) { return float(std::max<std::int32_t>(w.Options.Maximum, 0)); },
				[](WeaponDefinition& w, float v) { w.Options.Maximum = (v < 0.5f ? -1 : std::int32_t(std::round(v))); }, 0.0f, 255.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, ammo a player has when the level starts
			AddNumber(form, session, state, _x("editor", "Start ammo"), _x("editor", "Ammo every player has when the level starts"),
				[](const WeaponDefinition& w) { return float(w.StartAmmo); },
				[](WeaponDefinition& w, float v) { w.StartAmmo = std::int32_t(std::round(v)); }, 0.0f, 255.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, how many times as much ammo a pickup gives
			AddNumber(form, session, state, _x("editor", "Multiplier"), _x("editor", "A pickup gives this many times as much ammo, and the ammo shown is divided by it"),
				[](const WeaponDefinition& w) { return float(w.Options.Multiplier); },
				[](WeaponDefinition& w, float v) { w.Options.Multiplier = std::max<std::int32_t>(std::int32_t(std::round(v)), 1); }, 1.0f, 256.0f, 1.0f, 0);
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Infinite ammo"), _x("editor", "The ammo never runs out"),
				[](const WeaponDefinition& w) { return w.Options.Infinite; }, [](WeaponDefinition& w, bool v) { w.Options.Infinite = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Full when the level starts"), _x("editor", "Players start the level with the most ammo of it"),
				[](const WeaponDefinition& w) { return w.Options.Replenishes; }, [](WeaponDefinition& w, bool v) { w.Options.Replenishes = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Allowed"), _x("editor", "Players can have and fire it in the level"),
				[](const WeaponDefinition& w) { return w.Options.Allowed; }, [](WeaponDefinition& w, bool v) { w.Options.Allowed = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Can be powered up"), _x("editor", "A power-up monitor powers it up in the level"),
				[](const WeaponDefinition& w) { return w.Options.AllowedPowerup; }, [](WeaponDefinition& w, bool v) { w.Options.AllowedPowerup = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "From gun crates"), _x("editor", "Crates and barrels of random ammo may give its ammo"),
				[](const WeaponDefinition& w) { return w.Options.ComesFromGunCrates; }, [](WeaponDefinition& w, bool v) { w.Options.ComesFromGunCrates = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog, the bird that helps the player fires this weapon
			AddCheck(form, session, state, _x("editor", "Birds fire it"), _x("editor", "A bird helping the player fires this weapon while the player holds it"),
				[](const WeaponDefinition& w) { return w.Options.ComesFromBirds; }, [](WeaponDefinition& w, bool v) { w.Options.ComesFromBirds = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Birds fire it powered up"), _x("editor", "A bird fires it powered up if the player's is"),
				[](const WeaponDefinition& w) { return w.Options.ComesFromBirdsPowerup; }, [](WeaponDefinition& w, bool v) { w.Options.ComesFromBirdsPowerup = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Shield fires instead"), _x("editor", "While the player has a shield, the shield fires in place of this weapon"),
				[](const WeaponDefinition& w) { return w.Options.ReplacedByShield; }, [](WeaponDefinition& w, bool v) { w.Options.ReplacedByShield = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Bubbles under water"), _x("editor", "Under water it fires air bubbles, which do no harm"),
				[](const WeaponDefinition& w) { return w.Options.ReplacedByBubbles; }, [](WeaponDefinition& w, bool v) { w.Options.ReplacedByBubbles = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Sound of firing"), _x("editor", "Plays the sound of the weapon when it fires"),
				[](const WeaponDefinition& w) { return w.Options.DefaultSample; }, [](WeaponDefinition& w, bool v) { w.Options.DefaultSample = v; });
			// TRANSLATORS: Label in the Weapons dialog, gems a player hit by this weapon loses in Treasure Hunt
			AddNumber(form, session, state, _x("editor", "Gems lost"), _x("editor", "Gems a player hit by a shot loses in Treasure Hunt"),
				[](const WeaponDefinition& w) { return float(w.Options.GemsLost); },
				[](WeaponDefinition& w, float v) { w.Options.GemsLost = std::int32_t(std::round(v)); }, 0.0f, 255.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog
			AddNumber(form, session, state, _x("editor", "Gems lost powered up"), _x("editor", "Gems a player hit by a powered-up shot loses in Treasure Hunt"),
				[](const WeaponDefinition& w) { return float(w.Options.GemsLostPowerup); },
				[](WeaponDefinition& w, float v) { w.Options.GemsLostPowerup = std::int32_t(std::round(v)); }, 0.0f, 255.0f, 1.0f, 0);
			{
				IEditorContext* s = &session;
				static const PropertyChoice StyleChoices[] = {
					// TRANSLATORS: Choice of how often a weapon fires in the Weapons dialog
					{ _x("editor", "While held"), std::int32_t(WeaponStyle::Normal) },
					// TRANSLATORS: Choice of how often a weapon fires in the Weapons dialog, once per press of the button
					{ _x("editor", "Once per press"), std::int32_t(WeaponStyle::Missile) },
					// TRANSLATORS: Choice of how often a weapon fires in the Weapons dialog, never slower than this even with fast fire
					{ _x("editor", "While held, not slower"), std::int32_t(WeaponStyle::Popcorn) },
					// TRANSLATORS: Choice of how often a weapon fires in the Weapons dialog, tapping fire doesn't fire faster
					{ _x("editor", "While held, tapping not faster"), std::int32_t(WeaponStyle::Capped) },
				};
				// TRANSLATORS: Label in the Weapons dialog, how often the weapon fires
				auto* row = AddLabeledRow(form, _x("editor", "Fires"));
				AddChoice(row, StyleChoices, [s, state]() {
					return std::int32_t(state->GetCurrent(*s).Options.Style);
				}, [s, state](std::int32_t value) {
					Change(*s, state, [value](WeaponDefinition& weapon) { weapon.Options.Style = WeaponStyle(value); });
				});
				row->Tooltip = _x("editor", "A weapon of the game keeps how it fires, but tapping a capped one isn't faster");
				AddChangedMark(row, [s, state]() { return state->GetCurrent(*s).Options.Style != state->GetBaseline(*s).Options.Style; });
			}
		}

		void AddFiringPage(UiForm* form, IEditorContext& session, const StatePtr& state)
		{
			IEditorContext* s = &session;
			// TRANSLATORS: Label in the Weapons dialog, frames between two shots
			AddNumber(form, session, state, _x("editor", "Time between"), _x("editor", "Frames between two shots, at 60 a second"),
				[](const WeaponDefinition& w) { return w.Fire.Cooldown; }, [](WeaponDefinition& w, float v) { w.Fire.Cooldown = v; }, 1.0f, 600.0f, 1.0f, 1);
			// TRANSLATORS: Label in the Weapons dialog, frames each fast fire pickup takes from the time between shots
			AddNumber(form, session, state, _x("editor", "Fast fire"), _x("editor", "Frames each fast fire takes from the time between shots"),
				[](const WeaponDefinition& w) { return w.Fire.FastFireStep; }, [](WeaponDefinition& w, float v) { w.Fire.FastFireStep = v; }, 0.0f, 60.0f, 0.1f, 2);
			// TRANSLATORS: Label in the Weapons dialog, ammo one shot takes
			AddNumber(form, session, state, _x("editor", "Ammo a shot"), _x("editor", "Ammo one shot takes, 1 is one round"),
				[](const WeaponDefinition& w) { return float(w.Fire.AmmoPerShot) / 256.0f; },
				[](WeaponDefinition& w, float v) { w.Fire.AmmoPerShot = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(v * 256.0f)), 0, 65535)); },
				0.0f, 255.0f, 0.05f, 2);
			// TRANSLATORS: Label in the Weapons dialog, shots fired at once
			AddNumber(form, session, state, _x("editor", "Shots at once"), _x("editor", "Shots fired at once, spread over the angle below"),
				[](const WeaponDefinition& w) { return float(w.Fire.ShotsPerFire); },
				[](WeaponDefinition& w, float v) { w.Fire.ShotsPerFire = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(v)), 1, 32)); }, 1.0f, 32.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, the angle the shots fired at once are spread over
			AddNumber(form, session, state, _x("editor", "Spread angle"), _x("editor", "Angle the shots fired at once are spread over, in degrees"),
				[](const WeaponDefinition& w) { return w.Fire.SpreadAngle * DegreesPerRadian; },
				[](WeaponDefinition& w, float v) { w.Fire.SpreadAngle = v / DegreesPerRadian; }, 0.0f, 360.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, a random angle added to each shot
			AddNumber(form, session, state, _x("editor", "Random angle"), _x("editor", "Up to this many degrees are added to each shot at random"),
				[](const WeaponDefinition& w) { return w.Fire.RandomSpread * DegreesPerRadian; },
				[](WeaponDefinition& w, float v) { w.Fire.RandomSpread = v / DegreesPerRadian; }, 0.0f, 180.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, how hard firing pushes the player back
			AddNumber(form, session, state, _x("editor", "Recoil"), _x("editor", "How hard firing pushes the player back"),
				[](const WeaponDefinition& w) { return w.Fire.Recoil; }, [](WeaponDefinition& w, float v) { w.Fire.Recoil = v; }, -32.0f, 32.0f, 0.1f, 1);
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddCheck(form, session, state, _x("editor", "Fires under water"), _x("editor", "It can be fired under water"),
				[](const WeaponDefinition& w) { return w.Fire.AllowedInWater; }, [](WeaponDefinition& w, bool v) { w.Fire.AllowedInWater = v; });
			// TRANSLATORS: Checkbox in the Weapons dialog, the muzzle of the gun flashes
			AddCheck(form, session, state, _x("editor", "Muzzle flash"), _x("editor", "The muzzle flashes when it fires"),
				[](const WeaponDefinition& w) { return w.Fire.Flare; }, [](WeaponDefinition& w, bool v) { w.Fire.Flare = v; });
			{
				// TRANSLATORS: Label in the Weapons dialog, the name of the sound played when the weapon fires
				auto* row = AddLabeledRow(form, _x("editor", "Sound"));
				auto* box = row->Add<UiTextBox>();
				box->Width = 140.0f;
				box->Flex = 1.0f;
				box->MaxLength = 64;
				// TRANSLATORS: Placeholder of the empty box of the sound of a weapon in the Weapons dialog
				box->Placeholder = _x("editor", "none");
				box->TextFunc = [s, state]() { return state->GetCurrent(*s).Fire.Sound; };
				box->OnCommit = [s, state](StringView text) {
					String sound = text.trimmed();
					Change(*s, state, [&sound](WeaponDefinition& weapon) { weapon.Fire.Sound = sound; });
				};
				box->Tooltip = _x("editor", "Name of a sound of the shot's metadata or of the player's, such as \"Fire\" or \"WeaponBlaster\"");
				auto* play = row->Add<UiButton>(StringView(), EditorIcon::Play, [s, state]() {
					const WeaponsDescription& weapons = state->Get(*s);
					const WeaponDefinition& weapon = weapons.Weapons[state->Slot];
					WeaponType basedOn = (weapon.BasedOn != WeaponType::Unknown ? weapon.BasedOn : WeaponType(state->Slot));
					// As the shot plays it, from its own metadata, or the player
					String shotPath = weapons.GetSpriteMetadataPath(weapon.Shot.Sprite);
					String stockPath = WeaponDefinition::MakeCustom(WeaponType(state->Slot), basedOn).Shot.Sprite.Metadata;
					const StringView paths[] = { shotPath, stockPath, "Interactive/PlayerJazz"_s };
					if (!state->PlaySound(paths, weapon.Fire.Sound, weapon.Fire.LoopSound)) {
						s->ShowMessage(_x("editor", "The sound can't be played, it isn't in the metadata of the shot or the player, or sounds are muted"));
					}
				});
				// TRANSLATORS: Tooltip of the button next to the sound of a weapon in the Weapons dialog
				play->Tooltip = _x("editor", "Plays the sound as the game does");
				play->IsVisibleFunc = [state]() { return !state->IsSoundPlaying(); };
				auto* stop = row->Add<UiButton>(StringView(), EditorIcon::Stop, [state]() { state->StopSound(); });
				stop->Tooltip = _x("editor", "Stops the sound");
				stop->IsVisibleFunc = [state]() { return state->IsSoundPlaying(); };
				AddChangedMark(row, [s, state]() { return state->GetCurrent(*s).Fire.Sound != state->GetBaseline(*s).Fire.Sound; });
			}
			// TRANSLATORS: Checkbox in the Weapons dialog, the sound plays again and again while the weapon fires
			AddCheck(form, session, state, _x("editor", "Sound in a loop"), _x("editor", "The sound plays in a loop while the weapon fires, it must be the player's"),
				[](const WeaponDefinition& w) { return w.Fire.LoopSound; }, [](WeaponDefinition& w, bool v) { w.Fire.LoopSound = v; });
		}

		void AddShotPage(UiForm* form, IEditorContext& session, const StatePtr& state, bool powered)
		{
			IEditorContext* s = &session;
			if (powered) {
				auto* row = form->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				// TRANSLATORS: Button in the Weapons dialog, the powered-up shot becomes the same as the usual one
				auto* same = row->Add<UiButton>(_x("editor", "Same as the Usual Shot"), EditorIcon::Copy, [s, state]() {
					Change(*s, state, [](WeaponDefinition& weapon) { weapon.ShotPowered = weapon.Shot; });
				});
				same->Tooltip = _x("editor", "What the powered-up shot doesn't change is as the usual one");
			}
			// TRANSLATORS: Label in the Weapons dialog, how fast the shot flies
			AddShotNumber(form, session, state, powered, _x("editor", "Speed"), _x("editor", "Pixels a frame"),
				[](const WeaponShotOptions& o) { return o.Speed; }, [](WeaponShotOptions& o, float v) { o.Speed = v; }, -16.0f, 16.0f, 0.1f, 2);
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddShotCheck(form, session, state, powered, _x("editor", "Adds the player's speed"), _x("editor", "A running player fires the shot faster"),
				[](const WeaponShotOptions& o) { return o.InheritPlayerSpeed; }, [](WeaponShotOptions& o, bool v) { o.InheritPlayerSpeed = v; });
			// TRANSLATORS: Label in the Weapons dialog, how much the shot falls each frame
			AddShotNumber(form, session, state, powered, _x("editor", "Gravity"), _x("editor", "Speed added downwards each frame, below 0 it rises"),
				[](const WeaponShotOptions& o) { return o.Gravity; }, [](WeaponShotOptions& o, float v) { o.Gravity = v; }, -4.0f, 4.0f, 0.01f, 2);
			// TRANSLATORS: Label in the Weapons dialog, how long the shot lasts
			AddShotNumber(form, session, state, powered, _x("editor", "Lasts"), _x("editor", "Frames the shot lasts, at 60 a second"),
				[](const WeaponShotOptions& o) { return o.Lifetime; }, [](WeaponShotOptions& o, float v) { o.Lifetime = v; }, 1.0f, 3600.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, the damage of a shot
			AddShotNumber(form, session, state, powered, _x("editor", "Damage"), _x("editor", "Health a shot takes from an enemy"),
				[](const WeaponShotOptions& o) { return float(o.Strength); }, [](WeaponShotOptions& o, float v) { o.Strength = std::int32_t(std::round(v)); }, 0.0f, 100.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, enemies a shot passes through
			AddShotNumber(form, session, state, powered, _x("editor", "Passes through"), _x("editor", "Enemies the shot passes through before it stops"),
				[](const WeaponShotOptions& o) { return float(o.Pierce); }, [](WeaponShotOptions& o, float v) { o.Pierce = std::int32_t(std::round(v)); }, 0.0f, 255.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, walls a shot bounces off
			AddShotNumber(form, session, state, powered, _x("editor", "Bounces"), _x("editor", "Walls, floors and ceilings the shot bounces off before it stops"),
				[](const WeaponShotOptions& o) { return float(o.Bounces); }, [](WeaponShotOptions& o, float v) { o.Bounces = std::int32_t(std::round(v)); }, 0.0f, 255.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, the speed kept after a bounce
			AddShotNumber(form, session, state, powered, _x("editor", "Bounce speed"), _x("editor", "Part of the speed kept after a bounce, 1 keeps all of it"),
				[](const WeaponShotOptions& o) { return o.Elasticity; }, [](WeaponShotOptions& o, float v) { o.Elasticity = v; }, 0.0f, 2.0f, 0.05f, 2);
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddShotCheck(form, session, state, powered, _x("editor", "Flies through walls"), _x("editor", "Walls don't stop it, it can still break blocks"),
				[](const WeaponShotOptions& o) { return o.PassesWalls; }, [](WeaponShotOptions& o, bool v) { o.PassesWalls = v; });
			// TRANSLATORS: Label in the Weapons dialog, how fast the shot turns towards an enemy, in degrees each frame
			AddShotNumber(form, session, state, powered, _x("editor", "Homing turn"), _x("editor", "Degrees the shot turns towards an enemy each frame, 0 never"),
				[](const WeaponShotOptions& o) { return o.HomingTurnRate * DegreesPerRadian; }, [](WeaponShotOptions& o, float v) { o.HomingTurnRate = v / DegreesPerRadian; },
				0.0f, 57.0f, 0.5f, 1);
			// TRANSLATORS: Label in the Weapons dialog, how far a homing shot sees an enemy
			AddShotNumber(form, session, state, powered, _x("editor", "Homing range"), _x("editor", "Pixels the shot sees an enemy to turn to from"),
				[](const WeaponShotOptions& o) { return o.HomingRange; }, [](WeaponShotOptions& o, float v) { o.HomingRange = v; }, 0.0f, 1024.0f, 4.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, how long the shot freezes what it hits
			AddShotNumber(form, session, state, powered, _x("editor", "Freezes for"), _x("editor", "Frames it freezes what it hits for, 0 doesn't freeze"),
				[](const WeaponShotOptions& o) { return o.FreezeDuration; }, [](WeaponShotOptions& o, float v) { o.FreezeDuration = v; }, 0.0f, 3600.0f, 1.0f, 0);
			// TRANSLATORS: Checkbox in the Weapons dialog
			AddShotCheck(form, session, state, powered, _x("editor", "Thaws"), _x("editor", "It thaws what's frozen, as fire does"),
				[](const WeaponShotOptions& o) { return o.Thaws; }, [](WeaponShotOptions& o, bool v) { o.Thaws = v; });
			{
				// TRANSLATORS: Label in the Weapons dialog, the weapon of the game whose destructible blocks the shot breaks
				auto* row = AddLabeledRow(form, _x("editor", "Breaks blocks of"));
				AddChoice(row, GetStockChoices(), [s, state, powered]() {
					WeaponType type = GetShot(state->GetCurrent(*s), powered).DestroysBlocksAs;
					return std::int32_t(type != WeaponType::Unknown ? type : WeaponType(state->Slot));
				}, [s, state, powered](std::int32_t value) {
					Change(*s, state, [powered, value](WeaponDefinition& weapon) { GetShot(weapon, powered).DestroysBlocksAs = WeaponType(value); });
				});
				row->Tooltip = _x("editor", "Destructible blocks of this weapon of the game break by the shot");
				AddChangedMark(row, [s, state, powered]() {
					return GetShot(state->GetCurrent(*s), powered).DestroysBlocksAs != GetShot(state->GetBaseline(*s), powered).DestroysBlocksAs;
				});
			}
			{
				static const PropertyChoice TriggerChoices[] = {
					// TRANSLATORS: Choice of when a shot goes off in the Weapons dialog, when it hits something
					{ _x("editor", "When it hits"), std::int32_t(ShotTrigger::Contact) },
					// TRANSLATORS: Choice of when a shot goes off in the Weapons dialog, when an enemy comes close, like a mine
					{ _x("editor", "When an enemy is close"), std::int32_t(ShotTrigger::Proximity) },
					// TRANSLATORS: Choice of when a shot goes off in the Weapons dialog, when its time runs out
					{ _x("editor", "When its time is up"), std::int32_t(ShotTrigger::Timer) },
				};
				// TRANSLATORS: Label in the Weapons dialog, when the shot goes off
				auto* row = AddLabeledRow(form, _x("editor", "Goes off"));
				AddChoice(row, TriggerChoices, [s, state, powered]() {
					return std::int32_t(GetShot(state->GetCurrent(*s), powered).Trigger);
				}, [s, state, powered](std::int32_t value) {
					Change(*s, state, [powered, value](WeaponDefinition& weapon) { GetShot(weapon, powered).Trigger = ShotTrigger(value); });
				});
				row->Tooltip = _x("editor", "With a speed of 0, a shot that goes off by an enemy close is a mine");
				AddChangedMark(row, [s, state, powered]() {
					return GetShot(state->GetCurrent(*s), powered).Trigger != GetShot(state->GetBaseline(*s), powered).Trigger;
				});
			}
			// TRANSLATORS: Label in the Weapons dialog, how close an enemy has to come for a mine to go off
			AddShotNumber(form, session, state, powered, _x("editor", "Trigger distance"), _x("editor", "Pixels an enemy has to come to for the shot to go off"),
				[](const WeaponShotOptions& o) { return o.TriggerRadius; }, [](WeaponShotOptions& o, float v) { o.TriggerRadius = v; }, 0.0f, 512.0f, 2.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, the radius of the blast where the shot goes off
			AddShotNumber(form, session, state, powered, _x("editor", "Blast radius"), _x("editor", "Pixels around where it goes off that it hurts and breaks blocks, 0 for none"),
				[](const WeaponShotOptions& o) { return o.AreaRadius; }, [](WeaponShotOptions& o, float v) { o.AreaRadius = v; }, 0.0f, 512.0f, 2.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, the damage of the blast
			AddShotNumber(form, session, state, powered, _x("editor", "Blast damage"), _x("editor", "Health the blast takes from each enemy in it"),
				[](const WeaponShotOptions& o) { return float(o.AreaStrength); }, [](WeaponShotOptions& o, float v) { o.AreaStrength = std::int32_t(std::round(v)); }, 0.0f, 100.0f, 1.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, how hard the blast pushes players away
			AddShotNumber(form, session, state, powered, _x("editor", "Blast push"), _x("editor", "How hard the blast pushes players in it away"),
				[](const WeaponShotOptions& o) { return o.Knockback; }, [](WeaponShotOptions& o, float v) { o.Knockback = v; }, -64.0f, 64.0f, 0.5f, 1);
			{
				static const PropertyChoice ExplosionChoices[] = {
					// TRANSLATORS: Choice of the explosion of a shot in the Weapons dialog, no explosion
					{ _x("editor", "None"), -1 },
					// Kinds of explosions of the game aren't translated
					{ "Tiny", 0 }, { "Tiny blue", 1 }, { "Tiny dark", 2 }, { "Small", 3 }, { "Small dark", 4 }, { "Large", 5 },
					{ "Brown smoke", 6 }, { "Gray smoke", 7 }, { "White smoke", 8 }, { "Poof", 9 }, { "Splash", 10 },
					{ "Pepper", 11 }, { "RF", 12 }, { "RF powered up", 13 }, { "Ice", 15 },
				};
				// TRANSLATORS: Label in the Weapons dialog, the explosion shown where the shot goes off
				auto* row = AddLabeledRow(form, _x("editor", "Explosion"));
				AddChoice(row, ExplosionChoices, [s, state, powered]() {
					return GetShot(state->GetCurrent(*s), powered).Explosion;
				}, [s, state, powered](std::int32_t value) {
					Change(*s, state, [powered, value](WeaponDefinition& weapon) { GetShot(weapon, powered).Explosion = value; });
				});
				AddChangedMark(row, [s, state, powered]() {
					return GetShot(state->GetCurrent(*s), powered).Explosion != GetShot(state->GetBaseline(*s), powered).Explosion;
				});
			}
			// TRANSLATORS: Label in the Weapons dialog, the radius of the light of the shot
			AddShotNumber(form, session, state, powered, _x("editor", "Light"), _x("editor", "Radius of its light in pixels, 0 for none"),
				[](const WeaponShotOptions& o) { return o.LightRadius; }, [](WeaponShotOptions& o, float v) { o.LightRadius = v; }, 0.0f, 512.0f, 2.0f, 0);
			// TRANSLATORS: Label in the Weapons dialog, how bright the light of the shot is
			AddShotNumber(form, session, state, powered, _x("editor", "Light intensity"), _x("editor", "From 0 to 1"),
				[](const WeaponShotOptions& o) { return o.LightIntensity; }, [](WeaponShotOptions& o, float v) { o.LightIntensity = v; }, 0.0f, 1.0f, 0.05f, 2);
			// TRANSLATORS: Checkbox in the Weapons dialog, the sprite of the shot turns the way it flies
			AddShotCheck(form, session, state, powered, _x("editor", "Turns as it flies"), _x("editor", "The sprite turns the way the shot flies"),
				[](const WeaponShotOptions& o) { return o.RotatesWithSpeed; }, [](WeaponShotOptions& o, bool v) { o.RotatesWithSpeed = v; });
			{
				static const PropertyChoice LookChoices[] = {
					// TRANSLATORS: Choice of how a shot is drawn in the Weapons dialog, its sprite as it is
					{ _x("editor", "Sprite"), std::int32_t(ShotLook::Sprite) },
					// TRANSLATORS: Choice of how a shot is drawn in the Weapons dialog, its sprite added to what's behind, as the pepper spray
					{ _x("editor", "Glowing sprite"), std::int32_t(ShotLook::Glow) },
					// TRANSLATORS: Choice of how a shot is drawn in the Weapons dialog, sparks of its sprite left behind it, as the electro blaster
					{ _x("editor", "Swirl of sparks"), std::int32_t(ShotLook::Electro) },
					// TRANSLATORS: Choice of how a shot is drawn in the Weapons dialog, a beam from the gun, as the thunderbolt
					{ _x("editor", "Beam"), std::int32_t(ShotLook::Beam) },
				};
				// TRANSLATORS: Label in the Weapons dialog, how the shot is drawn
				auto* row = AddLabeledRow(form, _x("editor", "Look"));
				AddChoice(row, LookChoices, [s, state, powered]() {
					return std::int32_t(GetShot(state->GetCurrent(*s), powered).Look);
				}, [s, state, powered](std::int32_t value) {
					Change(*s, state, [powered, value](WeaponDefinition& weapon) { GetShot(weapon, powered).Look = ShotLook(value); });
				});
				row->Tooltip = _x("editor", "A swirl of sparks hides the shot and leaves frames of its sprite behind it, a beam doesn't fly and reaches as far as the shot would fly in its time");
				AddChangedMark(row, [s, state, powered]() {
					return GetShot(state->GetCurrent(*s), powered).Look != GetShot(state->GetBaseline(*s), powered).Look;
				});
			}
			{
				static const PropertyChoice FlipChoices[] = {
					// TRANSLATORS: Choices of whether the sprite of a shot is flipped in the Weapons dialog
					{ _x("editor", "Never"), std::int32_t(ShotFlip::Normal) },
					{ _x("editor", "Always"), std::int32_t(ShotFlip::Flipped) },
					{ _x("editor", "Randomly"), std::int32_t(ShotFlip::Random) },
				};
				auto addFlip = [form, s, &state, powered](StringView label, StringView tooltip, bool vertical) {
					auto* row = AddLabeledRow(form, label, tooltip);
					AddChoice(row, FlipChoices, [s, state, powered, vertical]() {
						const WeaponShotOptions& shot = GetShot(state->GetCurrent(*s), powered);
						return std::int32_t(vertical ? shot.FlipY : shot.FlipX);
					}, [s, state, powered, vertical](std::int32_t value) {
						Change(*s, state, [powered, vertical, value](WeaponDefinition& weapon) {
							(vertical ? GetShot(weapon, powered).FlipY : GetShot(weapon, powered).FlipX) = ShotFlip(value);
						});
					});
					AddChangedMark(row, [s, state, powered, vertical]() {
						const WeaponShotOptions& shot = GetShot(state->GetCurrent(*s), powered);
						const WeaponShotOptions& baseline = GetShot(state->GetBaseline(*s), powered);
						return (vertical ? shot.FlipY != baseline.FlipY : shot.FlipX != baseline.FlipX);
					});
				};
				// TRANSLATORS: Label in the Weapons dialog, whether the sprite of the shot is flipped horizontally
				addFlip(_x("editor", "Flip horizontally"), _x("editor", "The sprite shows its other side to where the shot flies"), false);
				// TRANSLATORS: Label in the Weapons dialog, whether the sprite of the shot is flipped upside down
				addFlip(_x("editor", "Flip vertically"), _x("editor", "The sprite is upside down, as half of the bolts of the thunderbolt"), true);
			}
		}

		// Sprites the Animations page edits
		enum class SpriteSlot : std::uint8_t { Shot, ShotAlt, ShotPowered, ShotPoweredAlt, Icon, IconPowered, Pickup };

		// A second sprite of a shot is none when it's empty, the others are as in the game
		bool IsAltSprite(SpriteSlot slot)
		{
			return (slot == SpriteSlot::ShotAlt || slot == SpriteSlot::ShotPoweredAlt);
		}

		WeaponSprite& GetSprite(WeaponDefinition& weapon, SpriteSlot slot)
		{
			switch (slot) {
				case SpriteSlot::Shot: return weapon.Shot.Sprite;
				case SpriteSlot::ShotAlt: return weapon.Shot.AltSprite;
				case SpriteSlot::ShotPowered: return weapon.ShotPowered.Sprite;
				case SpriteSlot::ShotPoweredAlt: return weapon.ShotPowered.AltSprite;
				case SpriteSlot::Icon: return weapon.HudIcon;
				case SpriteSlot::IconPowered: return weapon.HudIconPowered;
				default: return weapon.Pickup;
			}
		}

		const WeaponSprite& GetSprite(const WeaponDefinition& weapon, SpriteSlot slot)
		{
			return GetSprite(const_cast<WeaponDefinition&>(weapon), slot);
		}

		// The animation the game shows for a sprite, the level's own or the game's
		const GraphicResource* FindSpriteAnimation(IEditorContext& session, const StatePtr& state, SpriteSlot spriteSlot, bool& additive)
		{
			const WeaponsDescription& weapons = state->Get(session);
			std::int32_t slot = state->Slot;
			const WeaponDefinition& weapon = weapons.Weapons[slot];
			const WeaponSprite& sprite = GetSprite(weapon, spriteSlot);
			switch (spriteSlot) {
				case SpriteSlot::Shot:
				case SpriteSlot::ShotAlt:
				case SpriteSlot::ShotPowered:
				case SpriteSlot::ShotPoweredAlt: {
					bool powered = (spriteSlot == SpriteSlot::ShotPowered || spriteSlot == SpriteSlot::ShotPoweredAlt);
					additive = ((powered ? weapon.ShotPowered : weapon.Shot).Look == ShotLook::Glow);
					if (!sprite.IsEmpty()) {
						return FindWeaponAnimation(weapons.GetSpriteMetadataPath(sprite), sprite.AnimState);
					}
					if (IsAltSprite(spriteSlot)) {
						return nullptr;
					}
					WeaponType basedOn = (weapon.BasedOn != WeaponType::Unknown ? weapon.BasedOn : WeaponType(slot));
					WeaponDefinition stock = WeaponDefinition::MakeCustom(WeaponType(slot), basedOn);
					const WeaponSprite& stockSprite = (powered ? stock.ShotPowered.Sprite : stock.Shot.Sprite);
					return FindWeaponAnimation(stockSprite.Metadata, stockSprite.AnimState);
				}
				case SpriteSlot::Icon:
					return FindWeaponIcon(session, state, slot, false);
				case SpriteSlot::IconPowered:
					return FindWeaponIcon(session, state, slot, true);
				default:
					if (!sprite.IsEmpty()) {
						return FindWeaponAnimation(weapons.GetSpriteMetadataPath(sprite), sprite.AnimState);
					}
					if (const GraphicResource* res = FindScriptAmmo(session, state, slot)) {
						return res;
					}
					// The blaster has no ammo to pick up
					return (slot == 0 ? nullptr : FindWeaponAnimation(String("Collectible/Ammo"_s + GetStockWeaponName(WeaponType(slot))), 0));
			}
		}

		UiStack* AddSpriteRow(UiForm* form, IEditorContext& session, const StatePtr& state, StringView label, StringView tooltip, SpriteSlot slot)
		{
			IEditorContext* s = &session;
			auto* row = AddLabeledRow(form, label, tooltip);
			auto* source = row->Add<UiDropdown>([s, state, slot]() {
				const WeaponSprite& sprite = GetSprite(state->GetCurrent(*s), slot);
				if (sprite.Image >= 0) {
					// TRANSLATORS: In the Weapons dialog, a sprite that is an image of the level, followed by its number
					return String(_x("editor", "Embedded image") + " "_s + String(FormatInteger(sprite.Image + 1)));
				}
				if (sprite.Metadata.empty()) {
					// TRANSLATORS: In the Weapons dialog, no second sprite of a shot, or the sprite as the game has it
					return String(IsAltSprite(slot) ? _x("editor", "None") : _x("editor", "As in the game"));
				}
				// TRANSLATORS: In the Weapons dialog, a sprite that is an animation of the game
				return String(_x("editor", "Animation"));
			}, [s, state, slot](SmallVectorImpl<UiMenuEntry>& entries) {
				UiMenuEntry stock;
				stock.Label = (IsAltSprite(slot) ? _x("editor", "None") : _x("editor", "As in the game"));
				stock.IsChecked = [s, state, slot]() { return GetSprite(state->GetCurrent(*s), slot).IsEmpty(); };
				stock.Execute = [s, state, slot]() {
					Change(*s, state, [slot](WeaponDefinition& weapon) { GetSprite(weapon, slot) = WeaponSprite(); });
				};
				entries.push_back(Death::move(stock));
				UiMenuEntry animation;
				// TRANSLATORS: Choice in the Weapons dialog, an animation of the game given by its metadata
				animation.Label = _x("editor", "Animation of the game");
				animation.IsChecked = [s, state, slot]() { return !GetSprite(state->GetCurrent(*s), slot).Metadata.empty(); };
				animation.Execute = [s, state, slot]() {
					Change(*s, state, [slot](WeaponDefinition& weapon) {
						WeaponSprite& sprite = GetSprite(weapon, slot);
						sprite.Image = -1;
						if (sprite.Metadata.empty()) {
							sprite.Metadata = "Weapon/Blaster"_s;
						}
					});
				};
				entries.push_back(Death::move(animation));
				const WeaponsDescription& weapons = state->Get(*s);
				for (std::int32_t i = 0; i < std::int32_t(weapons.Images.size()); i++) {
					UiMenuEntry image;
					image.Label = _x("editor", "Embedded image") + " "_s + String(FormatInteger(i + 1));
					image.IsChecked = [s, state, slot, i]() { return GetSprite(state->GetCurrent(*s), slot).Image == i; };
					image.Execute = [s, state, slot, i]() {
						Change(*s, state, [slot, i](WeaponDefinition& weapon) {
							WeaponSprite& sprite = GetSprite(weapon, slot);
							sprite.Image = i;
							sprite.Metadata = {};
							sprite.AnimState = 0;
						});
					};
					entries.push_back(Death::move(image));
				}
			});
			source->Width = 110.0f;

			auto* path = row->Add<UiTextBox>();
			path->Width = 110.0f;
			path->Flex = 1.0f;
			path->MaxLength = 128;
			// TRANSLATORS: Placeholder of the box of the metadata of an animation in the Weapons dialog
			path->Placeholder = _x("editor", "metadata");
			path->IsVisibleFunc = [s, state, slot]() { return !GetSprite(state->GetCurrent(*s), slot).Metadata.empty(); };
			path->TextFunc = [s, state, slot]() { return GetSprite(state->GetCurrent(*s), slot).Metadata; };
			path->OnCommit = [s, state, slot](StringView text) {
				String metadata = text.trimmed();
				if (metadata.empty()) {
					return;
				}
				Change(*s, state, [slot, &metadata](WeaponDefinition& weapon) { GetSprite(weapon, slot).Metadata = metadata; });
			};
			path->Tooltip = _x("editor", "Metadata of the animation, such as \"Weapon/Seeker\"");
			auto* animState = row->Add<UiNumberBox>([s, state, slot]() {
				return float(GetSprite(state->GetCurrent(*s), slot).AnimState);
			}, [s, state, slot](float value, bool final) {
				Change(*s, state, [slot, value](WeaponDefinition& weapon) { GetSprite(weapon, slot).AnimState = std::uint32_t(std::max(value, 0.0f)); }, final);
			}, 0.0f, 1048576.0f, 1.0f, 0);
			animState->Width = 100.0f;
			animState->IsVisibleFunc = [s, state, slot]() { return !GetSprite(state->GetCurrent(*s), slot).Metadata.empty(); };
			animState->Tooltip = _x("editor", "Animation state in the metadata");
			auto* view = row->Add<WeaponSpriteView>(session, [s, state, slot](bool& additive) {
				return FindSpriteAnimation(*s, state, slot, additive);
			});
			// TRANSLATORS: Tooltip of an animation shown in the Weapons dialog
			view->Tooltip = _x("editor", "As it's shown in the game");
			AddChangedMark(row, [s, state, slot]() {
				const WeaponSprite& sprite = GetSprite(state->GetCurrent(*s), slot);
				return (!sprite.IsEmpty() && sprite != GetSprite(state->GetBaseline(*s), slot));
			});
			return row;
		}

		void ImportImage(IEditorContext& session, const StatePtr& state, StringView path)
		{
			PngImage png;
			String error;
			if (!PngCodec::Load(path, png, &error)) {
				session.ShowMessage(error);
				return;
			}
			// Frames side by side, square if the strip allows it
			std::int32_t frameCount = (png.Height > 0 && png.Width % png.Height == 0 ? png.Width / png.Height : 1);
			frameCount = std::clamp<std::int32_t>(frameCount, 1, Formats::WeaponsChunk::MaxFrameCount);
			std::int32_t frameWidth = png.Width / frameCount;
			std::int32_t frameHeight = png.Height;
			if (frameWidth < 1 || frameWidth > Formats::WeaponsChunk::MaxFrameSize || frameHeight < 1 || frameHeight > Formats::WeaponsChunk::MaxFrameSize) {
				session.ShowMessage(_fx("editor", "A frame can be at most {0} × {0} pixels, frames are side by side", Formats::WeaponsChunk::MaxFrameSize));
				return;
			}

			// Into the colors of the sprites, an indexed image of the same colors keeps its indices
			const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
			bool samePalette = png.Indexed;
			for (std::int32_t i = 1; samePalette && i < png.PaletteSize; i++) {
				samePalette = ((png.Palette[i] & 0x00FFFFFFu) == (palette[i] & 0x00FFFFFFu) || (png.Palette[i] >> 24) == 0);
			}
			std::unique_ptr<PaletteMapper> mapper = std::make_unique<PaletteMapper>(palette, false);

			WeaponImage image;
			image.FrameWidth = frameWidth;
			image.FrameHeight = frameHeight;
			image.FrameCount = frameCount;
			image.HotspotX = frameWidth / 2;
			image.HotspotY = frameHeight / 2;
			image.Pixels.resize(std::size_t(frameWidth) * frameHeight * frameCount);
			for (std::int32_t f = 0; f < frameCount; f++) {
				for (std::int32_t y = 0; y < frameHeight; y++) {
					for (std::int32_t x = 0; x < frameWidth; x++) {
						std::int32_t px = f * frameWidth + x;
						std::uint32_t color = png.GetColor(px, y);
						std::uint8_t index = 0;
						if ((color >> 24) >= 128) {
							index = (samePalette ? png.Pixels[std::size_t(y) * png.Width + px] : mapper->Map(color & 0x00FFFFFFu, x, y));
						}
						image.Pixels[(std::size_t(f) * frameHeight + y) * frameWidth + x] = index;
					}
				}
			}

			// TRANSLATORS: Name of an undo step in the history, an image imported for the weapons of the level
			bool changed = ChangeLevelWeapons(session, _x("editor", "Import weapon image"), [&image](WeaponsDescription& weapons) {
				if (weapons.Images.size() < Formats::WeaponsChunk::MaxImages) {
					weapons.Images.push_back(Death::move(image));
				}
			});
			if (changed) {
				session.ShowMessage(_fnx("editor", "{} frame was imported", "{} frames were imported", frameCount, frameCount));
			}
		}

		void ExportImage(IEditorContext& session, const WeaponImage& image)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			session.GetImageRoots(true, roots);
			if (roots.empty()) {
				session.ShowMessage(_x("editor", "There is no place to save files to"));
				return;
			}
			auto copy = std::make_shared<WeaponImage>(image);
			IEditorContext* s = &session;
			// TRANSLATORS: Title of the file dialog saving an image of the weapons of the level
			UiFileDialog::Show(*surface, _x("editor", "Export Weapon Image"), UiFileDialogMode::Save, Death::move(roots), ".png"_s, {}, "weapon.png"_s,
				[s, copy](StringView path) {
				PngImage png;
				png.Create(copy->FrameWidth * copy->FrameCount, copy->FrameHeight, true);
				std::memcpy(png.Palette, ContentResolver::Get().GetPalettes().data(), sizeof(png.Palette));
				png.Palette[0] &= 0x00FFFFFFu;
				png.PaletteSize = 256;
				for (std::int32_t f = 0; f < copy->FrameCount; f++) {
					for (std::int32_t y = 0; y < copy->FrameHeight; y++) {
						std::memcpy(&png.Pixels[std::size_t(y) * png.Width + std::size_t(f) * copy->FrameWidth],
							&copy->Pixels[(std::size_t(f) * copy->FrameHeight + y) * copy->FrameWidth], copy->FrameWidth);
					}
				}
				if (!AtomicFile::Write(path, [&png](Stream& stream) { return nCine::PngCodec::Encode(stream, png); })) {
					s->ShowMessage(_x("editor", "The file couldn't be written"));
				}
			});
		}

		void AddAnimationsPage(UiForm* form, IEditorContext& session, const StatePtr& state)
		{
			IEditorContext* s = &session;
			auto isCustom = [s, state]() { return state->GetCurrent(*s).Kind == WeaponKind::Custom; };
			// TRANSLATORS: Label in the Weapons dialog, the sprite of the shot
			ShowRowWhen(AddSpriteRow(form, session, state, _x("editor", "Shot"), _x("editor", "Sprite of the shot"), SpriteSlot::Shot), isCustom);
			// TRANSLATORS: Label in the Weapons dialog, the second sprite of the shot, one of the two is chosen at random
			ShowRowWhen(AddSpriteRow(form, session, state, _x("editor", "Shot 2"),
				_x("editor", "Each shot takes this sprite or the first one at random, as the thunderbolt has two bolts"), SpriteSlot::ShotAlt), isCustom);
			// TRANSLATORS: Label in the Weapons dialog, the sprite of the powered-up shot
			ShowRowWhen(AddSpriteRow(form, session, state, _x("editor", "Powered-up shot"), _x("editor", "Sprite of the powered-up shot"), SpriteSlot::ShotPowered), isCustom);
			// TRANSLATORS: Label in the Weapons dialog, the second sprite of the powered-up shot, one of the two is chosen at random
			ShowRowWhen(AddSpriteRow(form, session, state, _x("editor", "Powered-up shot 2"),
				_x("editor", "Each powered-up shot takes this sprite or the first one at random"), SpriteSlot::ShotPoweredAlt), isCustom);
			// TRANSLATORS: Label in the Weapons dialog, the icon of the weapon on the screen
			AddSpriteRow(form, session, state, _x("editor", "Icon"), _x("editor", "Icon on the screen while the weapon is in hand, and in the weapon wheel"), SpriteSlot::Icon);
			// TRANSLATORS: Label in the Weapons dialog, the icon of the powered-up weapon on the screen
			AddSpriteRow(form, session, state, _x("editor", "Powered-up icon"), _x("editor", "Icon while the weapon is powered up, the usual one if it's as in the game"), SpriteSlot::IconPowered);
			// TRANSLATORS: Label in the Weapons dialog, the sprite of the ammo pickup of the weapon
			AddSpriteRow(form, session, state, _x("editor", "Ammo"), _x("editor", "Sprite of the ammo of the weapon placed in the level"), SpriteSlot::Pickup);

			form->Add<UiSeparator>();
			// TRANSLATORS: Header of the images of the weapons in the Weapons dialog
			form->Add<UiLabel>(_x("editor", "Embedded assets"), UiTheme::TextHeader);
			form->Add<UiWrappedLabel>(_x("editor", "PNG strips, frames side by side, in the colors of the sprites"), UiTheme::TextDim);
			for (std::int32_t i = 0; i < std::int32_t(Formats::WeaponsChunk::MaxImages); i++) {
				auto* row = form->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				row->IsVisibleFunc = [s, state, i]() { return i < std::int32_t(state->Get(*s).Images.size()); };
				row->Add<WeaponSpriteView>(session, [s, state, i](bool& additive) -> const GraphicResource* {
					const auto& images = state->Get(*s).Images;
					return (i < std::int32_t(images.size()) ? FindWeaponAnimation(WeaponsDescription::GetImageMetadataPath(images[i]), 0) : nullptr);
				});
				auto* label = row->Add<UiLabel>(StringView(), UiTheme::Text);
				label->TextFunc = [s, state, i]() {
					const auto& images = state->Get(*s).Images;
					if (i >= std::int32_t(images.size())) {
						return String();
					}
					const WeaponImage& image = images[i];
					return _fx("editor", "{}. {} × {}, {} frames", i + 1, image.FrameWidth, image.FrameHeight, image.FrameCount);
				};
				label->Flex = 1.0f;
				auto* hotspotX = row->Add<UiNumberBox>([s, state, i]() {
					const auto& images = state->Get(*s).Images;
					return (i < std::int32_t(images.size()) ? float(images[i].HotspotX) : 0.0f);
				}, [s, i](float value, bool final) {
					ChangeLevelWeapons(*s, _x("editor", "Weapons"), [i, value](WeaponsDescription& weapons) {
						if (i < std::int32_t(weapons.Images.size())) {
							weapons.Images[i].HotspotX = std::int32_t(std::round(value));
						}
					}, final);
				}, -256.0f, 512.0f, 1.0f, 0);
				hotspotX->Width = 88.0f;
				hotspotX->Tooltip = _x("editor", "Hotspot X, the point of a frame at the position of the shot");
				auto* hotspotY = row->Add<UiNumberBox>([s, state, i]() {
					const auto& images = state->Get(*s).Images;
					return (i < std::int32_t(images.size()) ? float(images[i].HotspotY) : 0.0f);
				}, [s, i](float value, bool final) {
					ChangeLevelWeapons(*s, _x("editor", "Weapons"), [i, value](WeaponsDescription& weapons) {
						if (i < std::int32_t(weapons.Images.size())) {
							weapons.Images[i].HotspotY = std::int32_t(std::round(value));
						}
					}, final);
				}, -256.0f, 512.0f, 1.0f, 0);
				hotspotY->Width = 88.0f;
				hotspotY->Tooltip = _x("editor", "Hotspot Y, the point of a frame at the position of the shot");
				auto* rate = row->Add<UiNumberBox>([s, state, i]() {
					const auto& images = state->Get(*s).Images;
					return (i < std::int32_t(images.size()) ? images[i].FrameRate : 0.0f);
				}, [s, i](float value, bool final) {
					ChangeLevelWeapons(*s, _x("editor", "Weapons"), [i, value](WeaponsDescription& weapons) {
						if (i < std::int32_t(weapons.Images.size())) {
							weapons.Images[i].FrameRate = value;
						}
					}, final);
				}, 0.0f, 120.0f, 0.5f, 1);
				rate->Width = 88.0f;
				rate->Tooltip = _x("editor", "Speed of the animation, as the metadata of the game have it");
				auto* exportButton = row->Add<UiButton>(StringView(), EditorIcon::Export, [s, state, i]() {
					const auto& images = state->Get(*s).Images;
					if (i < std::int32_t(images.size())) {
						ExportImage(*s, images[i]);
					}
				});
				exportButton->Tooltip = _x("editor", "Saves the image as a PNG strip");
				auto* remove = row->Add<UiButton>(StringView(), EditorIcon::Delete, [s, i]() {
					// TRANSLATORS: Name of an undo step in the history, an image of the weapons of the level removed
					ChangeLevelWeapons(*s, _x("editor", "Remove weapon image"), [i](WeaponsDescription& weapons) {
						if (i >= std::int32_t(weapons.Images.size())) {
							return;
						}
						weapons.Images.erase(weapons.Images.begin() + i);
						// What used it is as in the game again, what used a later one moves with it
						for (WeaponDefinition& weapon : weapons.Weapons) {
							for (SpriteSlot slot : { SpriteSlot::Shot, SpriteSlot::ShotAlt, SpriteSlot::ShotPowered, SpriteSlot::ShotPoweredAlt,
								SpriteSlot::Icon, SpriteSlot::IconPowered, SpriteSlot::Pickup }) {
								WeaponSprite& sprite = GetSprite(weapon, slot);
								if (sprite.Image == i) {
									sprite = WeaponSprite();
								} else if (sprite.Image > i) {
									sprite.Image--;
								}
							}
						}
					});
				});
				remove->Tooltip = _x("editor", "Removes the image, what uses it looks as in the game again");
			}
			{
				auto* row = form->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				// TRANSLATORS: Button in the Weapons dialog, reads a PNG strip as a new image of the weapons
				row->Add<UiButton>(_x("editor", "Import Image"), EditorIcon::Import, [s, state]() {
					// TRANSLATORS: Title of the file dialog choosing a PNG strip for the weapons of the level
					ShowOpenImageDialog(*s, _x("editor", "Import Weapon Image"), [s, state](StringView path) { ImportImage(*s, state, path); });
				})->Tooltip = _x("editor", "Reads a PNG strip, frames side by side, square ones if the strip is a whole number of squares");
			}
		}

		// A weapon of a script chosen from a library, with the parameters its description or the guess has when it's chosen
		void ChooseScriptWeapon(IEditorContext& session, const StatePtr& state, StringView library, StringView constructor, StringView name)
		{
			SmallVector<std::uint8_t, 0> params;
			if (const ScriptWeaponTemplate* weaponTemplate = state->FindTemplate(session, library, constructor)) {
				MakeDefaultScriptParams(*weaponTemplate, params);
			}
			Change(session, state, [library, constructor, name, &params](WeaponDefinition& weapon) {
				weapon.ScriptLibrary = library;
				weapon.ScriptConstructor = constructor;
				weapon.ScriptParams.assign(params.begin(), params.end());
				if (weapon.Name.empty()) {
					weapon.Name = name;
				}
			});
		}

		/** The parameters of a weapon of a script as its description has them, made again for another description */
		class ScriptParamsForm : public UiForm
		{
		public:
			ScriptParamsForm(IEditorContext& session, const StatePtr& state)
				: _session(session), _state(state)
			{
				MinLabelWidth = LabelWidth;
				MaxLabelWidth = LabelWidth;
			}

			void Update(float timeMult) override
			{
				const ScriptWeaponTemplate* weaponTemplate = _state->FindCurrentTemplate(_session);
				if (!_built || weaponTemplate != _shown) {
					_built = true;
					_shown = weaponTemplate;
					Rebuild();
				}
				UiForm::Update(timeMult);
			}

		private:
			IEditorContext& _session;
			StatePtr _state;
			const ScriptWeaponTemplate* _shown = nullptr;
			bool _built = false;

			// The values of the parameters of the weapon, and how many bytes they take
			static std::size_t ReadValues(IEditorContext& session, const StatePtr& state, const ScriptWeaponTemplate& weaponTemplate,
				SmallVectorImpl<ScriptParamValue>& values)
			{
				const WeaponDefinition& weapon = state->GetCurrent(session);
				return ReadScriptParams(weaponTemplate, weapon.ScriptParams, values);
			}

			static ScriptParamValue GetValue(IEditorContext& session, const StatePtr& state, const ScriptWeaponTemplate* weaponTemplate, std::size_t index)
			{
				SmallVector<ScriptParamValue, 8> values;
				if (weaponTemplate != nullptr) {
					ReadValues(session, state, *weaponTemplate, values);
				}
				return (index < values.size() ? values[index] : ScriptParamValue());
			}

			static void SetValue(IEditorContext& session, const StatePtr& state, const ScriptWeaponTemplate* weaponTemplate, std::size_t index,
				const ScriptParamValue& value, bool final = true)
			{
				if (weaponTemplate == nullptr || index >= weaponTemplate->Params.size()) {
					return;
				}
				SmallVector<ScriptParamValue, 8> values;
				std::size_t used = ReadValues(session, state, *weaponTemplate, values);
				values[index] = value;
				// What the description doesn't tell is kept after it
				const WeaponDefinition& weapon = state->GetCurrent(session);
				ArrayView<const std::uint8_t> rest = ArrayView<const std::uint8_t>(weapon.ScriptParams).exceptPrefix(std::min(used, weapon.ScriptParams.size()));
				SmallVector<std::uint8_t, 0> data;
				WriteScriptParams(*weaponTemplate, values, rest, data);
				Change(session, state, [&data](WeaponDefinition& weapon) { weapon.ScriptParams.assign(data.begin(), data.end()); }, final);
			}

			void Rebuild()
			{
				ClearChildren();
				if (_shown == nullptr) {
					return;
				}
				IEditorContext* s = &_session;
				StatePtr state = _state;
				const ScriptWeaponTemplate* t = _shown;
				for (std::size_t i = 0; i < t->Params.size(); i++) {
					const ScriptWeaponParam& param = t->Params[i];
					// TRANSLATORS: In the Weapons dialog, a parameter of a weapon of a script that has no name, {} is its number
					String label = (param.Name.empty() ? _fx("editor", "Parameter {}", std::int32_t(i + 1)) : String(param.Name));
					auto isChanged = [s, state, t, i]() {
						ScriptParamValue value = GetValue(*s, state, t, i);
						const ScriptWeaponParam& param = t->Params[i];
						return (param.Type == ScriptParamType::String ? value.Text != param.DefaultText : value.Number != param.Default);
					};

					if (param.Type == ScriptParamType::Bool) {
						auto* row = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
						auto* box = row->Add<UiCheckBox>(label, [s, state, t, i]() {
							return GetValue(*s, state, t, i).Number != 0.0;
						}, [s, state, t, i](bool value) {
							ScriptParamValue v;
							v.Number = (value ? 1.0 : 0.0);
							SetValue(*s, state, t, i, v);
						});
						box->Tooltip = param.Description;
						AddChangedMark(row, Death::move(isChanged));
						continue;
					}

					auto* row = AddLabeledRow(this, label, param.Description);
					if (param.Type == ScriptParamType::String) {
						auto* box = row->Add<UiTextBox>();
						box->Width = 160.0f;
						box->Flex = 1.0f;
						box->MaxLength = 255;
						box->TextFunc = [s, state, t, i]() { return GetValue(*s, state, t, i).Text; };
						box->OnCommit = [s, state, t, i](StringView text) {
							ScriptParamValue v;
							v.Text = text;
							SetValue(*s, state, t, i, v);
						};
					} else if (!param.Choices.empty()) {
						auto* dropdown = row->Add<UiDropdown>([s, state, t, i]() {
							double value = GetValue(*s, state, t, i).Number;
							for (const ScriptParamChoice& choice : t->Params[i].Choices) {
								if (choice.Value == value) {
									return String(choice.Name);
								}
							}
							return String(FormatInteger(std::int32_t(value)));
						}, [s, state, t, i](SmallVectorImpl<UiMenuEntry>& entries) {
							for (const ScriptParamChoice& choice : t->Params[i].Choices) {
								double value = choice.Value;
								UiMenuEntry entry;
								entry.Label = choice.Name;
								entry.IsChecked = [s, state, t, i, value]() { return GetValue(*s, state, t, i).Number == value; };
								entry.Execute = [s, state, t, i, value]() {
									ScriptParamValue v;
									v.Number = value;
									SetValue(*s, state, t, i, v);
								};
								entries.push_back(Death::move(entry));
							}
						});
						dropdown->Width = ChoiceWidth;
					} else {
						bool decimals = (param.Type == ScriptParamType::Float || param.Type == ScriptParamType::Double);
						auto* box = row->Add<UiNumberBox>([s, state, t, i]() {
							return float(GetValue(*s, state, t, i).Number);
						}, [s, state, t, i](float value, bool final) {
							ScriptParamValue v;
							v.Number = value;
							SetValue(*s, state, t, i, v, final);
						}, float(param.Min), float(param.Max), decimals ? 0.1f : 1.0f, decimals ? 2 : 0);
						box->Width = NumberWidth;
						box->Tooltip = param.Description;
					}
					AddChangedMark(row, Death::move(isChanged));
				}

				// What the description doesn't tell
				auto* rest = Add<UiWrappedLabel>(StringView(), UiTheme::TextDim);
				rest->TextFunc = [s, state, t]() {
					SmallVector<ScriptParamValue, 8> values;
					std::size_t used = ReadValues(*s, state, *t, values);
					std::int32_t extra = std::int32_t(state->GetCurrent(*s).ScriptParams.size() - std::min(used, state->GetCurrent(*s).ScriptParams.size()));
					// TRANSLATORS: In the Weapons dialog, bytes of the parameters of a weapon of a script its description doesn't tell
					return _fnx("editor", "And {} byte more, kept as it is", "And {} bytes more, kept as they are", extra, extra);
				};
				rest->IsVisibleFunc = [s, state, t]() {
					SmallVector<ScriptParamValue, 8> values;
					return (ReadValues(*s, state, *t, values) < state->GetCurrent(*s).ScriptParams.size());
				};
			}
		};

		void AddScriptPage(UiForm* page, IEditorContext& session, const StatePtr& state)
		{
			IEditorContext* s = &session;
			{
				// What the code of the script does to the weapon by itself, which the editor doesn't change
				auto* handWritten = page->Add<UiWrappedLabel>(StringView(), UiTheme::TextWarning);
				handWritten->TextFunc = [s, state]() {
					// TRANSLATORS: In the Weapons dialog, the code of the level script that sets the weapon up follows
					return _fx("editor", "The script of the level sets this weapon up by its own code: {}", state->GetHandWrittenSetUp(*s, state->Slot));
				};
				handWritten->IsVisibleFunc = [s, state]() { return !state->GetHandWrittenSetUp(*s, state->Slot).empty(); };
				handWritten->Tooltip = _x("editor", "Its look and how it fires come from that code, the editor only shows it");
				auto* conflict = page->Add<UiWrappedLabel>(_x("editor", "It's a script-driven weapon too, so it's set up twice, which one wins depends on the script"),
					UiTheme::TextDanger);
				conflict->IsVisibleFunc = [s, state]() {
					return (state->GetCurrent(*s).Kind == WeaponKind::Script && !state->GetHandWrittenSetUp(*s, state->Slot).empty());
				};
				// TRANSLATORS: In the Weapons dialog, the option of the game that lets scripts of levels run is off
				auto* scriptsOff = page->Add<UiWrappedLabel>(_x("editor", "Scripts of levels are turned off in the options of the game, script-driven weapons don't work even in play-tests"),
					UiTheme::TextWarning);
				scriptsOff->IsVisibleFunc = []() { return !PreferencesCache::AllowUnsignedScripts; };
				auto* notDriven = page->Add<UiWrappedLabel>(_x("editor", "Choose Script-driven weapon on the General page to set it up as MLLE does"), UiTheme::TextDim);
				notDriven->IsVisibleFunc = [s, state]() { return state->GetCurrent(*s).Kind != WeaponKind::Script; };
			}
			UiForm* form = page->Add<UiForm>();
			form->MinLabelWidth = LabelWidth;
			form->MaxLabelWidth = LabelWidth;
			form->IsEnabledFunc = [s, state]() { return (state->GetCurrent(*s).Kind == WeaponKind::Script && !state->IsReadOnly()); };
			form->Add<UiWrappedLabel>(_x("editor", "The script of the level sets this weapon up, as MLLE does"), UiTheme::TextDim);
			form->Add<UiWrappedLabel>(_x("editor", "The lines that do it are written into the script when the level is saved"), UiTheme::TextDim);
			{
				// TRANSLATORS: Label in the Weapons dialog, the weapon of a script library
				auto* row = AddLabeledRow(form, _x("editor", "Weapon"));
				auto* dropdown = row->Add<UiDropdown>([s, state]() {
					const WeaponDefinition& weapon = state->GetCurrent(*s);
					if (weapon.ScriptConstructor.empty()) {
						// TRANSLATORS: In the Weapons dialog, no weapon of a script is chosen
						return String(_x("editor", "None"));
					}
					const ScriptWeaponTemplate* weaponTemplate = state->FindCurrentTemplate(*s);
					if (weaponTemplate != nullptr && weaponTemplate->Source != ScriptWeaponTemplateSource::Script && !weaponTemplate->Name.empty()) {
						return String(weaponTemplate->Name + " - "_s + weapon.ScriptLibrary);
					}
					return String(weapon.ScriptLibrary + " - "_s + weapon.ScriptConstructor);
				}, [s, state](SmallVectorImpl<UiMenuEntry>& entries) {
					state->LoadTemplates();
					state->FindLibraries(*s);
					auto isCurrent = [s, state](StringView library, StringView constructor) {
						const WeaponDefinition& current = state->GetCurrent(*s);
						return (StringUtils::equalsIgnoreCase(current.ScriptLibrary, library) && current.ScriptConstructor == constructor);
					};

					// The descriptions by their files, then what the libraries have that no description tells
					SmallVector<String, 8> files;
					for (const ScriptWeaponTemplate& weaponTemplate : state->Templates) {
						if (std::find(files.begin(), files.end(), weaponTemplate.SourcePath) == files.end()) {
							files.push_back(weaponTemplate.SourcePath);
						}
					}
					for (const String& file : files) {
						UiMenuEntry group;
						group.Label = fs::GetFileNameWithoutExtension(file);
						group.Submenu = [s, state, file, isCurrent](SmallVectorImpl<UiMenuEntry>& items) {
							for (std::size_t i = 0; i < state->Templates.size(); i++) {
								const ScriptWeaponTemplate& weaponTemplate = state->Templates[i];
								if (weaponTemplate.SourcePath != file) {
									continue;
								}
								UiMenuEntry entry;
								entry.Label = (weaponTemplate.Name.empty() ? weaponTemplate.Constructor : weaponTemplate.Name);
								entry.IsChecked = [state, i, isCurrent]() {
									return isCurrent(state->Templates[i].Library, state->Templates[i].Constructor);
								};
								entry.Execute = [s, state, i]() {
									const ScriptWeaponTemplate& chosen = state->Templates[i];
									ChooseScriptWeapon(*s, state, chosen.Library, chosen.Constructor, chosen.Name);
								};
								items.push_back(Death::move(entry));
							}
						};
						entries.push_back(Death::move(group));
					}

					SmallVector<std::size_t, 8> found;
					for (std::size_t i = 0; i < state->ScriptWeapons.size(); i++) {
						const ScriptWeaponClass& weapon = state->ScriptWeapons[i];
						bool described = false;
						for (const ScriptWeaponTemplate& weaponTemplate : state->Templates) {
							described |= IsScriptWeaponTemplateOf(weaponTemplate, weapon.Library, weapon.Constructor);
						}
						if (!described) {
							found.push_back(i);
						}
					}
					if (!found.empty()) {
						UiMenuEntry group;
						// TRANSLATORS: Submenu in the Weapons dialog, weapons found in script libraries that no description of weapons tells
						group.Label = _x("editor", "Found in libraries");
						group.Submenu = [s, state, found, isCurrent](SmallVectorImpl<UiMenuEntry>& items) {
							for (std::size_t i : found) {
								UiMenuEntry entry;
								entry.Label = state->ScriptWeapons[i].Library + " - "_s + state->ScriptWeapons[i].Constructor;
								entry.IsChecked = [state, i, isCurrent]() {
									return isCurrent(state->ScriptWeapons[i].Library, state->ScriptWeapons[i].Constructor);
								};
								entry.Execute = [s, state, i]() {
									const ScriptWeaponClass& chosen = state->ScriptWeapons[i];
									ChooseScriptWeapon(*s, state, chosen.Library, chosen.Constructor, {});
								};
								items.push_back(Death::move(entry));
							}
						};
						entries.push_back(Death::move(group));
					}

					if (entries.empty()) {
						UiMenuEntry none;
						// TRANSLATORS: In the Weapons dialog, no weapon of a script was found, keep "Editor/Weapons" as it is
						none.Label = _x("editor", "No weapons found, descriptions go into \"Editor/Weapons\" of the content");
						none.IsEnabled = []() { return false; };
						entries.push_back(Death::move(none));
					}
				});
				dropdown->Flex = 1.0f;
				dropdown->Tooltip = _x("editor", "Weapons of the descriptions in \"Editor/Weapons\" (.ini of MLLE or .json), and of the script libraries (.asc) next to the level and in the files of the original game");
			}
			{
				// A library that isn't there fails the whole script, not only the weapon
				auto* missing = form->Add<UiWrappedLabel>(StringView(), UiTheme::TextDanger);
				missing->TextFunc = [s, state]() {
					// TRANSLATORS: In the Weapons dialog, the script library of a weapon isn't found, {} is its file name
					return _fx("editor", "The library \"{}\" isn't next to the level or in the files of the original game, the script of the level won't run",
						state->GetCurrent(*s).ScriptLibrary);
				};
				missing->IsVisibleFunc = [s, state]() {
					const WeaponDefinition& weapon = state->GetCurrent(*s);
					return (weapon.Kind == WeaponKind::Script && !weapon.ScriptLibrary.empty() && !state->IsLibraryFound(*s, weapon.ScriptLibrary));
				};
				missing->Tooltip = _x("editor", "Put the library (.asc) next to the level or into the \"Source\" folder, with the files it needs");
			}
			{
				// Where the parameters come from, and the calls of the hook the weapon needs that the script doesn't make
				auto* source = form->Add<UiWrappedLabel>(StringView(), UiTheme::TextDim);
				source->TextFunc = [s, state]() {
					const ScriptWeaponTemplate* weaponTemplate = state->FindCurrentTemplate(*s);
					if (weaponTemplate == nullptr) {
						return String();
					}
					if (weaponTemplate->Source == ScriptWeaponTemplateSource::Script) {
						if (weaponTemplate->Complete && weaponTemplate->Params.empty()) {
							// TRANSLATORS: In the Weapons dialog, nothing in the library of a weapon of a script reads parameters
							return _fx("editor", "It takes no parameters, as {} shows", fs::GetFileName(weaponTemplate->SourcePath));
						}
						return (weaponTemplate->Complete
							// TRANSLATORS: In the Weapons dialog, the parameters of a weapon of a script were guessed from its library
							? _fx("editor", "Parameters guessed from {}", fs::GetFileName(weaponTemplate->SourcePath))
							// TRANSLATORS: In the Weapons dialog, only some parameters of a weapon of a script could be guessed from its library
							: _fx("editor", "Only the first {} parameters could be guessed from {}", std::int32_t(weaponTemplate->Params.size()), fs::GetFileName(weaponTemplate->SourcePath)));
					}
					// TRANSLATORS: In the Weapons dialog, the file that describes a weapon of a script follows
					return _fx("editor", "Described by {}", fs::GetFileName(weaponTemplate->SourcePath));
				};
				source->IsVisibleFunc = [s, state]() { return state->FindCurrentTemplate(*s) != nullptr; };

				auto* hooks = form->Add<UiWrappedLabel>(StringView(), UiTheme::TextWarning);
				auto getMissingHooks = [](IEditorContext& session, const StatePtr& state) {
					String missing;
					const ScriptWeaponTemplate* weaponTemplate = state->FindCurrentTemplate(session);
					if (weaponTemplate == nullptr) {
						return missing;
					}
					StringView script = state->GetScriptText(session);
					for (const String& hook : weaponTemplate->Hooks) {
						StringView call = GetScriptWeaponHookCall(hook);
						if (!call.empty() && !script.contains(call)) {
							missing += (missing.empty() ? StringView() : ", "_s) + hook + "() - MLLE::WeaponHook."_s + call + ")"_s;
						}
					}
					return missing;
				};
				hooks->TextFunc = [s, state, getMissingHooks]() {
					// TRANSLATORS: In the Weapons dialog, callbacks of the level script the weapon needs and the calls they have to make follow
					return _fx("editor", "The script of the level has to pass these on to the weapon: {}", getMissingHooks(*s, state));
				};
				hooks->IsVisibleFunc = [s, state, getMissingHooks]() { return !getMissingHooks(*s, state).empty(); };
				hooks->Tooltip = _x("editor", "MLLE doesn't write them, the callbacks of the script have to call the hook of the weapons themselves");
			}
			form->Add<ScriptParamsForm>(session, state);
			{
				auto* row = form->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				row->IsVisibleFunc = [s, state]() { return state->FindCurrentTemplate(*s) != nullptr; };
				// TRANSLATORS: Button in the Weapons dialog, the parameters of a weapon of a script become as its description has them
				auto* reset = row->Add<UiButton>(_x("editor", "Reset Parameters"), EditorIcon::Undo, [s, state]() {
					if (const ScriptWeaponTemplate* weaponTemplate = state->FindCurrentTemplate(*s)) {
						SmallVector<std::uint8_t, 0> params;
						MakeDefaultScriptParams(*weaponTemplate, params);
						Change(*s, state, [&params](WeaponDefinition& weapon) { weapon.ScriptParams.assign(params.begin(), params.end()); });
					}
				});
				reset->Tooltip = _x("editor", "The parameters become as the description of the weapon has them");
			}
			{
				// TRANSLATORS: Label in the Weapons dialog, the script code that creates the weapon
				auto* row = AddLabeledRow(form, _x("editor", "Created by"));
				auto* box = row->Add<UiTextBox>();
				box->Width = 180.0f;
				box->Flex = 1.0f;
				box->MaxLength = 255;
				box->Placeholder = "Namespace::Weapon()"_s;
				box->TextFunc = [s, state]() { return state->GetCurrent(*s).ScriptConstructor; };
				box->OnCommit = [s, state](StringView text) {
					String constructor = text.trimmed();
					Change(*s, state, [&constructor](WeaponDefinition& weapon) { weapon.ScriptConstructor = constructor; });
				};
				box->Tooltip = _x("editor", "Script code that creates the weapon, with its arguments");
			}
			{
				// Bytes no description tells, only kept or removed
				// TRANSLATORS: Label in the Weapons dialog, the parameters the script gets for the weapon
				auto* row = AddLabeledRow(form, _x("editor", "Parameters"));
				ShowRowWhen(row, [s, state]() { return state->FindCurrentTemplate(*s) == nullptr; });
				auto* label = row->Add<UiLabel>(StringView(), UiTheme::TextDim);
				label->TextFunc = [s, state]() {
					std::int32_t size = std::int32_t(state->GetCurrent(*s).ScriptParams.size());
					return _fnx("editor", "{} byte, kept as it is", "{} bytes, kept as they are", size, size);
				};
				auto* clear = row->Add<UiButton>(StringView(), EditorIcon::Delete, [s, state]() {
					Change(*s, state, [](WeaponDefinition& weapon) { weapon.ScriptParams.clear(); });
				});
				clear->Tooltip = _x("editor", "Removes the parameters, the weapon starts with what it has by default");
			}
			{
				// TRANSLATORS: Label in the Weapons dialog, the argument of the hook all weapons of the script share
				auto* row = AddLabeledRow(form, _x("editor", "Hook argument"));
				auto* box = row->Add<UiNumberBox>([s]() {
					WeaponsDescription weapons;
					ReadLevelWeapons(s->GetDocument(), weapons);
					return float(weapons.ScriptHookArgument);
				}, [s](float value, bool final) {
					ChangeLevelWeapons(*s, _x("editor", "Weapons"), [value](WeaponsDescription& weapons) {
						weapons.ScriptHookArgument = std::int16_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), -1, 255));
					}, final);
				}, -1.0f, 255.0f, 1.0f, 0);
				box->Width = NumberWidth;
				box->Tooltip = _x("editor", "Packet ID of the hook the weapons of the script share, -1 for none, as all levels of MLLE");
			}
			{
				// TRANSLATORS: Choices of how many bullets a weapon of a script fires at once in the Weapons dialog (SPREAD of JJ2+)
				static const PropertyChoice SpreadChoices[] = {
					{ _x("editor", "One shot"), std::int32_t(WeaponSpread::Normal) },
					{ _x("editor", "Two shots, up and down"), std::int32_t(WeaponSpread::Double) },
					{ _x("editor", "Three shots"), std::int32_t(WeaponSpread::Triple) },
					{ _x("editor", "Two shots, three powered up"), std::int32_t(WeaponSpread::DoubleOrTriple) },
					{ _x("editor", "Two shots, ahead and aimed"), std::int32_t(WeaponSpread::DirectionAndAim) },
					{ _x("editor", "One shot, two powered up"), std::int32_t(WeaponSpread::NormalOrDirectionAndAim) },
					{ _x("editor", "Spray"), std::int32_t(WeaponSpread::Pepper) },
					{ _x("editor", "One shot, spray powered up"), std::int32_t(WeaponSpread::NormalOrPepper) },
					{ _x("editor", "One shot, faster with fast fire"), std::int32_t(WeaponSpread::Toaster) },
				};
				// TRANSLATORS: Label in the Weapons dialog, how the bullets a script moves are spread (SPREAD::Spread of JJ2+)
				auto* row = AddLabeledRow(form, _x("editor", "Spread"));
				AddChoice(row, SpreadChoices, [s, state]() {
					return std::int32_t(state->GetCurrent(*s).Options.Spread);
				}, [s, state](std::int32_t value) {
					Change(*s, state, [value](WeaponDefinition& weapon) { weapon.Options.Spread = WeaponSpread(value); });
				});
				row->Tooltip = _x("editor", "How many bullets a script that moves them fires at once, as SPREAD of JJ2+");
				AddChangedMark(row, [s, state]() { return state->GetCurrent(*s).Options.Spread != state->GetBaseline(*s).Options.Spread; });
			}
		}
	}

	StringView GetStockWeaponName(WeaponType weaponType)
	{
		for (const PropertyChoice& choice : GetStockChoices()) {
			if (choice.Value == std::int32_t(weaponType)) {
				return choice.Label;
			}
		}
		return {};
	}

	String GetLevelWeaponName(const WeaponsDescription& weapons, WeaponType weaponType)
	{
		if (weaponType >= WeaponType::Count) {
			return {};
		}
		const WeaponDefinition& weapon = weapons.Weapons[std::int32_t(weaponType)];
		return (weapon.Name.empty() ? String(GetStockWeaponName(weaponType)) : weapon.Name);
	}

	const WeaponsDescription& GetCachedLevelWeapons(IEditorContext& session)
	{
		static WeaponsDescription cached;
		static std::uint32_t revision = UINT32_MAX;
		static const CommandHistory* history = nullptr;
		static const std::uint8_t* data = nullptr;
		static std::size_t size = 0;

		const Formats::ChunkRecord* chunk = session.GetDocument().FindChunk(Formats::WeaponsChunk::Tag);
		const std::uint8_t* chunkData = (chunk != nullptr ? chunk->Data.data() : nullptr);
		std::size_t chunkSize = (chunk != nullptr ? chunk->Data.size() : 0);
		CommandHistory& currentHistory = session.GetHistory();
		if (revision != currentHistory.GetRevision() || history != &currentHistory || data != chunkData || size != chunkSize) {
			revision = currentHistory.GetRevision();
			history = &currentHistory;
			data = chunkData;
			size = chunkSize;
			if (ReadLevelWeapons(session.GetDocument(), cached) != Formats::WeaponsChunk::ReadResult::Success) {
				cached = WeaponsDescription();
			}
		}
		return cached;
	}

	String GetEventOptionLabel(IEditorContext& session, const EventParamField& field, const ParamOption& option)
	{
		// "Random" of a crate isn't a weapon, even if its value is
		if (field.IsWeapon && option.Value >= 0 && option.Value < SlotCount && StringView(option.Label) == GetStockWeaponName(WeaponType(option.Value))) {
			return GetLevelWeaponName(GetCachedLevelWeapons(session), WeaponType(option.Value));
		}
		return option.Label;
	}

	Formats::WeaponsChunk::ReadResult ReadLevelWeapons(const LevelDocument& document, WeaponsDescription& weapons)
	{
		const Formats::ChunkRecord* chunk = document.FindChunk(Formats::WeaponsChunk::Tag);
		if (chunk == nullptr || chunk->Data.empty()) {
			weapons = WeaponsDescription();
			return Formats::WeaponsChunk::ReadResult::Success;
		}
		return Formats::WeaponsChunk::Read(chunk->Data, weapons);
	}

	bool ChangeLevelWeapons(IEditorContext& session, StringView name, Function<void(WeaponsDescription&)>&& change, bool final)
	{
		LevelDocument& document = session.GetDocument();
		WeaponsDescription weapons;
		Formats::WeaponsChunk::ReadResult result = ReadLevelWeapons(document, weapons);
		if (result != Formats::WeaponsChunk::ReadResult::Success) {
			session.ShowMessage(result == Formats::WeaponsChunk::ReadResult::NewerVersion
				? _x("editor", "The weapons of the level are of a newer version, they can't be changed here")
				: _x("editor", "The weapons of the level are damaged, they can't be changed here"));
			return false;
		}
		change(weapons);

		SmallVector<std::uint8_t, 0> data;
		Formats::WeaponsChunk::Write(weapons, data);
		const Formats::ChunkRecord* chunk = document.FindChunk(Formats::WeaponsChunk::Tag);
		bool same = (chunk != nullptr ? (chunk->Data.size() == data.size() && std::memcmp(chunk->Data.data(), data.data(), data.size()) == 0) : data.empty());
		if (!same) {
			session.Execute(std::make_unique<SetChunkCommand>(document, Formats::WeaponsChunk::Tag, data, String(name)), true);
		}
		if (final) {
			session.GetHistory().BreakMerge();
		}
		return !same;
	}

	void ShowWeaponsDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr || ReuseSingleDialog(session, "weapons")) {
			return;
		}

		IEditorContext* s = &session;
		auto state = std::make_shared<WeaponsState>();
		// TRANSLATORS: Title of the dialog of the weapons of the level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Weapons"));
		dialog->MaxScreenHeightFraction = 0.9f;
		dialog->MinScreenWidthFraction = 0.6f;
		dialog->MaxStretchedWidth = 720.0f;
		dialog->Resizable = true;
		dialog->MinResizeSize = Vector2f(560.0f, 260.0f);
		UiStack* content = dialog->GetContent();

		auto* readOnly = content->Add<UiWrappedLabel>(StringView(), UiTheme::TextDanger);
		readOnly->TextFunc = [s, state]() {
			state->Get(*s);
			return String(state->GetResult() == Formats::WeaponsChunk::ReadResult::NewerVersion
				? _x("editor", "The weapons of the level are of a newer version, they're shown but can't be changed")
				: _x("editor", "The weapons of the level are damaged, they're kept as they are"));
		};
		readOnly->IsVisibleFunc = [s, state]() {
			state->Get(*s);
			return state->IsReadOnly();
		};

		auto* columns = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 2.0f);
		columns->Flex = 1.0f;

		// The weapons, which scroll in a low window, and trying the one chosen
		auto* left = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		left->Width = 190.0f;
		auto* slotScroll = left->Add<UiScrollView>();
		slotScroll->Flex = 1.0f;
		auto* slots = slotScroll->SetContent<UiStack>(UiOrientation::Vertical, UiTheme::Spacing, UiThickness(0.0f, 0.0f, 4.0f, 0.0f));
		for (std::int32_t i = 0; i < SlotCount; i++) {
			slots->Add<WeaponSlotButton>(session, state, i);
		}
		left->Add<UiSeparator>();
		// TRANSLATORS: Button in the Weapons dialog, plays the level with the chosen weapon in hand
		auto* tryIt = left->Add<UiButton>(_x("editor", "Try It"), EditorIcon::Weapon, [s, state]() {
			s->PlayTestWeapon(WeaponType(state->Slot), false);
		});
		tryIt->IconColor = Colorf::White;
		tryIt->Tooltip = _x("editor", "Plays the level from the middle of the view with this weapon in hand and full of ammo");
		// TRANSLATORS: Button in the Weapons dialog, plays the level with the chosen weapon powered up in hand
		auto* tryPowered = left->Add<UiButton>(_x("editor", "Try Powered Up"), EditorIcon::Weapon, [s, state]() {
			s->PlayTestWeapon(WeaponType(state->Slot), true);
		});
		tryPowered->IconColor = Colorf::White;
		tryPowered->Tooltip = _x("editor", "The same, with the weapon powered up");

		columns->Add<UiSeparator>(UiOrientation::Vertical);
		auto* right = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		right->Flex = 1.0f;
		right->Width = 380.0f;

		auto isKind = [s, state](WeaponKind kind) {
			return [s, state, kind]() { return state->GetCurrent(*s).Kind == kind; };
		};
		{
			auto* tabs = right->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			auto addTab = [&tabs, state](WeaponsPage page, StringView label, Function<bool()>&& isVisible) {
				auto* tab = tabs->Add<UiButton>(label, EditorIcon::None, [state, page]() { state->Page = page; });
				tab->IsCheckedFunc = [state, page]() { return state->Page == page; };
				if (isVisible) {
					tab->IsVisibleFunc = Death::move(isVisible);
				}
			};
			// TRANSLATORS: Pages of the Weapons dialog
			addTab(WeaponsPage::General, _x("editor", "General"), {});
			addTab(WeaponsPage::Ammo, _x("editor", "Ammo"), {});
			addTab(WeaponsPage::Firing, _x("editor", "Firing"), isKind(WeaponKind::Custom));
			addTab(WeaponsPage::Shot, _x("editor", "Shot"), isKind(WeaponKind::Custom));
			addTab(WeaponsPage::PoweredUp, _x("editor", "Powered Up"), isKind(WeaponKind::Custom));
			addTab(WeaponsPage::Animations, _x("editor", "Animations"), {});
			addTab(WeaponsPage::Script, _x("editor", "Script"), [s, state]() {
				return (state->GetCurrent(*s).Kind == WeaponKind::Script || !state->GetHandWrittenSetUp(*s, state->Slot).empty());
			});
		}

		auto* scroll = right->Add<UiScrollView>();
		scroll->Flex = 1.0f;
		scroll->Height = 220.0f;
		auto* pages = scroll->SetContent<UiStack>(UiOrientation::Vertical, UiTheme::Spacing, UiThickness(0.0f, 0.0f, 4.0f, 0.0f));
		auto addPage = [&pages, s, state](WeaponsPage page) {
			UiForm* form = pages->Add<UiForm>();
			// The same on all pages, so the values don't move between them or as rows are shown
			form->MinLabelWidth = LabelWidth;
			form->MaxLabelWidth = LabelWidth;
			form->IsVisibleFunc = [s, state, page]() {
				WeaponKind kind = state->GetCurrent(*s).Kind;
				// A page that doesn't apply to the weapon shows the first one instead
				bool applies = ((page != WeaponsPage::Firing && page != WeaponsPage::Shot && page != WeaponsPage::PoweredUp) || kind == WeaponKind::Custom) &&
					(page != WeaponsPage::Script || kind == WeaponKind::Script || !state->GetHandWrittenSetUp(*s, state->Slot).empty());
				if (state->Page == page && !applies) {
					state->Page = WeaponsPage::General;
				}
				return (state->Page == page && applies);
			};
			form->IsEnabledFunc = [state]() { return !state->IsReadOnly(); };
			return form;
		};
		AddGeneralPage(addPage(WeaponsPage::General), session, state);
		AddAmmoPage(addPage(WeaponsPage::Ammo), session, state);
		AddFiringPage(addPage(WeaponsPage::Firing), session, state);
		AddShotPage(addPage(WeaponsPage::Shot), session, state, false);
		AddShotPage(addPage(WeaponsPage::PoweredUp), session, state, true);
		AddAnimationsPage(addPage(WeaponsPage::Animations), session, state);
		AddScriptPage(addPage(WeaponsPage::Script), session, state);

		// The shots of a custom weapon in a firing range
		{
			auto* previewArea = right->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			previewArea->IsVisibleFunc = isKind(WeaponKind::Custom);
			previewArea->Add<UiSeparator>();
			auto* preview = previewArea->Add<WeaponPreview>(session, [s, state]() {
				return &state->Get(*s);
			}, [state]() {
				return WeaponType(state->Slot);
			}, [state]() {
				return state->PreviewPowered;
			});
			preview->Height = 104.0f;
			auto* row = previewArea->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Checkbox under the preview of a weapon in the Weapons dialog
			row->Add<UiCheckBox>(_x("editor", "Show it powered up"), [state]() { return state->PreviewPowered; },
				[state](bool value) { state->PreviewPowered = value; });
			auto* summary = row->Add<UiLabel>(StringView(), UiTheme::TextDim, Alignment::Right);
			summary->Flex = 1.0f;
			summary->TextFunc = [preview]() { return preview->GetSummary(); };
		}

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		ShowSingleDialog(session, "weapons", Death::move(dialog), Death::move(options));
	}
}

#endif
