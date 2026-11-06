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
		void SetSize(float width, float height);
		/** @brief Sets the sprite size from a `Vector2f` */
		inline void SetSize(Vector2f size) {
			SetSize(size.X, size.Y);
		}

		/** @brief Returns the texture object */
		inline const Texture* GetTexture() const {
			return _texture;
		}
		/** @brief Sets the texture object */
		void SetTexture(Texture* texture);
		/** @brief Triggers a texture update without setting a new texture */
		void ResetTexture();

		/** @brief Returns the texture source rectangle used for blitting */
		inline Recti GetTexRect() const {
			return _texRect;
		}
		/** @brief Sets the texture source rectangle used for blitting */
		void SetTexRect(const Recti& rect);

		/** @brief Returns whether the sprite texture is horizontally flipped */
		inline bool IsFlippedX() const {
			return _flippedX;
		}
		/** @brief Flips the texture rectangle horizontally */
		void SetFlippedX(bool flippedX);
		/** @brief Returns whether the sprite texture is vertically flipped */
		inline bool IsFlippedY() const {
			return _flippedY;
		}
		/** @brief Flips the texture rectangle vertically */
		void SetFlippedY(bool flippedY);

		/** @brief Returns the flat palette offset added to the per-pixel index by palette shaders */
		inline float GetPaletteOffset() const {
			return _paletteOffset;
		}
		/** @brief Sets the flat palette offset used by palette shaders (no effect on non-palette shaders) */
		void SetPaletteOffset(float paletteOffset);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief The sprite texture */
		Texture* _texture;
		/** @brief The texture source rectangle */
		Recti _texRect;

		/** @brief Whether the sprite texture is horizontally flipped */
		bool _flippedX;
		/** @brief Whether the sprite texture is vertically flipped */
		bool _flippedY;
		/** @brief Flat index into the palette texture, uploaded per-instance for palette shaders (0 = first palette row) */
		float _paletteOffset;
#endif

		/** @brief Protected constructor accessible only by derived sprite classes */
		BaseSprite(SceneNode* parent, Texture* texture, float xx, float yy);
		/** @brief Protected constructor accessible only by derived sprite classes */
		BaseSprite(SceneNode* parent, Texture* texture, Vector2f position);

		/** @brief Protected copy constructor used to clone objects */
		BaseSprite(const BaseSprite& other);

		/** @brief Performs the required tasks upon a change to the shader */
		void ShaderHasChanged() override;

		/** @brief Performs the required tasks upon a change to the texture */
		virtual void TextureHasChanged(Texture* newTexture) = 0;

		void UpdateRenderCommand() override;
	};

}
