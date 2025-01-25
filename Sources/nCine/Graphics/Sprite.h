#pragma once

#include "BaseSprite.h"

namespace nCine
{
	/// Scene node representing a regular sprite
	class Sprite : public BaseSprite
	{
	public:
		/// Default constructor for a sprite with no parent and no texture, positioned in the origin
		Sprite();
		/// Constructor for a sprite with a parent and texture, positioned in the relative origin
		Sprite(SceneNode* parent, Texture* texture);
		/// Constructor for a sprite with a texture but no parent, positioned in the origin
		explicit Sprite(Texture* texture);
		/// Constructor for a sprite with a parent, a texture and a specified relative position
		Sprite(SceneNode* parent, Texture* texture, float xx, float yy);
		/// Constructor for a sprite with a parent, a texture and a specified relative position as a vector
		Sprite(SceneNode* parent, Texture* texture, Vector2f position);
		/// Constructor for a sprite with a texture and a specified position but no parent
		Sprite(Texture* texture, float xx, float yy);
		/// Constructor for a sprite with a texture and a specified position as a vector but no parent
		Sprite(Texture* texture, Vector2f position);

		Sprite& operator=(const Sprite&) = delete;
		Sprite(Sprite&&) = default;
		Sprite& operator=(Sprite&&) = default;

		/// Returns a copy of this object
		inline Sprite clone() const {
			return Sprite(*this);
		}

		inline static ObjectType sType() {
			return ObjectType::Sprite;
		}

	protected:
		/// Protected copy constructor used to clone objects
		Sprite(const Sprite& other);

		void textureHasChanged(Texture* newTexture) override;

	private:
		/// Initializer method for constructors and the copy constructor
		void init();
	};
}
