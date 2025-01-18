#pragma once

#include <Base/TypeInfo.h>

namespace nCine
{
	/// Base class of nCine objects
	class Object
	{
	public:
		/// Object types
		enum class ObjectType {
			Base = 0,
			Texture,
			Shader,
			SceneNode,
			Sprite,
			MeshSprite,
			AnimatedSprite,
			Particle,
			ParticleSystem,
			AudioBuffer,
			AudioBufferPlayer,
			AudioStreamPlayer
		};

		/// Constructs an object with a specified type and adds it to the index
		explicit Object(ObjectType type);
		/// Removes an object from the index and then destroys it
		virtual ~Object() = 0;

		Object(Object&& other) noexcept;
		Object& operator=(Object&& other) noexcept;

		/// Returns the object identification number
		inline unsigned int id() const {
			return _id;
		}

		/// Returns the object type (RTTI)
		inline ObjectType type() const {
			return _type;
		}
		/// Static method to return class type
		inline static ObjectType sType() {
			return ObjectType::Base;
		}

	protected:
		/// Object type
		ObjectType _type;

		/// Protected copy constructor used to clone objects
		Object(const Object& other);

	private:
		static std::uint32_t _lastId;

		std::uint32_t _id;

		Object& operator=(const Object&) = delete;
	};

	inline Object::~Object() { }
}