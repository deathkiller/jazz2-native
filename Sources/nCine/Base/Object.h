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
		virtual ~Object();

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

		/// Returns a casted pointer to the object with the specified id, if any exists
		template <class T> static T* fromId(unsigned int id);

	protected:
		/// Object type
		ObjectType type_;

		/// Protected copy constructor used to clone objects
		Object(const Object& other);

	private:
		/// Object identification in the indexer
		unsigned int id_;

		 /// Deleted assignment operator
		Object& operator=(const Object&) = delete;
	};
}