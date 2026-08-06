#pragma once

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	class IRhiCapabilities;
}

namespace nCine::RHI::GS
{
	/**
		@brief Debug-output and object-labelling stub of the GS backend

		The Graphics Synthesizer is driven by raw GIF packets and has no device-side debug facility of any
		kind - not even an error register a draw could be checked against - so every entry point is a no-op.
		The class exists only to satisfy the `RHI::Debug` contract alias (debug groups, message insertion and
		object labels used by the render pipeline). What debugging there is comes from outside: PCSX2's GS
		dumps and its EE console, which the engine reaches through the ordinary logger.
	*/
	class GsDebug
	{
	public:
		/** @brief Object types that can be labelled (values are irrelevant for the GS backend) */
		enum class LabelTypes
		{
			Buffer,
			Shader,
			Program,
			VertexArray,
			Query,
			ProgramPipeline,
			TransformFeedback,
			Sampler,
			Texture,
			RenderBuffer,
			FrameBuffer
		};

		/**
			@brief RAII scope for a debug message group (a no-op for the GS backend)
		*/
		class ScopedGroup
		{
		public:
			explicit ScopedGroup(StringView message) {
				static_cast<void>(message);
			}
		};

		static void Init(const IRhiCapabilities& caps) {
			static_cast<void>(caps);
		}
		static inline void Reset() {}

		static inline bool IsAvailable() {
			return false;
		}

		static void PushGroup(StringView message) {
			static_cast<void>(message);
		}
		static void PopGroup() {}
		static void MessageInsert(StringView message) {
			static_cast<void>(message);
		}

		static void SetObjectLabel(LabelTypes identifier, std::uint32_t name, StringView label) {
			static_cast<void>(identifier);
			static_cast<void>(name);
			static_cast<void>(label);
		}
		static void GetObjectLabel(LabelTypes identifier, std::uint32_t name, std::int32_t bufSize, std::int32_t* length, char* label) {
			static_cast<void>(identifier);
			static_cast<void>(name);
			static_cast<void>(bufSize);
			if (length != nullptr) {
				*length = 0;
			}
			if (label != nullptr && bufSize > 0) {
				label[0] = '\0';
			}
		}

		static inline std::int32_t GetMaxLabelLength() {
			return 0;
		}
	};
}
