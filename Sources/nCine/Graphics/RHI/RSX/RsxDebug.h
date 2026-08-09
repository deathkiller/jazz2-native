#pragma once

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	class IRhiCapabilities;
}

namespace nCine::RHI::RSX
{
	/**
		@brief Debug-output and object-labelling facade of the RSX backend

		Entirely inert, and for a more basic reason than the sceGxm backend's - which at least maps the
		pipeline's debug groups onto sceGxm's user markers. The RSX command set has no marker or annotation
		packet at all: a command buffer is a stream of NV40 method writes with nowhere to carry a name, and
		PSL1GHT's librsx exposes nothing for it either. Object labels have no counterpart for the same reason
		(the backend's resources are plain structures in memory the GPU reads by offset, not driver-side
		named objects), so both halves of this class are dropped rather than approximated.

		The class is kept so the render pipeline's `RHI::Debug` calls compile unchanged; the compiler removes
		the calls entirely, since every one of them is an empty inline function.
	*/
	class RsxDebug
	{
	public:
		/** @brief Object types that can be labelled (values are irrelevant for the RSX backend) */
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
