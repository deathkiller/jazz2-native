#include "LayerTypes.h"
#include "../Formats/LevelFile.h"

#include <algorithm>

using namespace nCine;

namespace Jazz2::Tiles
{
	LayerDescription LayerDescription::FromRecord(const Formats::LayerRecord& record, std::int32_t mainPlaneDepth)
	{
		LayerDescription description;
		if (record.Kind == Formats::LayerKind::Sprite) {
			// The sprite layer is always placed with its defaults, the objects walk on its tiles
			description.OffsetX = 0.0f;
			description.OffsetY = 0.0f;
			description.SpeedX = 1.0f;
			description.SpeedY = 1.0f;
			description.AutoSpeedX = 0.0f;
			description.AutoSpeedY = 0.0f;
			description.RepeatX = false;
			description.RepeatY = false;
			description.Depth = (std::uint16_t)(mainPlaneDepth - 50);
			description.UseInherentOffset = false;
			description.SpeedModelX = LayerSpeedModel::Default;
			description.SpeedModelY = LayerSpeedModel::Default;
			// Only drawn otherwise by a renderer that keeps its tiles where they are, the level file stores it in a chunk
			// (see Formats::SpriteLayerChunk), its record has the defaults otherwise
			if (IsSpriteLayerRenderer((LayerRendererType)record.RendererType)) {
				description.RendererType = (LayerRendererType)record.RendererType;
				description.Color = Vector4f(record.Color[0] / 255.0f, record.Color[1] / 255.0f, record.Color[2] / 255.0f, record.Color[3] / 255.0f);
			} else {
				description.RendererType = LayerRendererType::Default;
				description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
			}
			return description;
		}

		description.SpeedModelX = (LayerSpeedModel)(record.SpeedModels & 0x0f);
		description.SpeedModelY = (LayerSpeedModel)((record.SpeedModels >> 4) & 0x0f);

		description.OffsetX = record.OffsetX;
		description.OffsetY = record.OffsetY;
		description.SpeedX = record.SpeedX;
		description.SpeedY = record.SpeedY;
		description.AutoSpeedX = record.AutoSpeedX;
		description.AutoSpeedY = record.AutoSpeedY;
		description.RepeatX = ((record.Flags & Formats::LayerRecord::RepeatX) != 0);
		description.RepeatY = ((record.Flags & Formats::LayerRecord::RepeatY) != 0);
		description.Depth = (std::uint16_t)(mainPlaneDepth - record.Depth);
		description.UseInherentOffset = ((record.Flags & Formats::LayerRecord::UseInherentOffset) != 0);

		description.RendererType = (LayerRendererType)record.RendererType;
		bool knownRenderer = true;
		switch (description.RendererType) {
			case LayerRendererType::Default:
			case LayerRendererType::Solid:
			case LayerRendererType::Tinted:
			case LayerRendererType::XRay:
			case LayerRendererType::BlurBehind:
			case LayerRendererType::PaletteShift:
			case LayerRendererType::SingleColor:
			case LayerRendererType::ChromaKey:
			case LayerRendererType::Sky:
			case LayerRendererType::Tunnel:
			case LayerRendererType::TileMenu:
			case LayerRendererType::Wave:
			case LayerRendererType::Cylinder:
			case LayerRendererType::Reflection:
				break;
			default:
				// A renderer this version of the game doesn't know is drawn as an ordinary layer
				description.RendererType = LayerRendererType::Default;
				knownRenderer = false;
				break;
		}

		// A tinted, a single color and a chroma key layer keep a palette index in red, it's resolved from the live palette
		// when the layer is drawn, a palette-shifted one its shift. The color of an unknown renderer means something else to it - a
		// renderer of a later version may have its parameters there - so it's drawn as it is.
		description.Color = (knownRenderer
			? Vector4f(record.Color[0] / 255.0f, record.Color[1] / 255.0f, record.Color[2] / 255.0f, record.Color[3] / 255.0f)
			: Vector4f(1.0f, 1.0f, 1.0f, 1.0f));
		description.SetTexturedParams(record.Textured);
		return description;
	}

	void LayerDescription::SetTexturedParams(const Formats::TexturedLayerParams& params)
	{
		TextureFadePosition = Vector2f(params.FadeX, params.FadeY);
		TextureFade = params.Fade;
		TextureFlags = params.Flags;
		TextureInnerSpeed = Vector2f(params.InnerSpeedX, params.InnerSpeedY);
		TextureInnerAutoSpeed = Vector2f(params.InnerAutoSpeedX, params.InnerAutoSpeedY);
	}

	bool LayerDescription::IsSpriteLayerRenderer(LayerRendererType type)
	{
		switch (type) {
			case LayerRendererType::Default:
			case LayerRendererType::Solid:
			case LayerRendererType::Tinted:
			case LayerRendererType::PaletteShift:
			case LayerRendererType::SingleColor:
				return true;
			default:
				return false;
		}
	}

	bool LayerDescription::HasInnerSurface() const
	{
		return ((TextureFlags & (Formats::TexturedLayerParams::InnerWindow | Formats::TexturedLayerParams::InnerLayer)) != 0 &&
			(IsTexturedBackground(RendererType) || RendererType == LayerRendererType::Reflection) && RendererType != LayerRendererType::Wave);
	}

	bool LayerDescription::HasPlusPerspective() const
	{
		return (RendererType == LayerRendererType::Cylinder || (TextureFlags & (Formats::TexturedLayerParams::Fullscreen |
			Formats::TexturedLayerParams::InnerWindow | Formats::TexturedLayerParams::InnerLayer)) != 0);
	}

	bool LayerDescription::IsTexturePlacedByLayer5(bool vertical) const
	{
		return ((RendererType == LayerRendererType::Sky || RendererType == LayerRendererType::Tunnel || RendererType == LayerRendererType::Cylinder) &&
			HasPlusPerspective() && !HasInnerSurface() && (vertical ? SpeedModelY : SpeedModelX) == LayerSpeedModel::AlwaysOnTop);
	}

	Vector2f LayerDescription::GetGameplayOrigin(Vector2i layoutSize, Vector2i spriteLayoutSize, const Rectf& view) const
	{
		Vector2f offsets = GetDrawOffsets(view.H);
		Vector2f coords = GetViewCoordinates(layoutSize, spriteLayoutSize, view, offsets.X, offsets.Y);
		return Vector2f(view.X - HardcodedOffset - coords.X, view.Y - HardcodedOffset - coords.Y);
	}

	Vector2f LayerDescription::GetDrawOffsets(float viewHeight) const
	{
		// As limitVisibleRegion of JJ2+, which doesn't move a layer that repeats vertically or is placed by another model
		bool inherentOffset = (UseInherentOffset && !RepeatY && SpeedModelY == LayerSpeedModel::Default);
		return Vector2f(OffsetX, OffsetY - (inherentOffset ? (viewHeight - 200) / 2 : 0) + 1);
	}

	namespace
	{
		// How far a view is between the two ends of the level, 0 at the start and 1 at the end - it places the layers of
		// LayerSpeedModel::FitLevel and LayerSpeedModel::SpeedMultipliers
		float GetLevelProgress(float viewStart, float viewSize, std::int32_t levelTiles)
		{
			float range = float(levelTiles * Formats::LevelFile::TileSize) - viewSize;
			return (range > 0.0f ? std::clamp(viewStart / range, 0.0f, 1.0f) : 0.0f);
		}

		// The layer coordinate at the start of the view on one axis, minus the hardcoded offset
		float GetViewCoordinate(LayerSpeedModel model, float viewStart, float viewSize, float speed, float autoSpeed, float offset,
			std::int32_t layerTiles, std::int32_t levelTiles, bool isY)
		{
			constexpr std::int32_t TileSize = Formats::LevelFile::TileSize;
			constexpr std::int32_t HardcodedOffset = LayerDescription::HardcodedOffset;

			switch (model) {
				case LayerSpeedModel::AlwaysOnTop:
					// Tied to the view, the offset doesn't move it either
					return -HardcodedOffset;
				case LayerSpeedModel::FitLevel:
					// The whole layer between the two ends of the level, whatever the speeds are
					return GetLevelProgress(viewStart, viewSize, levelTiles) * (float(layerTiles * TileSize) - viewSize) + offset - HardcodedOffset;
				case LayerSpeedModel::SpeedMultipliers: {
					// The speed of the size of the view at the start of the level and the auto speed at its end
					float progress = GetLevelProgress(viewStart, viewSize, levelTiles);
					return -viewSize * (speed + (autoSpeed - speed) * progress) + offset - HardcodedOffset;
				}
				case LayerSpeedModel::FromStart:
					// The origin at the start of the level, whatever the size of the view
					return viewStart * speed + offset - HardcodedOffset;
				default:
					return LayerDescription::TranslateCoordinate(viewStart - HardcodedOffset, speed, offset, std::int32_t(viewSize), isY);
			}
		}
	}

	Vector2f LayerDescription::GetViewCoordinates(Vector2i layoutSize, Vector2i spriteLayoutSize, const Rectf& placementRect,
		float loX, float loY) const
	{
		return Vector2f(
			GetViewCoordinate(SpeedModelX, placementRect.X, placementRect.W, SpeedX, AutoSpeedX, loX, layoutSize.X, spriteLayoutSize.X, false),
			GetViewCoordinate(SpeedModelY, placementRect.Y, placementRect.H, SpeedY, AutoSpeedY, loY, layoutSize.Y, spriteLayoutSize.Y, true));
	}

	float LayerDescription::TranslateCoordinate(float coordinate, float speed, float offset, std::int32_t viewSize, bool isY)
	{
		std::int32_t alignment = ((isY ? (viewSize - 200) : (viewSize - 320)) / 2) + HardcodedOffset;
		return (coordinate * speed + offset + alignment * (speed - 1.0f));
	}
}
