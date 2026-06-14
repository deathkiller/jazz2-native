#pragma once

#include "BaseSprite.h"

namespace nCine
{
	/**
		@brief Scene node that draws a single textured quad
		
		A drawable node that renders a rectangular sprite from a texture (or an untextured solid
		quad). Its size defaults to the texture dimensions and the whole texture is used as the
		source rectangle.
	*/
	class Sprite : public BaseSprite
	{
	public:
		/** @brief Constructs a sprite with no parent and no texture, positioned at the origin */
		Sprite();
		/** @brief Constructs a sprite with a parent and a texture, positioned at the relative origin */
		Sprite(SceneNode* parent, Texture* texture);
		/** @brief Constructs a sprite with a texture but no parent, positioned at the origin */
		explicit Sprite(Texture* texture);
		/** @brief Constructs a sprite with a parent, a texture and a relative position given as two coordinates */
		Sprite(SceneNode* parent, Texture* texture, float xx, float yy);
		/** @brief Constructs a sprite with a parent, a texture and a relative position given as a vector */
		Sprite(SceneNode* parent, Texture* texture, Vector2f position);
		/** @brief Constructs a sprite with a texture and a position given as two coordinates but no parent */
		Sprite(Texture* texture, float xx, float yy);
		/** @brief Constructs a sprite with a texture and a position given as a vector but no parent */
		Sprite(Texture* texture, Vector2f position);

		Sprite& operator=(const Sprite&) = delete;
		Sprite(Sprite&&) = default;
		Sprite& operator=(Sprite&&) = default;

		/** @brief Returns a copy of this object */
		inline Sprite clone() const {
			return Sprite(*this);
		}

		inline static ObjectType sType() {
			return ObjectType::Sprite;
		}

	protected:
		/** @brief Protected copy constructor used to clone objects */
		Sprite(const Sprite& other);

		void textureHasChanged(Texture* newTexture) override;

	private:
		/** @brief Common initialization for the constructors and the copy constructor */
		void init();
	};
}
