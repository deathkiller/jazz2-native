#include "Object.h"
#include "../ServiceLocator.h"
#include "../../Common.h"

namespace nCine
{
	unsigned int Object::lastId_ = 0;

	Object::Object(ObjectType type)
		: type_(type)
	{
		id_ = ++lastId_;
	}

	Object::Object(Object&& other) noexcept
		: type_(other.type_), id_(other.id_)
	{
	}

	Object& Object::operator=(Object&& other) noexcept
	{
		id_ = other.id_;
		return *this;
	}

	Object::Object(const Object& other)
		: type_(other.type_)
	{
		id_ = ++lastId_;
	}
}
