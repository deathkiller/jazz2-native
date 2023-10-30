#include "Resources.h"

namespace Jazz2
{
	GenericGraphicResource::GenericGraphicResource()
	{
	}

	GraphicResource::GraphicResource()
	{
	}

	GenericSoundResource::GenericSoundResource(const StringView& path)
		: Buffer(path)
	{
	}

	SoundResource::SoundResource()
	{
	}

	Metadata::Metadata()
		: Flags(MetadataFlags::None)
	{
	}

	Episode::Episode()
	{
	}
}