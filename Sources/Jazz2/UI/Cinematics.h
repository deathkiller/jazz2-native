#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "../ContentResolver.h"
#include "../Rendering/UpscaleRenderPass.h"

#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Audio/AudioStreamPlayer.h"
#include "../../nCine/Input/InputEvents.h"

#include <Containers/Pair.h>
#include <Containers/SmallVector.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::IO;

namespace Jazz2::UI
{
	/**
		@brief Handler that plays a cinematic video
		
		State handler that plays the original intro and ending cutscenes, decompressing the custom frame format,
		applying the palette, and synchronizing the accompanying music and sound effects. Playback can be skipped,
		after which the supplied callback is invoked.
	*/
	class Cinematics : public IStateHandler
	{
	public:
		/** @{ @name Constants */

		/** @brief Default width of viewport */
		static constexpr std::int32_t DefaultWidth = 720;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = 405;

#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr std::uint8_t SfxListVersion = 1;
#endif

		/** @} */

		/**
		 * @brief Creates a new instance and starts playing the specified cinematic
		 *
		 * @param root      Root controller
		 * @param path      Path to the cinematic to play
		 * @param callback  Called when the playback finishes or is skipped
		 */
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
		// Every frame is decoded at full resolution (the delta encoding requires it), but the texture can
		// be built from every n-th pixel of every n-th row. Uploading a 640x480 frame costs several
		// megabytes of memory traffic per frame, which platforms without the bandwidth for it cannot
		// sustain, so they trade sharpness for a video that plays at its intended speed.
		std::uint32_t _videoDownscale;
		std::uint32_t _textureWidth, _textureHeight;
		float _frameDelay, _frameProgress;
		std::int32_t _frameIndex;
		std::int32_t _framesLeft;
		// Double-buffered: uploading into the texture the GPU may still sample from the previous
		// frame forces a driver sync stall (costly on tiled GPUs), so uploads ping-pong
		std::unique_ptr<Texture> _textures[2];
		std::int32_t _textureIndex;
		std::unique_ptr<std::uint8_t[]> _buffer;
		std::unique_ptr<std::uint8_t[]> _lastBuffer;
		std::uint32_t _palette[256];
		
		/**
			@brief Sliding window over the video file, shared by the four compressed streams

			Reading slices straight from the file dominates playback: on an optical drive one read costs far
			more than decoding a frame. The four streams' chunks are interleaved in file order and consumed at
			the same rate, so at any moment all four read within a few kilobytes of each other and one window
			covers them all; it keeps a margin behind the latest request so a stream that trails slightly
			still hits it. The window is deliberately small: refilling it is the one part of playback that can
			stall a frame, so it is better to read a little often than a lot rarely.
		*/
		class FileWindow
		{
		public:
			static constexpr std::int32_t WindowSize = 64 * 1024;
			// Kept behind a refilled window for the streams that trail the one that triggered it
			static constexpr std::int32_t WindowMargin = 16 * 1024;

			void Initialize(Stream* file, std::int64_t startOffset);
			/** @brief Reads through the window, refilling it when the range is not covered */
			std::int32_t Read(std::int64_t offset, void* destination, std::int32_t bytes);

		private:
			Stream* _file = nullptr;
			std::unique_ptr<std::uint8_t[]> _data;
			// File offset of _data[0] and the number of valid bytes from there
			std::int64_t _start = 0;
			std::int32_t _length = 0;
		};

		/**
			@brief Reads one of the four interleaved compressed streams of a \".j2v\" file from the source file

			The file stores the streams as interleaved chunks; this stream walks the chunk list of one of
			them on demand through the shared window, so the whole video never has to be buffered into memory.
		*/
		class ChunkedStream : public Stream
		{
		public:
			ChunkedStream();

			void Initialize(FileWindow* window, SmallVector<Pair<std::int64_t, std::int32_t>, 0>&& chunks, std::int64_t initialOffset);

			void Dispose() override;
			std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
			std::int64_t GetPosition() const override;
			std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
			std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
			bool Flush() override;
			bool IsValid() override;
			std::int64_t GetSize() const override;
			std::int64_t SetSize(std::int64_t size) override;

		private:
			FileWindow* _window;
			SmallVector<Pair<std::int64_t, std::int32_t>, 0> _chunks;
			std::int64_t _size;
			std::int64_t _position;
			// Cursor of the last read (chunk index and its start in the decompressed stream), so the
			// mostly-sequential reads don't rescan the chunk list from the beginning every time
			std::size_t _chunkIndex;
			std::int64_t _chunkStart;
		};

		/**
			@brief Buffers the decompressed output of one of the four streams

			The frame decoding reads the streams one byte at a time, and every such read would otherwise
			reach the decompressor - which inflates into a one-byte window, paying the full setup for each
			of the hundreds of thousands of bytes in a frame. Inflating into a block and handing out bytes
			from it makes the decoding an order of magnitude cheaper.
		*/
		struct StreamBuffer {
			static constexpr std::int32_t Capacity = 8 * 1024;

			Stream* Source = nullptr;
			std::unique_ptr<std::uint8_t[]> Data;
			std::int32_t Position = 0;
			std::int32_t Length = 0;

			void Initialize(Stream* source);
			/** @brief Refills the block; returns the number of bytes available afterwards */
			std::int32_t Refill();
		};

		// The file must outlive the streams reading from it, so it is declared first
		std::unique_ptr<Stream> _videoFile;
		FileWindow _fileWindow;
		ChunkedStream _compressedStreams[4];
		Compression::DeflateStream _decompressedStreams[4];
		StreamBuffer _streamBuffers[4];

		/** @brief Set when the file uses the game's own format instead of the original one */
		bool _nativeFormat;
		/**
			@brief Set when frames are uploaded as palette indices instead of expanded to texels

			Saves both the per-pixel palette lookup and three quarters of the upload - the palette is applied
			by the sampler through @ref _paletteTexture instead.
		*/
		bool _indexedUpload;
		/**
			@brief Whether the frames are converted to RGB565 on the CPU instead of uploaded as indices

			Indexed frames are the cheaper choice wherever the hardware can sample them as they are, but the
			PowerVR only supports paletted textures in its twiddled layout, so every upload would pay for a
			bit-interleave over the whole (power-of-two padded) surface - measured at some 36 ms per frame,
			more than everything else in a frame put together. Converting to RGB565 instead keeps the upload
			a plain row copy, and a 16-bit palette makes the conversion one lookup and one store per pixel.
		*/
		bool _convertTo565;
		std::unique_ptr<std::uint16_t[]> _frame565;
		std::uint16_t _palette565[256];

		/** @brief 256x1 palette the indexed frames are sampled through */
		std::unique_ptr<nCine::Texture> _paletteTexture;
		/** @brief Downscaled indices, only needed when the frames are larger than the texture */
		std::unique_ptr<std::uint8_t[]> _indexedFrame;
		/**
			@brief Palette the decoder is currently using

			Owned by whoever decodes - which is the prefetch thread when one is running, so the main thread
			never touches it. Frames carry their palette along, and the main thread keeps its own copy below.
		*/
		bool _paletteDirty;
		std::uint32_t _uploadPalette[256];
		bool _paletteTextureDirty;

		/** @brief Scratch buffer for a frame whose payload does not fit in the block buffer (never happens in practice) */
		std::unique_ptr<std::uint8_t[]> _framePayload;
		std::uint32_t _framePayloadCapacity;

		/**
			@brief Sequential read-ahead buffer for the native format

			Each frame is only a few kilobytes, and asking the drive for them one at a time is dominated by
			per-request overhead - measured at 58 ms per frame on average with spikes past half a second, far
			more than the transfer itself. Reading in large blocks amortizes that: one request covers several
			seconds of video, and the frames are then served from memory.
		*/
		static constexpr std::uint32_t VideoBlockCapacity =
#if defined(DEATH_TARGET_DREAMCAST)
			1024 * 1024;
#else
			256 * 1024;
#endif
		/** @brief Alignment both the destination and the file offset need for the DMA path (see EnsureBuffered) */
		static constexpr std::uint32_t VideoBlockAlignment = 32;
		std::unique_ptr<std::uint8_t[]> _blockAllocation;
		std::uint8_t* _blockBuffer;
		std::uint32_t _blockSize;
		std::uint32_t _blockOffset;
		std::int64_t _blockFilePosition;

		/** @brief Makes sure at least @p bytes are buffered, refilling from the file when needed */
		bool EnsureBuffered(std::uint32_t bytes);

		BitArray _pressedKeys;
		std::uint32_t _pressedActions;
		bool _decodingFailed;

		void Initialize(StringView path);
		bool LoadCinematicsFromFile(StringView path);
		bool LoadSfxList(StringView path);
		void PrepareNextFrame(bool prepareTexture = true);
		bool LoadLegacyVideo(std::unique_ptr<Stream>&& s, StringView path);
		bool LoadNativeVideo(std::unique_ptr<Stream>&& s, StringView path);
		/** @brief Decodes one frame of the original format into @ref _buffer */
		void DecodeFrameLegacy();
		/** @brief Decodes one frame of the game's own format into @ref _buffer */
		void DecodeFrameNative();
		/** @brief Uploads the given indices, and the palette if it changed */
		void ApplyPaletteAndUpload(const std::uint8_t* indices);
		/** @brief Starts any sounds scheduled for the current frame */
		void PlayFrameSounds();
		void Read(std::int32_t streamIndex, void* buffer, std::uint32_t bytes);
		/** @brief Discards the given number of bytes of a stream */
		void Skip(std::int32_t streamIndex, std::uint32_t bytes);
		void UpdatePressedActions();

		template<typename T>
		inline T ReadValue(std::int32_t streamIndex) {
			T buffer;
			Read(streamIndex, &buffer, sizeof(T));
			return buffer;
		}

		/** @brief Reads a single byte, the hot path of the frame decoding */
		inline std::uint8_t ReadByte(std::int32_t streamIndex) {
			StreamBuffer& buffer = _streamBuffers[streamIndex];
			if DEATH_LIKELY(buffer.Position < buffer.Length) {
				return buffer.Data[buffer.Position++];
			}
			return ReadValue<std::uint8_t>(streamIndex);
		}
	};
}