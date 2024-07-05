#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "UpscaleRenderPass.h"
#include "../ContentResolver.h"

#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Shader.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Audio/AudioStreamPlayer.h"
#include "../../nCine/Input/InputEvents.h"

#include <functional>

#include <IO/DeflateStream.h>
#include <IO/MemoryStream.h>

using namespace Death::IO;

namespace Jazz2::UI
{
	class Cinematics : public IStateHandler
	{
	public:
		static constexpr std::int32_t DefaultWidth = 720;
		static constexpr std::int32_t DefaultHeight = 405;

		static constexpr std::uint8_t SfxListVersion = 1;

		Cinematics(IRootController* root, const StringView path, const std::function<bool(IRootController*, bool)>& callback);
		Cinematics(IRootController* root, const StringView path, std::function<bool(IRootController*, bool)>&& callback);
		~Cinematics() override;

		void OnBeginFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

	private:
		class CinematicsCanvas : public SceneNode
		{
		public:
			CinematicsCanvas(Cinematics* owner)
				: _owner(owner)
			{
				Initialize();
			}

			void Initialize();

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			Cinematics* _owner;
			RenderCommand _renderCommand;
		};

#if defined(WITH_AUDIO)
		struct SfxItem {
			std::unique_ptr<AudioBuffer> Buffer;

			SfxItem();
			SfxItem(std::unique_ptr<Stream> stream, const StringView path);
		};

		struct SfxPlaylistItem {
			std::uint32_t Frame;
			std::uint16_t Sample;
			float Gain;
			float Panning;
			std::unique_ptr<AudioBufferPlayer> CurrentPlayer;
		};
#endif

		IRootController* _root;
		UI::UpscaleRenderPass _upscalePass;
		std::unique_ptr<CinematicsCanvas> _canvas;
#if defined(WITH_AUDIO)
		std::unique_ptr<AudioStreamPlayer> _music;
		SmallVector<SfxItem> _sfxSamples;
		SmallVector<SfxPlaylistItem> _sfxPlaylist;
#endif
		std::function<bool(IRootController*, bool)> _callback;
		std::uint32_t _width, _height;
		float _frameDelay, _frameProgress;
		std::int32_t _frameIndex;
		std::int32_t _framesLeft;
		std::unique_ptr<Texture> _texture;
		std::unique_ptr<std::uint8_t[]> _buffer;
		std::unique_ptr<std::uint8_t[]> _lastBuffer;
		std::unique_ptr<std::uint32_t[]> _currentFrame;
		std::uint32_t _palette[256];
		MemoryStream _compressedStreams[4];
		DeflateStream _decompressedStreams[4];

		BitArray _pressedKeys;
		std::uint32_t _pressedActions;

		void Initialize(const StringView path);
		bool LoadCinematicsFromFile(const StringView path);
		bool LoadSfxList(const StringView path);
		void PrepareNextFrame();
		void Read(std::int32_t streamIndex, void* buffer, std::uint32_t bytes);
		void UpdatePressedActions();

		template<typename T>
		inline T ReadValue(std::int32_t streamIndex) {
			T buffer;
			Read(streamIndex, &buffer, sizeof(T));
			return buffer;
		}
	};
}