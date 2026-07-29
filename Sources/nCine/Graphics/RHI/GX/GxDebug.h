#pragma once

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IGfxCapabilities;
}

namespace nCine::RHI::GX
{
	/**
		@brief Debug-output and object-labelling stub of the GX backend

		GX has no device-side debug facility, so every entry point is a no-op. The class exists only to
		satisfy the `RHI::Debug` contract alias (debug groups, message insertion and object labels used by
		the render pipeline).
	*/
	class GxDebug
	{
	public:
		/** @brief Object types that can be labelled (values are irrelevant for the GX backend) */
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
			@brief RAII scope for a debug message group (a no-op for the GX backend)
		*/
		class ScopedGroup
		{
		public:
			explicit ScopedGroup(StringView message) {
				static_cast<void>(message);
			}
		};

		static void Init(const IGfxCapabilities& gfxCaps) {
			static_cast<void>(gfxCaps);
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
