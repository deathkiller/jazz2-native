#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorIcons.h"
#include "../../../nCine/Graphics/Texture.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Vector4.h"

#include <memory>

using namespace nCine;

namespace Jazz2::Editor
{
	/**
		@brief Textures the interface of the level editor is drawn with

		All shapes are generated, no file is needed. The rounded shapes are drawn at the interface scale, so their
		edges are smooth at any scale, the icons are pixel art drawn at a whole multiple of their size.
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

		/** @brief Generates the textures for an interface scale, does nothing if they are for that scale already */
		void Build(float scale);

		/** @brief Returns the texture of the shapes */
		const Texture& GetShapes() const {
			return *_shapes;
		}
		/** @brief Returns the texture of the icons */
		const Texture& GetIcons() const {
			return *_icons;
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
		/** @brief Converts a part of the shape texture to texture coordinates */
		Vector4f GetShapeTexCoords(const Recti& rect) const;
		/** @brief Returns the texture coordinates of an icon */
		Vector4f GetIconTexCoords(EditorIcon icon) const;

	private:
		static constexpr std::int32_t ShapesWidth = 256;
		static constexpr std::int32_t ShapesHeight = 128;
		static constexpr std::int32_t IconsPerRow = 16;

		std::unique_ptr<Texture> _shapes;
		std::unique_ptr<Texture> _icons;
		float _scale;
		NineSlice _roundFill;
		NineSlice _roundRing;
		NineSlice _shadow;
		std::int32_t _shadowSpread;
		Recti _circle;

		void BuildIcons();
	};
}

#endif
