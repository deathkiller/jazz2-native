#pragma once

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	class IRhiCapabilities;
}

namespace nCine::RHI::GX2
{
	/**
		@brief Debug-output and object-labelling facade of the GX2 backend

		Inert: GX2 has debug tags (`GX2DebugTagUserString()`), but they are only seen by Nintendo's GPU
		profiler, and the backend's resources are plain structures in memory rather than driver-side named
		objects. The class is kept so the render pipeline's `RHI::Debug` calls compile unchanged; every call
		is an empty inline function.
	*/
	class Gx2Debug
	{
	public:
		/** @brief Object types that can be labelled (values are irrelevant for the GX2 backend) */
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

		/** @brief RAII scope for a debug message group (inert on this backend) */
		class ScopedGroup
		{
		public:
			explicit ScopedGroup(StringView message) {
				static_cast<void>(message);
			}
			~ScopedGroup() {}

			ScopedGroup(const ScopedGroup&) = delete;
			ScopedGroup& operator=(const ScopedGroup&) = delete;
		};

		static inline void Init(const IRhiCapabilities& caps) {
			static_cast<void>(caps);
		}
		static inline void Reset() {}

		static inline bool IsAvailable() {
			return false;
		}

		static inline void PushGroup(StringView message) {
			static_cast<void>(message);
		}
		static inline void PopGroup() {}
		static inline void MessageInsert(StringView message) {
			static_cast<void>(message);
		}

		static inline void SetObjectLabel(LabelTypes identifier, std::uint32_t name, StringView label) {
			static_cast<void>(identifier);
			static_cast<void>(name);
			static_cast<void>(label);
		}
		static inline void GetObjectLabel(LabelTypes identifier, std::uint32_t name, std::int32_t bufSize, std::int32_t* length, char* label) {
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
