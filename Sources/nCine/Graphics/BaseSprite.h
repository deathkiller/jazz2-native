#pragma once

#include "DrawableNode.h"
#include "../Primitives/Rect.h"

namespace nCine
{
	class Texture;

	/**
		@brief Base class for textured sprites
		
		Common base for drawable sprites such as @ref Sprite and @ref MeshSprite. Holds the texture, the
		source rectangle to blit from it, horizontal/vertical flipping and the palette offset used by the
		palette shaders. This class is abstract; instances are created through its derived classes.
	*/
	class BaseSprite : public DrawableNode
	{
	public:
		BaseSprite(BaseSprite&&) = default;
		BaseSprite& operator=(BaseSprite&&) = default;

		BaseSprite& operator=(const BaseSprite&) = delete;

		/** @brief Sets the sprite size */
		void setSize(float width, float height);
		/** @brief Sets the sprite size from a `Vector2f` */
		inline void setSize(Vector2f size) {
			setSize(size.X, size.Y);
		}

		/** @brief Returns the texture object */
		inline const Texture* texture() const {
			return texture_;
		}
		/** @brief Sets the texture object */
		void setTexture(Texture* texture);
		/** @brief Triggers a texture update without setting a new texture */
		void resetTexture();

		/** @brief Returns the texture source rectangle used for blitting */
		inline Recti texRect() const {
			return texRect_;
		}
		/** @brief Sets the texture source rectangle used for blitting */
		void setTexRect(const Recti& rect);

		/** @brief Returns whether the sprite texture is horizontally flipped */
		inline bool isFlippedX() const {
			return flippedX_;
		}
		/** @brief Flips the texture rectangle horizontally */
		void setFlippedX(bool flippedX);
		/** @brief Returns whether the sprite texture is vertically flipped */
		inline bool isFlippedY() const {
			return flippedY_;
		}
		/** @brief Flips the texture rectangle vertically */
		void setFlippedY(bool flippedY);

		/** @brief Returns the flat palette offset added to the per-pixel index by palette shaders */
		inline float paletteOffset() const {
			return paletteOffset_;
		}
		/** @brief Sets the flat palette offset used by palette shaders (no effect on non-palette shaders) */
		void setPaletteOffset(float paletteOffset);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief The sprite texture */
		Texture* texture_;
		/** @brief The texture source rectangle */
		Recti texRect_;

		/** @brief Whether the sprite texture is horizontally flipped */
		bool flippedX_;
		/** @brief Whether the sprite texture is vertically flipped */
		bool flippedY_;
		/** @brief Flat index into the palette texture, uploaded per-instance for palette shaders (0 = first palette row) */
		float paletteOffset_;
#endif

		/** @brief Protected constructor accessible only by derived sprite classes */
		BaseSprite(SceneNode* parent, Texture* texture, float xx, float yy);
		/** @brief Protected constructor accessible only by derived sprite classes */
		BaseSprite(SceneNode* parent, Texture* texture, Vector2f position);

		/** @brief Protected copy constructor used to clone objects */
		BaseSprite(const BaseSprite& other);

		/** @brief Performs the required tasks upon a change to the shader */
		void shaderHasChanged() override;

		/** @brief Performs the required tasks upon a change to the texture */
		virtual void textureHasChanged(Texture* newTexture) = 0;

		void updateRenderCommand() override;
	};

}
