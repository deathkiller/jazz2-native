#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "../ContentResolver.h"
#include "../Rendering/UpscaleRenderPass.h"

#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Audio/AudioStreamPlayer.h"
#include "../../nCine/Input/InputEvents.h"
#if defined(WITH_THREADS)
#	include "../../nCine/Threading/Thread.h"
#	include "../../nCine/Threading/ThreadSync.h"
#endif

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

		/** @brief Default width of viewport (see @ref Rendering::UpscaleRenderPass::DefaultViewWidth) */
		static constexpr std::int32_t DefaultWidth = Rendering::UpscaleRenderPass::DefaultViewWidth;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = Rendering::UpscaleRenderPass::DefaultViewHeight;

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
#	if !defined(NCINE_HAS_NATIVE_AUDIO)
			SfxItem(std::unique_ptr<Stream> stream, StringView path);
#	endif
			explicit SfxItem(StringView path);
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
#if defined(DEATH_TARGET_N64)
		// A full-motion video prepared for this console (see nCine::Backends::N64FullMotionVideo), played in
		// place of the original video by the first OnBeginFrame()
		String _fullMotionVideoPath;
#endif
		std::uint32_t _width, _height;
		// Every frame is decoded at full resolution (the delta encoding requires it), but the texture can
		// be built from every n-th pixel of every n-th row. Uploading a 640x480 frame costs several
		// megabytes of memory traffic per frame, which platforms without the bandwidth for it cannot
		// sustain, so they trade sharpness for a video that plays at its intended speed. It is also what
		// keeps a frame within one hardware texture where that is smaller than the video (see
		// SetupFrameTextureSize()).
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
			@brief Set when the frames of the game's own format are LZ4-compressed (see @ref VideoFormat::CodecDeltaRleLz4)

			The delta commands spend a byte on every pixel that changed, which at 640x480 comes to 1.8 MB a second
			for five seconds in the busiest stretch of the ending - more than the disc drives of the consoles that
			play it can deliver. LZ4 takes more than a third off that, and decompressing a block is close to a plain copy.
		*/
		bool _lz4Payloads;
		/**
			@brief Whether the frames are converted to RGB565 on the CPU instead of uploaded as indices

			Indexed frames are the cheaper choice wherever the hardware can sample them as they are, but the
			PowerVR only supports paletted textures in its twiddled layout, so every upload would pay for a
			bit-interleave over the whole (power-of-two padded) surface - measured at some 36 ms per frame,
			more than everything else in a frame put together. Converting to RGB565 instead keeps the upload
			a plain row copy, and a 16-bit palette makes the conversion one lookup and one store per pixel.
		*/
		bool _convertTo565;
		/**
			@brief Whether the decoder keeps @ref _frame565 current itself, converting only the pixels each frame writes

			Converting every pixel of a 640x480 frame was most of the 11.7 ms the Dreamcast spent on getting one
			to the screen, and the average frame of the intro changes 13% of them - the native format says
			exactly which ones, so only those are converted, while they are decoded. A new palette changes the
			color of every pixel, so the frame that brings one is still converted as a whole.
		*/
		bool _incremental565;
		/** @brief Set when the decoder did not patch @ref _frame565 for the current frame, which is then converted as a whole */
		bool _frame565Stale;
		/** @brief Whole converted frame - kept current by the decoder (see @ref _incremental565), or used where the texture's storage cannot be written directly */
		std::unique_ptr<std::uint16_t[]> _frame565;
		/** @brief One converted row, small enough to stay in the cache between converting it and copying it out */
		std::unique_ptr<std::uint16_t[]> _frameRow;
		std::uint16_t _palette565[256];

		/** @brief 256x1 palette the indexed frames are sampled through */
		std::unique_ptr<nCine::Texture> _paletteTexture;
		/** @brief Downscaled indices, only needed when the frames are larger than the texture */
		std::unique_ptr<std::uint8_t[]> _indexedFrame;
		/** @brief Set when a decoded frame carried a new palette that has not been picked up yet */
		bool _paletteDirty;
		std::uint32_t _uploadPalette[256];
		bool _paletteTextureDirty;

		/** @brief Scratch buffer for a frame whose payload is not contiguous in the read-ahead ring (it wraps around its end, or is larger than the ring) */
		std::unique_ptr<std::uint8_t[]> _framePayload;
		std::uint32_t _framePayloadCapacity;
		/** @brief Scratch buffer the payload of an LZ4-compressed frame is decompressed into */
		std::unique_ptr<std::uint8_t[]> _decompressedPayload;
		std::uint32_t _decompressedPayloadCapacity;

		/**
			@brief Sequential read-ahead of the native format, kept topped up while the video plays

			Each frame is only a few kilobytes, and asking the drive for them one at a time is dominated by
			per-request overhead - measured at 58 ms per frame on average with spikes past half a second, far
			more than the transfer itself. So the file is read in pieces of tens of kilobytes into a ring, and
			the frames are decoded out of it. What matters just as much is WHEN those pieces are read. Reading a
			whole block only once it has run dry stalls the video for as long as the drive takes to deliver it:
			a 256 KB block measured about 200 ms on the PlayStation 2 and the GameCube and a 1 MB one more than
			half a second on the Dreamcast (in their emulators, which model the drives' speed), every one of
			them a visible freeze, lost against the music. The ring is therefore refilled piece by piece long
			before it runs dry:

			- Where there are threads, a reader thread does it. The optical drives of these consoles transfer by
			  DMA while the thread sleeps on the driver, so the frames keep being decoded and presented while the
			  disc is read - the read costs the main thread nothing at all.
			- Elsewhere it happens on rendered frames that do not decode a video frame (at 60 Hz that is most of
			  them, the video runs at 24), one piece at a time, sized to fit into the time such a frame otherwise
			  spends waiting for the display.

			A read only has to happen synchronously when a frame is needed before the read-ahead could deliver
			it, which is the old behaviour, now the exception.

			Where the frames are LZ4-compressed and there is a reader thread, a second thread decompresses them,
			a few frames ahead into slots the main thread takes them from (see @ref AcquireFrame()). These
			consoles have a single core, so this is not about doing two things at once: running below the main
			thread, it only gets the time the main thread spends waiting for the display, which decompressing on
			the main thread took out of the frame instead. It is a thread of its own because the reader thread
			spends most of a busy scene blocked on the drive, and decompressing there would wait for every read.
		*/
		class VideoReader
		{
		public:
			/** @brief Alignment both the destination and the file offset need for the Dreamcast's DMA path */
			static constexpr std::uint32_t Alignment = 32;

			VideoReader();
			~VideoReader();

			VideoReader(const VideoReader&) = delete;
			VideoReader& operator=(const VideoReader&) = delete;

			/** @brief Prepares reading @p file from @p dataOffset into a ring of @p capacity bytes in pieces of @p pieceSize */
			bool Initialize(Stream* file, std::int64_t dataOffset, std::uint32_t capacity, std::uint32_t pieceSize);
			/**
				@brief Starts reading

				@p prefillPieces are read right away, before anything is decoded; the rest follows in the
				background (if @p background is set) or through @ref Pump(). With @p decompressAhead (only together
				with @p background, and only for LZ4-compressed frames) the frames are taken with @ref AcquireFrame().
			*/
			bool Start(std::uint32_t prefillPieces, bool background, bool decompressAhead = false);
			/** @brief Stops the reader thread, if any; reading through this object afterwards is not allowed */
			void Shutdown();

			/**
				@brief Returns @p bytes contiguous bytes at the read position, waiting for them or reading them if needed

				Returns @cpp nullptr @ce if they are not contiguous in the ring (the caller then uses @ref ReadInto())
				or if the file ends first (then @p endOfFile is set).
			*/
			const std::uint8_t* Peek(std::uint32_t bytes, bool& endOfFile);
			/** @brief Copies @p bytes into @p destination and consumes them, waiting for them or reading them if needed */
			bool ReadInto(std::uint8_t* destination, std::uint32_t bytes);
			/** @brief Releases @p bytes at the read position (previously returned by @ref Peek()) back to the reader */
			void Consume(std::uint32_t bytes);
			/** @brief Reads one piece ahead if there is room for it - only for a reader without a thread */
			void Pump();
			/** @brief Whether the reader runs on its own thread */
			bool IsBackground() const {
				return _background;
			}
			/** @brief Whether a thread decompresses the frames ahead, which are then taken with @ref AcquireFrame() */
			bool IsDecompressingAhead() const {
				return _decompressAhead;
			}
#if defined(WITH_THREADS)
			/**
				@brief Takes the next frame's payload, decompressed, waiting for it if it is not ready yet

				Returns `false` at the end of the file or for a malformed frame. The payload stays valid until
				@ref ReleaseFrame().
			*/
			bool AcquireFrame(const std::uint8_t*& payload, std::uint32_t& size);
			/** @brief Hands the slot of the frame taken by @ref AcquireFrame() back to the decompressing thread */
			void ReleaseFrame();
#endif

		private:
			Stream* _file;
			std::unique_ptr<std::uint8_t[]> _allocation;
			std::uint8_t* _data;
			std::uint32_t _capacity;
			std::uint32_t _pieceSize;
			// Positions in the ring; `_filled` bytes from `_readPos` onwards are valid. The producer only ever
			// writes the free part of the ring and the consumer only reads the filled part, so the data itself
			// needs no lock - only the counters that hand the regions over do.
			std::uint32_t _readPos;
			std::uint32_t _writePos;
			std::uint32_t _filled;
			// Bytes before the first frame that were only read to keep the file offset aligned
			std::uint32_t _skip;
			std::int64_t _fileOffset;
			bool _endOfFile;
			bool _background;
			bool _decompressAhead;
#if defined(WITH_THREADS)
			bool _stopRequested;
			Thread _thread;
			Mutex _mutex;
			CondVariable _dataAvailable;
			CondVariable _spaceAvailable;

			/** @brief Frames decompressed ahead of the main thread */
			static constexpr std::int32_t FrameSlotCount = 3;

			struct FrameSlot {
				std::unique_ptr<std::uint8_t[]> Data;
				std::uint32_t Capacity = 0;
				std::uint32_t Size = 0;
			};

			// Slots from `_firstReadySlot` on, `_readySlots` of them, hold decompressed frames; the rest are the
			// decompressing thread's to fill. `_framesEnded` is set when no further frame will become ready.
			Thread _decompressThread;
			CondVariable _frameAvailable;
			CondVariable _slotAvailable;
			FrameSlot _frameSlots[FrameSlotCount];
			std::int32_t _firstReadySlot;
			std::int32_t _readySlots;
			bool _framesEnded;
			// A frame record that is not contiguous in the ring, gathered before it is decompressed
			std::unique_ptr<std::uint8_t[]> _record;
			std::uint32_t _recordCapacity;
#	if defined(DEATH_TARGET_DREAMCAST)
			// The decompressing thread goes one below it (see DecompressThreadFunc)
			std::int32_t _mainThreadPriority;
#	endif

			static void ThreadFunc(void* arg);
#	if defined(WITH_LZ4)
			static void DecompressThreadFunc(void* arg);
			/** @brief Takes the next frame record out of the ring and decompresses it into @p slot */
			bool DecompressNextFrame(FrameSlot& slot);
#	endif
#endif

			/** @brief Reads the next piece into the free part of the ring; the caller has made sure there is room */
			void ReadPiece();
			/**
				@brief Waits until @p bytes are filled (or the file ended), reading them on this thread if there is no other

				Returns how many bytes are filled, which is fewer than asked for only at the end of the file.
			*/
			std::uint32_t WaitForData(std::uint32_t bytes);
		};

		/** @brief Reader of the native format, declared after the file it reads so that it stops first */
		VideoReader _videoReader;

		BitArray _pressedKeys;
		std::uint32_t _pressedActions;
		bool _decodingFailed;

		void Initialize(StringView path);
		bool LoadCinematicsFromFile(StringView path);
		bool LoadSfxList(StringView path);
		void PrepareNextFrame();
		bool LoadLegacyVideo(std::unique_ptr<Stream>&& s, StringView path);
		bool LoadNativeVideo(std::unique_ptr<Stream>&& s, StringView path);
		/** @brief Derives @ref _textureWidth / @ref _textureHeight from the platform's starting @p downscale */
		void SetupFrameTextureSize(std::uint32_t downscale);
		/** @brief Decodes one frame of the original format into @ref _buffer */
		void DecodeFrameLegacy();
		/** @brief Decodes one frame of the game's own format into @ref _buffer */
		void DecodeFrameNative();
		/** @brief Executes one frame's (uncompressed) payload of the game's own format: its palette, then its commands */
		void DecodePayload(const std::uint8_t* src, const std::uint8_t* srcEnd);
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