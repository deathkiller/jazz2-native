#include "IFileStream.h"

namespace nCine
{
	IFileStream::IFileStream(const String& filename)
		: type_(FileType::Base), filename_(filename), fileDescriptor_(-1), filePointer_(nullptr), shouldCloseOnDestruction_(true), fileSize_(0)
	{
	}

	bool IFileStream::IsOpened() const
	{
		return (fileDescriptor_ >= 0 || filePointer_ != nullptr);
	}
}
