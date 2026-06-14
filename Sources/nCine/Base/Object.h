#pragma once

#include <Base/TypeInfo.h>

namespace nCine
{
	/**
		@brief Base class of all nCine objects
		
		Provides each derived object with a unique identifier and a run-time type tag (@ref ObjectType)
		usable for lightweight type checks without RTTI.
	*/
	class Object
	{
	public:
		/**
		 * @brief Run-time type of an object
		 */
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

		/** @brief Constructs an object of the specified type and assigns it a unique identifier */
		explicit Object(ObjectType type);
		virtual ~Object() = 0;

		Object(Object&& other) noexcept;
		Object& operator=(Object&& other) noexcept;

		/** @brief Returns the unique object identifier */
		inline unsigned int id() const {
			return _id;
		}

		/** @brief Returns the run-time object type */
		inline ObjectType type() const {
			return _type;
		}
		/** @brief Returns the static type of this class */
		inline static ObjectType sType() {
			return ObjectType::Base;
		}

	protected:
		/** @brief Run-time object type */
		ObjectType _type;

		/** @brief Protected copy constructor used to clone objects */
		Object(const Object& other);

	private:
		static std::uint32_t _lastId;

		std::uint32_t _id;

		Object& operator=(const Object&) = delete;
	};

	inline Object::~Object() { }
}