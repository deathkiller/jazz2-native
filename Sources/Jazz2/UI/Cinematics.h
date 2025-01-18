#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "../ContentResolver.h"
#include "../Rendering/UpscaleRenderPass.h"

#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Shader.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Audio/AudioStreamPlayer.h"
#include "../../nCine/Input/InputEvents.h"

#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::IO;

namespace Jazz2::UI
{
	/** @brief Handler that plays a cinematic video */
	class Cinematics : public IStateHandler
	{
	public:
		/** @brief Default width of viewport */
		static constexpr std::int32_t DefaultWidth = 720;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = 405;

#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr std::uint8_t SfxListVersion = 1;
#endif

		Cinematics(IRootController* root, StringView path, Function<bool(IRootController*, bool)>&& callback);
		~Cinematics() override;

		Vector2i GetViewSize() const override;

		void OnBeginFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
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
#endif

#if defined(WITH_AUDIO)
		struct SfxItem {
			std::unique_ptr<AudioBuffer> Buffer;

			SfxItem();
			SfxItem(std::unique_ptr<Stream> stream, StringView path);
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
		Rendering::UpscaleRenderPass _upscalePass;
		std::unique_ptr<CinematicsCanvas> _canvas;
#if defined(WITH_AUDIO)
		std::unique_ptr<AudioStreamPlayer> _music;
		SmallVector<SfxItem> _sfxSamples;
		SmallVector<SfxPlaylistItem> _sfxPlaylist;
#endif
		Function<bool(IRootController*, bool)> _callback;
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
		Compression::DeflateStream _decompressedStreams[4];

		BitArray _pressedKeys;
		std::uint32_t _pressedActions;

		void Initialize(StringView path);
		bool LoadCinematicsFromFile(StringView path);
		bool LoadSfxList(StringView path);
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