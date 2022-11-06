#pragma once

#include "IIndexer.h"
#include "Base/Object.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// Keeps track of allocated objects in a growing only array
	class ArrayIndexer : public IIndexer
	{
	public:
		ArrayIndexer();
		~ArrayIndexer() override;

		unsigned int addObject(Object* object) override;
		bool removeObject(unsigned int id) override;

		Object* object(unsigned int id) const override;
		bool setObject(unsigned int id, Object* object) override;

		bool empty() const override {
			return numObjects_ == 0;
		}
		unsigned int size() const override {
			return numObjects_;
		}

	private:
		unsigned int numObjects_;
		unsigned int nextId_;
		SmallVector<Object*, 0> pointers_;

		/// Deleted copy constructor
		ArrayIndexer(const ArrayIndexer&) = delete;
		/// Deleted assignment operator
		ArrayIndexer& operator=(const ArrayIndexer&) = delete;
	};

}
