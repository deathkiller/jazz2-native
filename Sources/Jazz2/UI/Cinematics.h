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
		static constexpr int DefaultWidth = 720;
		static constexpr int DefaultHeight = 405;

		static constexpr std::uint8_t SfxListVersion = 1;

		Cinematics(IRootController* root, const StringView path, const std::function<bool(IRootController*, bool)>& callback);
		Cinematics(IRootController* root, const StringView path, std::function<bool(IRootController*, bool)>&& callback);
		~Cinematics() override;

		void OnBeginFrame() override;
		void OnInitializeViewport(int width, int height) override;

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

		IRootController* _root;
		UI::UpscaleRenderPass _upscalePass;
		std::unique_ptr<CinematicsCanvas> _canvas;
		std::unique_ptr<AudioStreamPlayer> _music;
		SmallVector<SfxItem> _sfxSamples;
		SmallVector<SfxPlaylistItem> _sfxPlaylist;
		std::function<bool(IRootController*, bool)> _callback;
		uint32_t _width, _height;
		float _frameDelay, _frameProgress;
		int _frameIndex;
		int _framesLeft;
		std::unique_ptr<Texture> _texture;
		std::unique_ptr<uint8_t[]> _buffer;
		std::unique_ptr<uint8_t[]> _lastBuffer;
		std::unique_ptr<uint32_t[]> _currentFrame;
		uint32_t _palette[256];
		MemoryStream _compressedStreams[4];
		DeflateStream _decompressedStreams[4];

		BitArray _pressedKeys;
		uint32_t _pressedActions;

		void Initialize(const StringView path);
		bool LoadCinematicsFromFile(const StringView path);
		bool LoadSfxList(const StringView path);
		void PrepareNextFrame();
		void Read(int streamIndex, void* buffer, uint32_t bytes);
		void UpdatePressedActions();

		template<typename T>
		inline T ReadValue(int streamIndex) {
			T buffer;
			Read(streamIndex, &buffer, sizeof(T));
			return buffer;
		}
	};
}