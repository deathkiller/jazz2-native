#include "IFileStream.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	IFileStream::IFileStream(const String& filename)
		: type_(FileType::Base), filename_(filename), fileDescriptor_(-1), filePointer_(nullptr),
		shouldCloseOnDestruction_(true), fileSize_(0)
	{
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool IFileStream::IsOpened() const
	{
		return (fileDescriptor_ >= 0 || filePointer_ != nullptr);
	}
}
