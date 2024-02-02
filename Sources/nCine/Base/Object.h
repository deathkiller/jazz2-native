#pragma once

namespace nCine
{
	/// Static RRTI and identification index
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

		/// Move constructor
		Object(Object&& other) noexcept;
		/// Move assignment operator
		Object& operator=(Object&& other) noexcept;

		/// Returns the object identification number
		inline unsigned int id() const {
			return id_;
		}

		/// Returns the object type (RTTI)
		inline ObjectType type() const {
			return type_;
		}
		/// Static method to return class type
		inline static ObjectType sType() {
			return ObjectType::Base;
		}

	protected:
		/// Object type
		ObjectType type_;

		/// Protected copy constructor used to clone objects
		Object(const Object& other);

	private:
		static unsigned int lastId_;

		/// Object identification in the indexer
		unsigned int id_;

		 /// Deleted assignment operator
		Object& operator=(const Object&) = delete;
	};

	inline Object::~Object() { }
}