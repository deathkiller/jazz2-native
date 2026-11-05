#pragma once

#include "../../Main.h"

#if defined(WITH_OPENMPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioReader.h"

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("libopenmpt/libopenmpt.h")
#		define __HAS_LOCAL_LIBOPENMPT
#	endif
#endif
#if defined(__HAS_LOCAL_LIBOPENMPT)
#	include "libopenmpt/libopenmpt.h"
#	if __has_include("libopenmpt/libopenmpt_ext.h")
#		include "libopenmpt/libopenmpt_ext.h"
#		define NCINE_HAS_OPENMPT_EXT
#	endif
#else
#	include <libopenmpt.h>
#	if defined(__has_include)
#		if __has_include(<libopenmpt_ext.h>)
#			include <libopenmpt_ext.h>
#			define NCINE_HAS_OPENMPT_EXT
#		endif
#	endif
#endif

#if defined(OPENMPT_API_VERSION_AT_LEAST)
#	if OPENMPT_API_VERSION_AT_LEAST(0, 7, 0)
#		define NCINE_HAS_OPENMPT_0_7
#	endif
#endif
#if defined(NCINE_HAS_OPENMPT_EXT) && defined(NCINE_HAS_OPENMPT_0_7)
#	define NCINE_HAS_OPENMPT_TEMPO2
#endif

#include <memory>

#include <Containers/SmallVector.h>
#include <IO/Stream.h>
#include <Threading/Spinlock.h>

namespace nCine
{
	class IFile;

	/**
		@brief Audio reader for tracker module formats, backed by `libopenmpt`
	*/
	class AudioReaderMpt : public IAudioReader
	{
	public:
		AudioReaderMpt(std::unique_ptr<Death::IO::Stream> fileHandle, std::int32_t frequency);
		~AudioReaderMpt();

		AudioReaderMpt(const AudioReaderMpt&) = delete;
		AudioReaderMpt& operator=(const AudioReaderMpt&) = delete;

		std::int32_t Read(void* buffer, std::int32_t bufferSize) const override;
		void Rewind() const override;
		void SetLooping(bool value) override;

		std::int64_t GetDecodedFrames() const override;
		bool GetModuleStateAt(std::int64_t frame, AudioModuleState& state) const override;
		bool CanSetModulePosition() const override;
		bool SetModulePosition(std::int32_t order, std::int32_t row) override;
		bool SetModuleSpeed(std::int32_t speed) override;
		bool SetModuleTempo(std::int32_t tempo) override;
		bool SlideModuleChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds) override;

	private:
		/** @brief Frames decoded at once, the state of the module is recorded between them */
		static constexpr std::int32_t SliceFrames = 256;
		/** @brief Number of recorded states, enough for every chunk a stream keeps queued */
		static constexpr std::int32_t TimelineSize = 128;

		struct TimelineEntry {
			std::int64_t Frame;
			AudioModuleState State;
		};

		struct ChannelSlide {
			std::int32_t Channel;
			float From;
			float To;
			std::int64_t Length;
			std::int64_t Elapsed;
			bool Started;
		};

		/** @brief Audio file handle */
		std::unique_ptr<Death::IO::Stream> _fileHandle;
		std::int32_t _frequency;
		openmpt_module* _module;
#if defined(NCINE_HAS_OPENMPT_EXT)
		openmpt_module_ext* _moduleExt;
		openmpt_module_ext_interface_interactive _interactive;
		bool _hasInteractive;
#	if defined(NCINE_HAS_OPENMPT_TEMPO2)
		openmpt_module_ext_interface_interactive3 _interactive3;
		bool _hasInteractive3;
#	endif
#endif

		// Shared with the threads that query or change the module, everything below is guarded by `_lock`
		mutable Death::Threading::Spinlock _lock;
		mutable std::int64_t _decodedFrames;
		mutable TimelineEntry _timeline[TimelineSize];
		mutable std::int32_t _timelineNext;
		mutable std::int32_t _timelineCount;
		mutable bool _pendingPosition;
		mutable std::int32_t _pendingOrder;
		mutable std::int32_t _pendingRow;
		mutable std::int32_t _pendingSpeed;
		mutable std::int32_t _pendingTempo;
		mutable Death::Containers::SmallVector<ChannelSlide, 4> _pendingSlides;
		// Used only by the reading thread
		mutable Death::Containers::SmallVector<ChannelSlide, 4> _activeSlides;
		mutable std::int32_t _recordedOrder;
		mutable std::int32_t _recordedRow;

		void ApplyPendingChanges() const;
		void AdvanceSlides(std::int32_t frames) const;
		void RecordState(std::int64_t frame, bool force) const;
		void QueryPosition(AudioModuleState& state) const;
		void QueryTiming(AudioModuleState& state) const;

#if defined(WITH_OPENMPT_DYNAMIC)
		using openmpt_module_create2_t = decltype(openmpt_module_create2);
		using openmpt_module_destroy_t = decltype(openmpt_module_destroy);
		using openmpt_module_read_interleaved_stereo_t = decltype(openmpt_module_read_interleaved_stereo);
		using openmpt_module_set_position_seconds_t = decltype(openmpt_module_set_position_seconds);
		using openmpt_module_set_repeat_count_t = decltype(openmpt_module_set_repeat_count);
		using openmpt_module_set_position_order_row_t = decltype(openmpt_module_set_position_order_row);
		using openmpt_module_get_current_int_t = std::int32_t(openmpt_module*);
		using openmpt_module_get_current_tempo2_t = double(openmpt_module*);
#	if defined(NCINE_HAS_OPENMPT_EXT)
		using openmpt_module_ext_create_t = decltype(openmpt_module_ext_create);
		using openmpt_module_ext_destroy_t = decltype(openmpt_module_ext_destroy);
		using openmpt_module_ext_get_module_t = decltype(openmpt_module_ext_get_module);
		using openmpt_module_ext_get_interface_t = decltype(openmpt_module_ext_get_interface);
#	endif

		static void* _openmptLib;
		static bool _openmptLibInitialized;
		static openmpt_module_create2_t* _openmpt_module_create2;
		static openmpt_module_destroy_t* _openmpt_module_destroy;
		static openmpt_module_read_interleaved_stereo_t* _openmpt_module_read_interleaved_stereo;
		static openmpt_module_set_position_seconds_t* _openmpt_module_set_position_seconds;
		static openmpt_module_set_repeat_count_t* _openmpt_module_set_repeat_count;
		// Optional, the module can't be controlled without them
		static openmpt_module_set_position_order_row_t* _openmpt_module_set_position_order_row;
		static openmpt_module_get_current_int_t* _openmpt_module_get_current_order;
		static openmpt_module_get_current_int_t* _openmpt_module_get_current_row;
		static openmpt_module_get_current_int_t* _openmpt_module_get_current_speed;
		static openmpt_module_get_current_int_t* _openmpt_module_get_current_tempo;
		static openmpt_module_get_current_tempo2_t* _openmpt_module_get_current_tempo2;
		static openmpt_module_get_current_int_t* _openmpt_module_get_num_channels;
#	if defined(NCINE_HAS_OPENMPT_EXT)
		static openmpt_module_ext_create_t* _openmpt_module_ext_create;
		static openmpt_module_ext_destroy_t* _openmpt_module_ext_destroy;
		static openmpt_module_ext_get_module_t* _openmpt_module_ext_get_module;
		static openmpt_module_ext_get_interface_t* _openmpt_module_ext_get_interface;
#	endif

		static bool TryLoadLibrary();
#endif

		static std::size_t StreamRead(void* stream, void* dst, std::size_t bytes);
		// `int` rather than `std::int32_t`, because this has to match libopenmpt's function-pointer type
		// exactly, and the two are distinct types wherever `std::int32_t` is `long int` (MIPS, among others)
		static int StreamSeek(void* stream, std::int64_t offset, int whence);
		static std::int64_t StreamTell(void* stream);

		static void InternalLog(const char* message, void* user);
	};
}

#endif
