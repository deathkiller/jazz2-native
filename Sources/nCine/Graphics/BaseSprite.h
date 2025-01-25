#pragma once

#include "DrawableNode.h"
#include "../Primitives/Rect.h"

namespace nCine
{
	class Texture;
	class GLUniformBlockCache;

	/// Base class for sprites
	/*! \note Users cannot create instances of this class */
	class BaseSprite : public DrawableNode
	{
	public:
		BaseSprite(BaseSprite&&) = default;
		BaseSprite& operator=(BaseSprite&&) = default;

		BaseSprite& operator=(const BaseSprite&) = delete;

		/// Sets sprite size
		void setSize(float width, float height);
		/// Sets sprite size with a `Vector2f`
		inline void setSize(Vector2f size) {
			setSize(size.X, size.Y);
		}

		/// Gets the texture object
		inline const Texture* texture() const {
			return texture_;
		}
		/// Sets the texture object
		void setTexture(Texture* texture);
		/// Triggers a texture update without setting a new texture
		void resetTexture();

		/// Gets the texture source rectangle for blitting
		inline Recti texRect() const {
			return texRect_;
		}
		/// Sets the texture source rectangle for blitting
		void setTexRect(const Recti& rect);

		/// Returns `true` if the sprite texture is horizontally flipped
		inline bool isFlippedX() const {
			return flippedX_;
		}
		/// Flips the texture rect horizontally
		void setFlippedX(bool flippedX);
		/// Returns `true` if the sprite texture is vertically flipped
		inline bool isFlippedY() const {
			return flippedY_;
		}
		/// Flips the texture rect vertically
		void setFlippedY(bool flippedY);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/// The sprite texture
		Texture* texture_;
		/// The texture source rectangle
		Recti texRect_;

		/// A flag indicating if the sprite texture is horizontally flipped
		bool flippedX_;
		/// A flag indicating if the sprite texture is vertically flipped
		bool flippedY_;

		GLUniformBlockCache* instanceBlock_;
#endif

		/// Protected constructor accessible only by derived sprite classes
		BaseSprite(SceneNode* parent, Texture* texture, float xx, float yy);
		/// Protected constructor accessible only by derived sprite classes
		BaseSprite(SceneNode* parent, Texture* texture, Vector2f position);

		/// Protected copy constructor used to clone objects
		BaseSprite(const BaseSprite& other);

		/// Performs the required tasks upon a change to the shader
		void shaderHasChanged() override;

		/// Performs the required tasks upon a change to the texture
		virtual void textureHasChanged(Texture* newTexture) = 0;

		void updateRenderCommand() override;
	};

}
