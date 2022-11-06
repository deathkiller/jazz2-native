#include "Object.h"
#include "../ServiceLocator.h"
#include "../../Common.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Object::Object(ObjectType type)
		: type_(type), id_(0)
	{
		id_ = theServiceLocator().indexer().addObject(this);
	}

	Object::~Object()
	{
		// Don't remove the object id from indexer if this is a moved out object
		if (id_ > 0)
			theServiceLocator().indexer().removeObject(id_);
	}

	Object::Object(Object&& other)
		: type_(other.type_), id_(other.id_)/*, name_(std::move(other.name_))*/
	{
		theServiceLocator().indexer().setObject(id_, this);
		other.id_ = 0;
	}

	Object& Object::operator=(Object&& other) noexcept
	{
		type_ = other.type_;
		theServiceLocator().indexer().removeObject(id_);
		id_ = other.id_;
		//name_ = other.name_;

		other.id_ = 0;
		return *this;
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	template <class T>
	T* Object::fromId(unsigned int id)
	{
		const Object* object = theServiceLocator().indexer().object(id);

		if (object != nullptr) {
			if (object->type_ == T::sType()) {
				return static_cast<T*>(object);
			} else { // Cannot cast
				LOGF_X("Object %u is of type %u instead of %u", id, object->type_, T::sType());
				return nullptr;
			}
		} else {
			LOGW_X("Object %u not found", id);
			return nullptr;
		}
	}

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	Object::Object(const Object& other)
		: type_(other.type_), id_(0)
	{
		id_ = theServiceLocator().indexer().addObject(this);
	}
}
