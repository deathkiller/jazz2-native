#include "Resources.h"

namespace Jazz2
{
	GenericGraphicResource::GenericGraphicResource() noexcept
		: Flags(GenericGraphicResourceFlags::None)
	{
	}

	GraphicResource::GraphicResource() noexcept
	{
	}

	bool GraphicResource::operator<(const GraphicResource& p) const noexcept
	{
		return State < p.State;
	}

	GenericSoundResource::GenericSoundResource(const StringView& path) noexcept
		: Buffer(path), Flags(GenericSoundResourceFlags::None)
	{
	}

	SoundResource::SoundResource() noexcept
	{
	}

	Metadata::Metadata() noexcept
		: Flags(MetadataFlags::None)
	{
	}

	GraphicResource* Metadata::FindAnimation(AnimState state) noexcept
	{
		auto it = std::lower_bound(Animations.begin(), Animations.end(), state, [](const GraphicResource& x, AnimState value) {
			return x.State < value;
		});

		return (it != Animations.end() && it->State == state ? it : nullptr);
	}

	Episode::Episode() noexcept
	{
	}
}