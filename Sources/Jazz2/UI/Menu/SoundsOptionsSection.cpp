#include "SoundsOptionsSection.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"
#if defined(WITH_PSPAUDIO) || defined(WITH_PS2AUDIO) || defined(WITH_NDSP) || defined(WITH_AHIAUDIO)
#	include "../../../nCine/ServiceLocator.h"
#	include "../../../nCine/Audio/IAudioDevice.h"
#endif

#include <algorithm>
#include <Utf8.h>

namespace Jazz2::UI::Menu
{
	SoundsOptionsSection::~SoundsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
#if defined(WITH_PSPAUDIO) || defined(WITH_PS2AUDIO) || defined(WITH_NDSP) || defined(WITH_AHIAUDIO)
		if (_sampleRateChanged && _root != nullptr) {
			// The mixer already runs at the new rate, but a stream that was open keeps decoding at the one it
			// was opened with (the mixer resamples it, so it plays at the right pitch, only at the old cost).
			// Restarting the menu music here reopens it at the new rate; in game it is ignored (see
			// InGameMenu::ApplyPreferencesChanges) and the level music follows on the next track instead.
			_sampleRateChanged = false;
			_root->ApplyPreferencesChanges(ChangedPreferencesType::MainMenu);
		}
#endif
	}

	void SoundsOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Sounds"));

		// A ScrollView rather than a spread-out StackLayout: with the sample-rate row the list no longer fits the
		// 240-row panels (the 3DS, the PSP), and a StackLayout would crop it rather than scroll
		auto list = std::make_unique<ScrollView>();
		// The last row shows its value under the label, which reaches the row's bottom edge; a little more room
		// after it than the default keeps the value off the frame when the list is scrolled to its end
		list->ContentPadding = 24.0f;

		// TRANSLATORS: Menu item in Options > Sounds section
		list->Add<Slider>(_("Master Volume"),
			[]() -> float { return PreferencesCache::MasterVolume; },
			[this](float delta) {
				PreferencesCache::MasterVolume = std::clamp(PreferencesCache::MasterVolume + delta, 0.0f, 1.0f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Sounds section
		list->Add<Slider>(_("SFX Volume"),
			[]() -> float { return PreferencesCache::SfxVolume; },
			[this](float delta) {
				PreferencesCache::SfxVolume = std::clamp(PreferencesCache::SfxVolume + delta, 0.0f, 1.0f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Sounds section
		list->Add<Slider>(_("Music Volume"),
			[]() -> float { return PreferencesCache::MusicVolume; },
			[this](float delta) {
				PreferencesCache::MusicVolume = std::clamp(PreferencesCache::MusicVolume + delta, 0.0f, 1.0f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
				_isDirty = true;
			});

#if defined(WITH_PSPAUDIO) || defined(WITH_PS2AUDIO) || defined(WITH_NDSP) || defined(WITH_AHIAUDIO)
		// Only the software-mixing consoles have a mixing rate to trade for CPU time (the mixer's cost is linear
		// in it, and so is the module decoder's, which renders at the device's rate): the PSP mixes at half or a
		// quarter of its hardware's 44100 Hz (see PspAudioDevice), the PS2 at any rate audsrv accepts and the
		// SPU2 then resamples from (see Ps2AudioDevice), the 3DS at any rate its DSP then resamples to its own
		// 32728 Hz (see NdspAudioDevice), the Amiga at whatever AHI resamples from. The change applies to the
		// effects at once; the music is reopened at the new rate when the section is left (see the destructor).
		// TRANSLATORS: Menu item in Options > Sounds section
		auto* sampleRateItem = list->Add<ChoiceItem>(_("Sample Rate"),
			[this]() -> StringView {
				// `0` means the platform's default, which is whatever the device chose for itself
				std::int32_t rate = (PreferencesCache::AudioSampleRate > 0
					? PreferencesCache::AudioSampleRate
					: theServiceLocator().GetAudioDevice().nativeFrequency());
				_sampleRateValue = format("{} Hz", rate);
				return _sampleRateValue;
			},
			[this](std::int32_t direction) {
				// Ascending presets so Right increases and Left decreases; clamped at the ends (no wraparound)
#	if defined(WITH_PSPAUDIO)
				// 44100 Hz would only spend the CPU the whole option is there to save: the samples are 11-22 kHz
				// and the module music is capped at 22050 Hz on this console regardless (see AudioLoaderMpt)
				static const std::int32_t presets[] = { 11025, 22050 };
#	elif defined(WITH_NDSP)
				// The DSP outputs at 32728 Hz, so that is the most a mix can be heard at; mixing at 44100 Hz would
				// spend CPU on samples the hardware resamples straight back down
				static const std::int32_t presets[] = { 11025, 22050, 32728 };
#	else
				// The full set, which is also what the PlayStation 2 takes: the SPU2 runs at 48000 Hz and audsrv
				// pitches the stream up to it, so any of these plays back correctly, and the EE has the headroom
				// for 44100 in a way the handhelds do not (see Ps2AudioDevice)
				static const std::int32_t presets[] = { 11025, 22050, 44100 };
#	endif
				constexpr std::int32_t count = (std::int32_t)(sizeof(presets) / sizeof(presets[0]));
				std::int32_t current = (PreferencesCache::AudioSampleRate > 0
					? PreferencesCache::AudioSampleRate
					: theServiceLocator().GetAudioDevice().nativeFrequency());
				std::int32_t index = count - 1;
				for (std::int32_t i = 0; i < count; i++) {
					if (current <= presets[i]) {
						index = i;
						break;
					}
				}
				index += direction;
				if (index < 0) {
					index = 0;
				} else if (index >= count) {
					index = count - 1;
				}
				PreferencesCache::AudioSampleRate = presets[index];
				theServiceLocator().GetAudioDevice().setMixingFrequency(presets[index]);
				_sampleRateChanged = true;
				_isDirty = true;
			});
		// Set apart a little from the volume sliders above it, which it is not one of
		sampleRateItem->MarginTop = 8.0f;
#endif

		SetContent(std::move(list));
	}
}
