#include "EventPreviews.h"

#if defined(WITH_EDITOR)

#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"
#include "../../Resources.h"

#if defined(WITH_AUDIO)
#	include "../../../nCine/Audio/AudioBufferPlayer.h"
#endif

#include <algorithm>

using namespace Death::Containers::Literals;
using namespace Jazz2::Resources;

namespace Jazz2::Editor
{
	namespace
	{
		// The sound an event plays and how loud, as the game plays it
		bool GetSound(const Formats::EventRecord& event, StringView& metadata, StringView& identifier, float& gain)
		{
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
			// The parameters as the game reads them, an event pack may mean something else by them
			if (descriptor == nullptr || EventCatalog::IsExtension(descriptor)) {
				return false;
			}
			switch (EventType(event.Type)) {
				case EventType::AreaAmbientSound:
					// AmbientSound::OnActivatedAsync()
					switch (event.Params[0]) {
						case 0: identifier = "AmbientWind"_s; break;
						case 1: identifier = "AmbientFire"_s; break;
						case 2: identifier = "AmbientScienceNoise"_s; break;
						default: return false;
					}
					metadata = "Common/AmbientSound"_s;
					gain = 0.4f * (event.Params[1] / 255.0f);
					return true;
				default:
					return false;
			}
		}
	}

	bool EventPreviews::HasSound(const Formats::EventRecord& event)
	{
		StringView metadata, identifier;
		float gain;
		return GetSound(event, metadata, identifier, gain);
	}

	void EventPreviews::UpdateSound(const Formats::EventRecord* event)
	{
#if defined(WITH_AUDIO)
		StringView metadataPath, identifier;
		float gain = 0.0f;
		if (event == nullptr || !GetSound(*event, metadataPath, identifier, gain)) {
			StopSound();
			return;
		}
		// As loud as the game plays it, LevelHandler::PlaySfx(), so not at all while sounds are muted
		gain *= PreferencesCache::MasterVolume * PreferencesCache::SfxVolume;
		if (gain <= 0.0f) {
			StopSound();
			return;
		}

		// The same sound goes on, only as loud as it's set now; one that can't be played, or that the audio device stopped
		// as the output went away, isn't tried again every frame, only once the event is selected again
		if (identifier == _soundIdentifier) {
			if (_sound != nullptr && gain != _soundGain) {
				_sound->setGain(gain);
				_soundGain = gain;
			}
			return;
		}
		StopSound();
		_soundIdentifier = identifier;
		_soundGain = gain;

		ContentResolver& resolver = ContentResolver::Get();
		Metadata* metadata = resolver.RequestMetadata(metadataPath);
		if (metadata == nullptr) {
			return;
		}
		// Kept loaded as the sprites are, loading the level to play it would release the samples while they're heard
		if (std::find(_pinned.begin(), _pinned.end(), metadata) == _pinned.end()) {
			resolver.PinMetadata(metadata, true);
			_pinned.push_back(metadata);
		}
		auto it = metadata->Sounds.find(String::nullTerminatedView(identifier));
		// The samples may not be loaded yet (see ContentResolver::ResolveSound)
		if (it == metadata->Sounds.end() || !resolver.ResolveSound(it->second) || it->second.Buffers.empty()) {
			return;
		}

		// Over and over as in the game, but the same wherever the event is, as it's the one being edited
		_sound = std::make_shared<nCine::AudioBufferPlayer>(&it->second.Buffers[0]->Buffer);
		_sound->setSourceRelative(true);
		_sound->setPosition(nCine::Vector3f::Zero);
		_sound->setGain(gain);
		_sound->setLooping(true);
		_sound->play();
#endif
	}

	void EventPreviews::StopSound()
	{
#if defined(WITH_AUDIO)
		if (_sound != nullptr) {
			_sound->stop();
		}
#endif
		_sound = nullptr;
		_soundIdentifier = {};
		_soundGain = 0.0f;
	}
}

#endif
