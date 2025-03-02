#include "ImportSection.h"

#if defined(SHAREWARE_DEMO_ONLY) && defined(DEATH_TARGET_EMSCRIPTEN)

#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Base/Algorithms.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	ImportSection::ImportSection()
		: _animation(0.0f), _state(State::Waiting), _fileCount(0), _timeout(0)
	{
	}

	void ImportSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		ShowPicker();
	}

	void ImportSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_state == State::Loading && !_picker.IsCancelSupported()) {
			if (_timeout > 0.0f) {
				_timeout -= timeMult;
			} else {
				_state = State::NothingSelected;
			}
		}

		if (_root->ActionHit(PlayerAction::Fire)) {
			if (_state > State::Loading) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				ShowPicker();
			}
		} else if (_root->ActionHit(PlayerAction::Menu)) {
			if (_state != State::Loading) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->LeaveSection();
			}
		}
	}

	void ImportSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		Vector2f center = Vector2f(contentBounds.X + contentBounds.W * 0.5f, contentBounds.Y + contentBounds.H * 0.5f);
		float topLine = contentBounds.Y + TopLine + 28.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		center.Y = topLine + (bottomLine - topLine) * 0.4f;
		std::int32_t charOffset = 0;

		_root->DrawStringShadow(_("Import Episodes"), charOffset, center.X, contentBounds.Y + TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// TRANSLATORS: Header in Import Episodes section
		_root->DrawStringShadow(_("Select files of your original game to unlock additional episodes"), charOffset, center.X, topLine - 15.0f - 4.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.76f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		switch (_state) {
			case State::Loading:
				if (_fileCount > 0) {
					_root->DrawStringShadow(_fn("Processing of %i file...", "Processing of %i files...", _fileCount, _fileCount), charOffset, center.X, center.Y, IMenuContainer::FontLayer,
						Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					_root->DrawStringShadow(_("Waiting for files..."), charOffset, center.X, center.Y, IMenuContainer::FontLayer,
						Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				}
				break;

			case State::NothingSelected:
				_root->DrawStringShadow(_("No files were selected!"), charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				break;

			case State::NothingImported:
				_root->DrawStringShadow(_("No new episodes were imported!"), charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				break;
		}
	}

	void ImportSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y < 80.0f) {
					if (_state != State::Loading) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
					}
					return;
				} else if (y > 120.0f && _state > State::Loading) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					ShowPicker();
					return;
				}
			}
		}
	}

	void ImportSection::OnFilesReceived(ArrayView<EmscriptenFileStream> files)
	{
		_fileCount = (std::int32_t)files.size();
		if (_fileCount <= 0) {
			_state = State::NothingSelected;
		}

		for (auto& file : files) {
			if (file.GetSize() >= 262 && fs::GetExtension(file.GetName()) == "j2l"_s) {
				file.Seek(180, SeekOrigin::Current); // Skip header

				std::uint32_t magic = file.ReadValue<std::uint32_t>();
				if (magic == 0x4C56454C /*LEVL*/) {
					file.Seek(4, SeekOrigin::Current);

					char name[32 + 1];
					name[32] = '\0';
					file.Read(name, 32);

					LOGD("Found level: %s", name);
					_foundLevels.emplace(name, true);
				}
			}
		}

		CheckFoundLevels();
	}

	void ImportSection::ShowPicker()
	{
		_state = State::Loading;
		_fileCount = 0;
		_timeout = 10.0f * FrameTimer::FramesPerSecond;

		// Directories are not supported by mobile browsers yet
		//_picker.FetchDirectoryAsync({ *this, &ImportSection::OnFilesReceived });
		_picker.FetchFilesAsync(".j2l"_s, true, { *this, &ImportSection::OnFilesReceived });
	}

	void ImportSection::CheckFoundLevels()
	{
		static const StringView FormerlyAPrinceLevels[] = { "Dungeon Dilemma"_s, "Knight Cap"_s, "Tossed Salad"_s, "Carrot Juice"_s, "Weirder Science"_s, "Loose Screws"_s };
		static const StringView JazzInTimeLevels[] = { "Victorian Secret"_s, "Colonial Chaos"_s, "Purple Haze Maze"_s, "Funky Grooveathon"_s, "Beach Bunny Bingo"_s, "Marinated Rabbit"_s };
		static const StringView FlashbackLevels[] = { "A Diamondus Forever"_s, "Fourteen Carrot"_s, "Electric Boogaloo"_s, "Voltage Village"_s, "Medieval Kineval"_s, "Hare Scare"_s };
		static const StringView FunkyMonkeysLevels[] = { "Thriller Gorilla"_s, "Jungle Jump"_s, "A Cold Day In Heck"_s, "Rabbit Roast"_s, "Burnin Biscuits"_s, "Bad Pitt"_s };
		static const StringView ChristmasChroniclesLevels[] = { "Snow Bunnies"_s, "Dashing thru the snow.."_s, "Tinsel Town"_s };
		static const StringView TheSecretFilesLevels[] = { "Easter Bunny"_s, "Spring Chickens"_s, "Scrambled Eggs"_s, "Ghostly Antics"_s, "Skeletons Turf"_s, "Graveyard Shift"_s, "Turtle Town"_s, "Suburbia Commando"_s, "Urban Brawl"_s };

		UnlockableEpisodes unlockedEpisodes = PreferencesCache::UnlockedEpisodes;
		if (HasAllLevels(FormerlyAPrinceLevels)) unlockedEpisodes |= UnlockableEpisodes::FormerlyAPrince;
		if (HasAllLevels(JazzInTimeLevels)) unlockedEpisodes |= UnlockableEpisodes::JazzInTime;
		if (HasAllLevels(FlashbackLevels)) unlockedEpisodes |= UnlockableEpisodes::Flashback;
		if (HasAllLevels(FunkyMonkeysLevels)) unlockedEpisodes |= UnlockableEpisodes::FunkyMonkeys;
		if (HasAllLevels(ChristmasChroniclesLevels)) unlockedEpisodes |= UnlockableEpisodes::ChristmasChronicles;
		if (HasAllLevels(TheSecretFilesLevels)) unlockedEpisodes |= UnlockableEpisodes::TheSecretFiles;

		if (PreferencesCache::UnlockedEpisodes != unlockedEpisodes) {
			PreferencesCache::UnlockedEpisodes = unlockedEpisodes;
			PreferencesCache::Save();
			_state = State::Success;

			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		} else {
			_state = State::NothingImported;
		}
	}

	bool ImportSection::HasAllLevels(ArrayView<const StringView> levelNames)
	{
		bool hasAll = true;
		for (auto levelName : levelNames) {
			if (_foundLevels.find(String::nullTerminatedView(levelName)) == _foundLevels.end()) {
				hasAll = false;
				break;
			}
		}
		return hasAll;
	}
}

#endif