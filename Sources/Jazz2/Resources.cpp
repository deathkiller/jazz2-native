#include "Resources.h"
#include "ContentResolver.h"

namespace Jazz2::Resources
{
	GenericGraphicResource::GenericGraphicResource() noexcept
		: Flags(GenericGraphicResourceFlags::None), MaskStride(0)
	{
	}

	GraphicResource::GraphicResource() noexcept
		: Base(nullptr), PaletteOffset(0), DeferredIndex(NotDeferred)
	{
	}

	bool GraphicResource::operator<(const GraphicResource& p) const noexcept
	{
		return State < p.State;
	}

	GenericSoundResource::GenericSoundResource(std::unique_ptr<Stream> stream, StringView filename) noexcept
		: Buffer(std::move(stream), filename), Flags(GenericSoundResourceFlags::None)
	{
	}

	SoundResource::SoundResource() noexcept
	{
	}

	Metadata::Metadata() noexcept
		: Flags(MetadataFlags::None)
	{
	}

	GraphicResource* Metadata::FindAnimation(AnimState state)
	{
		auto it = std::lower_bound(Animations.begin(), Animations.end(), state, [](const GraphicResource& x, AnimState value) {
			return x.State < value;
		});

		if (it == Animations.end() || it->State != state) {
			return nullptr;
		}

		// Everything that isn't deferred is loaded already, so the common case is a single predictable branch
		if DEATH_UNLIKELY(it->Base == nullptr) {
			return (ContentResolver::Get().ResolveAnimation(*this, *it) ? it : nullptr);
		}

		return it;
	}

	Episode::Episode() noexcept
	{
	}
}