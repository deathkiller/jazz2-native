#include "Object.h"
#include "../../Common.h"

namespace nCine
{
	std::uint32_t Object::_lastId = 0;

	Object::Object(ObjectType type)
		: _type(type)
	{
		_id = ++_lastId;
	}

	Object::Object(Object&& other) noexcept
		: _type(other._type), _id(other._id)
	{
	}

	Object& Object::operator=(Object&& other) noexcept
	{
		_id = other._id;
		return *this;
	}

	Object::Object(const Object& other)
		: _type(other._type)
	{
		_id = ++_lastId;
	}
}
