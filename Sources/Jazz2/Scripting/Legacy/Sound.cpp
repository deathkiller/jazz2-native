#if defined(WITH_ANGELSCRIPT)

#include "Sound.h"
#include "Internal.h"

#include "../../Compatibility/AnimSetMapping.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Audio/AudioBuffer.h"
#include "../../../nCine/Audio/AudioBufferPlayer.h"
#include "../../../nCine/Audio/AudioStreamPlayer.h"
#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	bool LevelScriptLoader::jjMusicLoad(const String& filename, bool forceReload, bool temporary) {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		bool result = _this->_levelHandler->BeginPlayMusic(filename, !temporary, forceReload);
		// The music that already plays is kept, which counts as success
		return (result || (_this->_levelHandler->_music != nullptr && _this->_levelHandler->_musicCurrentPath == filename));
#else
		return false;
#endif
	}
	void LevelScriptLoader::jjMusicStop() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->Stop();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPlay() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->Play();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPause() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr && _this->_levelHandler->_music->IsPlaying()) {
			_this->_levelHandler->_music->Pause();
		}
#endif
	}
	void LevelScriptLoader::jjMusicResume() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr && _this->_levelHandler->_music->IsPaused()) {
			_this->_levelHandler->_music->Play();
		}
#endif
	}

	AudioBuffer* LevelScriptLoader::ResolveSampleBuffer(std::int32_t sampleId)
	{
#if defined(WITH_AUDIO)
		auto it = _scriptSamples.find(sampleId);
		if (it != _scriptSamples.end()) {
			// A cached null entry means the sample previously failed to load - don't keep retrying
			return it->second.get();
		}

		// The sample mapping is the same one the content pipeline uses to convert the original sound bank, so the
		// original sample index maps directly to the converted "Animations/{Category}/{Name}.wav" asset
		if (_sampleMapping == nullptr) {
			Compatibility::JJ2Version version = (versionTSF ? Compatibility::JJ2Version::TSF : Compatibility::JJ2Version::BaseGame);
			_sampleMapping = std::make_unique<Compatibility::AnimSetMapping>(Compatibility::AnimSetMapping::GetSampleMapping(version));
		}

		std::unique_ptr<AudioBuffer> buffer;
		auto* entry = _sampleMapping->GetByOrdinal((std::uint32_t)sampleId);
		if (entry != nullptr && entry->Category != Compatibility::AnimSetMapping::Discard && !entry->Name.empty()) {
			String path = String("Animations/"_s) + entry->Category + '/' + entry->Name + ".wav"_s;
			auto stream = ContentResolver::Get().OpenContentFile(path);
			if (stream->IsValid()) {
				buffer = std::make_unique<AudioBuffer>(Death::move(stream), path);
			}
		}

		AudioBuffer* result = buffer.get();
		_scriptSamples.emplace(sampleId, Death::move(buffer));
		return result;
#else
		return nullptr;
#endif
	}

	std::shared_ptr<AudioBufferPlayer> LevelScriptLoader::PlaySample(const Vector2f& pos, std::int32_t sampleId, std::int32_t volume, std::int32_t frequency, bool sourceRelative)
	{
#if defined(WITH_AUDIO)
		AudioBuffer* buffer = ResolveSampleBuffer(sampleId);
		if (buffer == nullptr) {
			return nullptr;
		}

		// JJ2 volume is roughly 0-64 (0x40 = full); frequency 0 means the sample's natural rate
		float gain = (volume <= 0 ? 0.0f : volume / 64.0f);
		float pitch = (frequency <= 0 || buffer->GetFrequency() <= 0 ? 1.0f : (float)frequency / (float)buffer->GetFrequency());

		return _levelHandler->PlaySfx(nullptr, {}, buffer, Vector3f(pos.X, pos.Y, 0.0f), sourceRelative, gain, pitch, SfxFlags::None);
#else
		return nullptr;
#endif
	}

	std::int32_t LevelScriptLoader::PlayLoopedSample(const Vector2f& pos, std::int32_t sampleId, std::int32_t channel, std::int32_t volume, std::int32_t frequency)
	{
#if defined(WITH_AUDIO)
		auto it = (channel > 0 ? _loopedSamples.find(channel) : _loopedSamples.end());
		if (it != _loopedSamples.end() && it->second.Player != nullptr && it->second.Player->IsPlaying()) {
			// Goes on where the object is now
			it->second.Player->SetPosition(Vector3f(pos.X, pos.Y, 100.0f));
			it->second.Tick = GetGameTick();
			return channel;
		}

		auto player = PlaySample(pos, sampleId, volume, frequency, false);
		if (player == nullptr) {
			return 0;
		}
		player->SetLooping(true);
		if (it == _loopedSamples.end()) {
			channel = ++_lastLoopChannel;
			it = _loopedSamples.emplace(channel, LoopedSample{}).first;
		}
		it->second.Player = Death::move(player);
		it->second.Tick = GetGameTick();
		return channel;
#else
		return 0;
#endif
	}

	void LevelScriptLoader::StopUnusedLoopedSamples()
	{
		std::int32_t currentTick = GetGameTick();
		for (auto it = _loopedSamples.begin(); it != _loopedSamples.end(); ) {
			if (currentTick - it->second.Tick > 1) {
				if (it->second.Player != nullptr) {
					it->second.Player->Stop();
				}
				it = _loopedSamples.erase(it);
			} else {
				++it;
			}
		}
	}

	bool LevelScriptLoader::IsSampleLoaded(std::int32_t sampleId)
	{
		return ResolveSampleBuffer(sampleId) != nullptr;
	}

	bool LevelScriptLoader::LoadSample(std::int32_t sampleId, StringView path)
	{
#if defined(WITH_AUDIO)
		auto stream = OpenScriptFile(path);
		if (stream == nullptr || !stream->IsValid()) {
			return false;
		}
		auto buffer = std::make_unique<AudioBuffer>(Death::move(stream), path);

		auto it = _scriptSamples.find(sampleId);
		if (it != _scriptSamples.end()) {
			it->second = Death::move(buffer);
		} else {
			_scriptSamples.emplace(sampleId, Death::move(buffer));
		}
		return true;
#else
		return false;
#endif
	}

	void playSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->PlaySample(Vector2f(xPixel, yPixel), sample, volume, frequency, false);
	}
	int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t channel, std::int32_t volume, std::int32_t frequency) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->PlayLoopedSample(Vector2f(xPixel, yPixel), sample, channel, volume, frequency);
	}
	void playPrioritySample(std::int32_t sample) {
		// Priority samples play non-positionally (relative to the listener) at full volume
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->PlaySample(Vector2f(0.0f, 0.0f), sample, 63, 0, true);
	}
	bool isSampleLoaded(std::int32_t sample) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->IsSampleLoaded(sample);
	}
	bool loadSample(std::int32_t sample, const String& filename) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->LoadSample(sample, filename);
	}

	static std::int32_t getSoundFXVolume() {
#if defined(WITH_AUDIO)
		if (ContentResolver::Get().IsHeadless()) {
			return 0;
		}
		return std::int32_t(std::round(std::clamp(PreferencesCache::MasterVolume * PreferencesCache::SfxVolume, 0.0f, 1.0f) * 100.0f));
#else
		return 0;
#endif
	}
	static std::int32_t getMusicVolume() {
#if defined(WITH_AUDIO)
		if (ContentResolver::Get().IsHeadless()) {
			return 0;
		}
		return std::int32_t(std::round(std::clamp(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume, 0.0f, 1.0f) * 100.0f));
#else
		return 0;
#endif
	}
	static bool getSoundEnabled() {
#if defined(WITH_AUDIO)
		return !ContentResolver::Get().IsHeadless();
#else
		return false;
#endif
	}
	static bool getSoundFXActive() {
		return (getSoundFXVolume() > 0);
	}
	static bool getMusicActive() {
		return (getMusicVolume() > 0);
	}

	AudioStreamPlayer* LevelScriptLoader::GetMusicPlayer() const
	{
#if defined(WITH_AUDIO)
		return _levelHandler->_music.get();
#else
		return nullptr;
#endif
	}

	static AudioStreamPlayer* GetActiveMusic() {
		return ScriptLoader::FromActiveContext<LevelScriptLoader>()->GetMusicPlayer();
	}

	static bool GetModState(AudioModuleState& state) {
		AudioStreamPlayer* music = GetActiveMusic();
		return (music != nullptr && music->GetModuleState(state));
	}

	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset) {
#if defined(WITH_AUDIO)
		if (AudioStreamPlayer* music = GetActiveMusic()) {
			music->SetModulePosition(order, row);
		}
#endif
	}
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds) {
		if (AudioStreamPlayer* music = GetActiveMusic()) {
			music->SlideModuleChannelVolume(channel, volume, milliseconds);
		}
	}
	std::int32_t jjGetModOrder() {
		AudioModuleState state;
		return (GetModState(state) ? state.Order : -1);
	}
	std::int32_t jjGetModRow() {
		AudioModuleState state;
		return (GetModState(state) ? state.Row : -1);
	}
	std::int32_t jjGetModTempo() {
		AudioModuleState state;
		return (GetModState(state) ? state.Tempo : -1);
	}
	void jjSetModTempo(std::uint8_t tempo) {
		if (AudioStreamPlayer* music = GetActiveMusic()) {
			music->SetModuleTempo(tempo);
		}
	}
	std::int32_t jjGetModSpeed() {
		AudioModuleState state;
		return (GetModState(state) ? state.Speed : -1);
	}
	void jjSetModSpeed(std::uint8_t speed) {
		if (AudioStreamPlayer* music = GetActiveMusic()) {
			music->SetModuleSpeed(speed);
		}
	}

	void LevelScriptLoader::RegisterLegacySound(asIScriptEngine* engine)
	{
		/**
		 * @brief Loads a music file and starts playing it
		 *
		 * With `forceReload`, the file is started again even if it is already playing. With `temporary`, the music
		 * isn't kept as the level's music, so it plays only until the level's own music is restored, e.g. after the
		 * player dies.
		 * @return Whether the music was loaded or is already playing
		 */
		engine->RegisterGlobalFunction("bool jjMusicLoad(string &in filename, bool forceReload = false, bool temporary = false)", asFUNCTION(jjMusicLoad), asCALL_CDECL);
		/** @brief Stops the current music */
		engine->RegisterGlobalFunction("void jjMusicStop()", asFUNCTION(jjMusicStop), asCALL_CDECL);
		/** @brief Starts playing the current music, or restarts it */
		engine->RegisterGlobalFunction("void jjMusicPlay()", asFUNCTION(jjMusicPlay), asCALL_CDECL);
		/**
		 * @brief Pauses the current music
		 *
		 * @ref jjMusicResume() continues it where it was paused.
		 */
		engine->RegisterGlobalFunction("void jjMusicPause()", asFUNCTION(jjMusicPause), asCALL_CDECL);
		/** @brief Resumes the music paused by @ref jjMusicPause() */
		engine->RegisterGlobalFunction("void jjMusicResume()", asFUNCTION(jjMusicResume), asCALL_CDECL);

		engine->SetDefaultNamespace("SOUND");
		/** @brief Sound samples of the original game, used by @ref jjSample() and related functions */
		engine->RegisterEnum("Sample");
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalFunction("void jjSample(float xPixel, float yPixel, SOUND::Sample sample, int volume = 63, int frequency = 0)", asFUNCTION(playSample), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjSampleLooped(float xPixel, float yPixel, SOUND::Sample sample, int channel, int volume = 63, int frequency = 0)", asFUNCTION(playLoopedSample), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSamplePriority(SOUND::Sample sample)", asFUNCTION(playPrioritySample), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSampleIsLoaded(SOUND::Sample sample)", asFUNCTION(isSampleLoaded), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSampleLoad(SOUND::Sample sample, string& in filename)", asFUNCTION(loadSample), asCALL_CDECL);

		/**
		 * @brief Whether the game produces any audio
		 *
		 * `false` in a build without audio or when no audio is output (a dedicated server).
		 */
		engine->RegisterGlobalFunction("bool get_jjSoundEnabled()", asFUNCTION(getSoundEnabled), asCALL_CDECL);
		/**
		 * @brief Whether sound effects are enabled in the settings
		 *
		 * `false` when their volume is set to 0.
		 */
		engine->RegisterGlobalFunction("bool get_jjSoundFXActive()", asFUNCTION(getSoundFXActive), asCALL_CDECL);
		/**
		 * @brief Whether music is enabled in the settings
		 *
		 * `false` when its volume is set to 0.
		 */
		engine->RegisterGlobalFunction("bool get_jjMusicActive()", asFUNCTION(getMusicActive), asCALL_CDECL);
		/**
		 * @brief Volume of sound effects set in the settings
		 *
		 * From 0 to 100, including the master volume.
		 */
		engine->RegisterGlobalFunction("int get_jjSoundFXVolume()", asFUNCTION(getSoundFXVolume), asCALL_CDECL);
		/**
		 * @brief Volume of music set in the settings
		 *
		 * From 0 to 100, including the master volume.
		 */
		engine->RegisterGlobalFunction("int get_jjMusicVolume()", asFUNCTION(getMusicVolume), asCALL_CDECL);
		/**
		 * @brief Strength of the echo effect, as set by the Echo event
		 *
		 * @partialsupport Currently starts at 0 and changing it has no effect.
		 */
		engine->RegisterGlobalProperty("int jjEcho", &levelEcho);

		engine->RegisterGlobalFunction("void jjSetModPosition(int order, int row, bool reset)", asFUNCTION(jjSetModPosition), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSlideModChannelVolume(int channel, float volume, int milliseconds)", asFUNCTION(jjSlideModChannelVolume), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModOrder()", asFUNCTION(jjGetModOrder), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModRow()", asFUNCTION(jjGetModRow), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModTempo()", asFUNCTION(jjGetModTempo), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetModTempo(uint8 tempo)", asFUNCTION(jjSetModTempo), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModSpeed()", asFUNCTION(jjGetModSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetModSpeed(uint8 speed)", asFUNCTION(jjSetModSpeed), asCALL_CDECL);
	}
}

#endif
