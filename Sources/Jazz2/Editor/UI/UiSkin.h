#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorIcons.h"
#include "../../../nCine/Graphics/Texture.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Vector2.h"
#include "../../../nCine/Primitives/Vector4.h"

#include <memory>

using namespace nCine;

namespace Jazz2::Editor
{
	/**
		@brief Texture the interface of the level editor is drawn with

		All shapes are generated, no file is needed. The rounded shapes are drawn at the interface scale, so their
		edges are smooth at any scale, the icons are pixel art enlarged to the size of an icon at that scale. The
		shapes, the icons and a white patch for plain rectangles are all in one texture, so everything but text
		and images drawn in a slot of the interface can be batched together.
	*/
	class UiSkin
	{
	public:
		/** @brief Part of a texture drawn as nine slices, its corners keep their size */
		struct NineSlice
		{
			/** @brief Part of the texture */
			Recti Rect;
			/** @brief Size of the corners in pixels */
			std::int32_t Corner;
		};

		UiSkin();
		~UiSkin();

		/** @brief Generates the texture for an interface scale, does nothing if it's for that scale already */
		void Build(float scale);

		/** @brief Returns the texture with all the shapes and icons */
		const Texture& GetAtlas() const {
			return *_atlas;
		}
		/** @brief Returns the texture coordinates that give plain white, for a rectangle of any size */
		Vector4f GetWhiteTexCoords() const;
		/** @brief Returns the size of an icon in pixels, as it's drawn */
		std::int32_t GetIconSize() const {
			return _iconSize;
		}
		/** @brief Returns a filled rounded rectangle */
		const NineSlice& GetRoundFill() const {
			return _roundFill;
		}
		/** @brief Returns the border of a rounded rectangle */
		const NineSlice& GetRoundRing() const {
			return _roundRing;
		}
		/** @brief Returns a soft shadow, its corners include the part outside of what casts it */
		const NineSlice& GetShadow() const {
			return _shadow;
		}
		/** @brief Returns how far the shadow reaches out in pixels */
		std::int32_t GetShadowSpread() const {
			return _shadowSpread;
		}
		/** @brief Returns a filled circle */
		const Recti& GetCircle() const {
			return _circle;
		}
		/** @brief Converts a part of the texture to texture coordinates */
		Vector4f GetShapeTexCoords(const Recti& rect) const;
		/** @brief Returns the texture coordinates of an icon */
		Vector4f GetIconTexCoords(EditorIcon icon) const;

	private:
		// Widest the texture gets, the icons wrap to more rows instead
		static constexpr std::int32_t MaxAtlasWidth = 1024;
		static constexpr std::int32_t MaxIconsPerRow = 16;

		std::unique_ptr<Texture> _atlas;
		std::int32_t _atlasWidth;
		std::int32_t _atlasHeight;
		float _scale;
		NineSlice _roundFill;
		NineSlice _roundRing;
		NineSlice _shadow;
		std::int32_t _shadowSpread;
		Recti _circle;
		Recti _white;
		std::int32_t _iconSize;
		std::int32_t _iconsPerRow;
		Vector2i _iconsOrigin;
	};
}

#endif
