#include "ArrayIndexer.h"

namespace nCine
{
	ArrayIndexer::ArrayIndexer()
		: numObjects_(0), nextId_(0)
	{
		pointers_.reserve(16);

		// First element reserved
		pointers_.push_back(nullptr);
		nextId_++;
	}

	ArrayIndexer::~ArrayIndexer()
	{
		for (auto& obj : pointers_) {
			if (obj != nullptr) {
				delete obj;
			}
		}
	}

	unsigned int ArrayIndexer::addObject(Object* object)
	{
		if (object == nullptr) {
			return 0;
		}

		numObjects_++;

		pointers_.push_back(object);
		nextId_++;

		return nextId_ - 1;
	}

	bool ArrayIndexer::removeObject(unsigned int id)
	{
		// setting to `nullptr` instead of physically removing
		if (id < pointers_.size() && pointers_[id] != nullptr) {
			pointers_[id] = nullptr;
			numObjects_--;
			return true;
		}
		return false;
	}

	Object* ArrayIndexer::object(unsigned int id) const
	{
		Object* objPtr = nullptr;

		if (id < pointers_.size()) {
			objPtr = pointers_[id];
		}
		return objPtr;
	}

	bool ArrayIndexer::setObject(unsigned int id, Object* object)
	{
		if (id < pointers_.size()) {
			pointers_[id] = object;
			return true;
		}
		return false;
	}
}
