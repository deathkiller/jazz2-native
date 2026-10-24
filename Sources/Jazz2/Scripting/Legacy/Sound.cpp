#if defined(WITH_ANGELSCRIPT)

#include "Sound.h"
#include "Internal.h"

#include "../../Compatibility/AnimSetMapping.h"

#include "../../../nCine/Audio/AudioBuffer.h"
#include "../../../nCine/Audio/AudioBufferPlayer.h"
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
		_this->_levelHandler->BeginPlayMusic(filename, !temporary, forceReload);
#endif
		return false;
	}
	void LevelScriptLoader::jjMusicStop() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPlay() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->play();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPause() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
#endif
	}
	void LevelScriptLoader::jjMusicResume() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr && _this->_levelHandler->_music->isPaused()) {
			_this->_levelHandler->_music->play();
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

	std::shared_ptr<AudioBufferPlayer> LevelScriptLoader::PlaySample(const Vector2f& pos, std::int32_t sampleId, std::int32_t volume, std::int32_t frequency, bool sourceRelative, std::int32_t loopChannel)
	{
#if defined(WITH_AUDIO)
		AudioBuffer* buffer = ResolveSampleBuffer(sampleId);
		if (buffer == nullptr) {
			return nullptr;
		}

		// JJ2 volume is roughly 0-64 (0x40 = full); frequency 0 means the sample's natural rate
		float gain = (volume <= 0 ? 0.0f : volume / 64.0f);
		float pitch = (frequency <= 0 || buffer->frequency() <= 0 ? 1.0f : (float)frequency / (float)buffer->frequency());

		auto player = _levelHandler->PlaySfx(nullptr, {}, buffer, Vector3f(pos.X, pos.Y, 0.0f), sourceRelative, gain, pitch, SfxFlags::None);
		if (player != nullptr && loopChannel >= 0) {
			player->setLooping(true);

			auto prev = _loopedSamples.find(loopChannel);
			if (prev != _loopedSamples.end()) {
				if (prev->second != nullptr) {
					prev->second->stop();
				}
				prev->second = player;
			} else {
				_loopedSamples.emplace(loopChannel, player);
			}
		}
		return player;
#else
		return nullptr;
#endif
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
		owner->PlaySample(Vector2f(xPixel, yPixel), sample, volume, frequency, false, -1);
	}
	int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t channel, std::int32_t volume, std::int32_t frequency) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->PlaySample(Vector2f(xPixel, yPixel), sample, volume, frequency, false, channel);
		return channel;
	}
	void playPrioritySample(std::int32_t sample) {
		// Priority samples play non-positionally (relative to the listener) at full volume
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->PlaySample(Vector2f(0.0f, 0.0f), sample, 63, 0, true, -1);
	}
	bool isSampleLoaded(std::int32_t sample) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->IsSampleLoaded(sample);
	}
	bool loadSample(std::int32_t sample, const String& filename) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->LoadSample(sample, filename);
	}

	// TODO

	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset) {
		noop();
	}
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds) {
		noop();
	}
	std::int32_t jjGetModOrder() {
		noop();
		return 0;
	}
	std::int32_t jjGetModRow() {
		noop();
		return 0;
	}
	std::int32_t jjGetModTempo() {
		noop();
		return 0;
	}
	void jjSetModTempo(std::uint8_t speed) {
		noop();
	}
	std::int32_t jjGetModSpeed() {
		noop();
		return 0;
	}
	void jjSetModSpeed(std::uint8_t speed) {
		noop();
	}

	void LevelScriptLoader::RegisterLegacySound(asIScriptEngine* engine)
	{
		engine->RegisterGlobalFunction("bool jjMusicLoad(string &in filename, bool forceReload = false, bool temporary = false)", asFUNCTION(jjMusicLoad), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicStop()", asFUNCTION(jjMusicStop), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicPlay()", asFUNCTION(jjMusicPlay), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicPause()", asFUNCTION(jjMusicPause), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicResume()", asFUNCTION(jjMusicResume), asCALL_CDECL);

		engine->SetDefaultNamespace("SOUND");
		engine->RegisterEnum("Sample");
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalFunction("void jjSample(float xPixel, float yPixel, SOUND::Sample sample, int volume = 63, int frequency = 0)", asFUNCTION(playSample), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjSampleLooped(float xPixel, float yPixel, SOUND::Sample sample, int channel, int volume = 63, int frequency = 0)", asFUNCTION(playLoopedSample), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSamplePriority(SOUND::Sample sample)", asFUNCTION(playPrioritySample), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSampleIsLoaded(SOUND::Sample sample)", asFUNCTION(isSampleLoaded), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSampleLoad(SOUND::Sample sample, string& in filename)", asFUNCTION(loadSample), asCALL_CDECL);

		engine->RegisterGlobalProperty("const bool jjSoundEnabled", &soundEnabled);
		engine->RegisterGlobalProperty("const bool jjSoundFXActive", &soundFXActive);
		engine->RegisterGlobalProperty("const bool jjMusicActive", &musicActive);
		engine->RegisterGlobalProperty("const int jjSoundFXVolume", &soundFXVolume);
		engine->RegisterGlobalProperty("const int jjMusicVolume", &musicVolume);
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
