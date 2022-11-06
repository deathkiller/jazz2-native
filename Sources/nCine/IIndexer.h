#pragma once

namespace nCine
{
	class Object;

	/// The Interface for every Object indexer
	class IIndexer
	{
	public:
		virtual ~IIndexer() = 0;

		/// Adds an object to the index
		virtual unsigned int addObject(Object* object) = 0;
		/// Removes an object from the index
		virtual bool removeObject(unsigned int id) = 0;

		/// Returns the object with the specified object id, if any
		virtual Object* object(unsigned int id) const = 0;
		/// Sets the object pointer for the specified id
		virtual bool setObject(unsigned int id, Object* object) = 0;

		/// Returns true if the index is empty
		virtual bool empty() const = 0;

		/// Returns the number of objects in the index
		virtual unsigned int size() const = 0;
	};

	inline IIndexer::~IIndexer() {}

	/// Fake indexer, always returning `nullptr` and a zero index
	class NullIndexer : public IIndexer
	{
	public:
		unsigned int addObject(Object* object) override {
			return 0U;
		}
		bool removeObject(unsigned int id) override {
			return true;
		}

		Object* object(unsigned int id) const override {
			return nullptr;
		}
		bool setObject(unsigned int id, Object* object) override {
			return true;
		};

		bool empty() const override {
			return true;
		}

		unsigned int size() const override {
			return 0U;
		}
	};

}
