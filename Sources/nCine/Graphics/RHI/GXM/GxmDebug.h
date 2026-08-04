#pragma once

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	class IRhiCapabilities;
}

namespace nCine::RHI::GXM
{
	/**
		@brief Debug-output and object-labelling facade of the sceGxm backend

		The render pipeline's debug groups map onto sceGxm's own user markers, which the Razor GPU capture
		tools and Vita3K's command tracing both display, so a capture is annotated with the same pass names
		the OpenGL backend pushes through `glPushDebugGroup`. Object labels have no sceGxm counterpart (its
		resources are plain structures in user memory, not driver-side named objects) and are dropped.

		The markers are only emitted between @ref GxmDevice::CreateSwapchain() and its teardown, because
		`sceGxmPushUserMarker()` needs the context; before that they are silently dropped.
	*/
	class GxmDebug
	{
	public:
		/** @brief Object types that can be labelled (values are irrelevant for the sceGxm backend) */
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
			@brief RAII scope for a debug message group (a `sceGxmPushUserMarker()` / `sceGxmPopUserMarker()` pair)
		*/
		class ScopedGroup
		{
		public:
			explicit ScopedGroup(StringView message) {
				PushGroup(message);
			}
			~ScopedGroup() {
				PopGroup();
			}

			ScopedGroup(const ScopedGroup&) = delete;
			ScopedGroup& operator=(const ScopedGroup&) = delete;
		};

		static void Init(const IRhiCapabilities& caps) {
			static_cast<void>(caps);
		}
		static inline void Reset() {}

		static inline bool IsAvailable() {
			return true;
		}

		/** @brief Opens a user marker on the sceGxm context */
		static void PushGroup(StringView message);
		/** @brief Closes the innermost user marker */
		static void PopGroup();
		/** @brief Inserts a standalone user marker */
		static void MessageInsert(StringView message);

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
