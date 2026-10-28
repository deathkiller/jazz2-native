#include "EventPreviews.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../ContentResolver.h"
#include "../../Resources.h"
#include "../../Actors/Enemies/Bosses/Bolly.h"
#include "../../Actors/Solid/Bridge.h"
#include "../../UI/Canvas.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Death::Containers::Literals;
using namespace Jazz2::Resources;

namespace Jazz2::Editor
{
	namespace
	{
		/**
		 * Turns a rectangle by quarter turns clockwise around a point and returns the angle to draw it at, the canvas
		 * turns a rectangle around its center. With @p wholePixels the turned rectangle starts at a whole pixel.
		 */
		float TurnRect(Rectf& rect, Vector2f pivot, std::uint8_t quarterTurns, bool wholePixels)
		{
			quarterTurns &= 3;
			Vector2f offset(rect.X + rect.W * 0.5f - pivot.X, rect.Y + rect.H * 0.5f - pivot.Y);
			for (std::uint8_t i = 0; i < quarterTurns; i++) {
				// Clockwise with Y going down
				offset = Vector2f(-offset.Y, offset.X);
			}
			Vector2f center = pivot + offset;
			// What is seen is a rectangle on its side after an odd number of turns
			Vector2f seen = ((quarterTurns & 1) != 0 ? Vector2f(rect.H, rect.W) : Vector2f(rect.W, rect.H));
			if (wholePixels) {
				center.X = std::round(center.X - seen.X * 0.5f) + seen.X * 0.5f;
				center.Y = std::round(center.Y - seen.Y * 0.5f) + seen.Y * 0.5f;
			}
			rect.X = center.X - rect.W * 0.5f;
			rect.Y = center.Y - rect.H * 0.5f;
			return float(quarterTurns) * fPiOver2;
		}

		// How the game spawns its own events is known here, by their parameters as the game reads them. An event the
		// game knows that a level or an event pack describes in its own way may mean something else by them, it's
		// shown only as its description says then.
		bool HasBuiltInLayout(std::uint16_t type)
		{
			return !EventCatalog::IsExtension(EventCatalog::Find(EventType(type)));
		}

		// Lines of a label of a box can be closer to each other than lines of a text, there are no accents above them
		constexpr float BoxLabelLineStep = 0.8f;

		// The translated name of an event type, or its number in the buffer if it isn't known
		StringView GetEventNameView(std::uint16_t type, const std::uint8_t* params, char (&buffer)[16])
		{
			if (const EventDescriptor* descriptor = EventCatalog::Find(EventType(type), params)) {
				return descriptor->Name;
			}
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "#%u", std::uint32_t(type));
			return StringView(buffer, std::size_t(std::max<std::int32_t>(length, 0)));
		}

		// Sprites often reach over other tiles, a square in the corner tells which tile the event is at. An event off the grid is
		// at no tile, it has a triangle there instead, a quarter longer on each side.
		void DrawCornerMark(UiPainter& painter, const Rectf& tileRect, Colorf color, float alpha, bool offGrid)
		{
			if (tileRect.W < 10.0f) {
				return;
			}
			float size = std::max(2.0f, std::round(tileRect.W / 8.0f));
			color.A *= alpha;
			if (!offGrid) {
				painter.FillRect(Rectf(tileRect.X, tileRect.Y, size, size), color, UiLayer::Overlay);
				return;
			}
			float corner = std::round(size * 1.25f);
			float x = std::round(tileRect.X), y = std::round(tileRect.Y);
			for (float row = 0.0f; row < corner; row += 1.0f) {
				painter.FillRect(Rectf(x, y + row, corner - row, 1.0f), color, UiLayer::Overlay);
			}
		}
	}

	EventPreviews::EventPreviews()
	{
		_sprites.resize(std::size_t(EventType::Count) + 1);
	}

	EventPreviews::~EventPreviews()
	{
		Clear();
	}

	bool EventPreviews::GetPreview(const Formats::EventRecord& event, EventPreviewSpec& spec)
	{
		spec = EventPreviewSpec();
		if (EventType(event.Type) == EventType::LevelStart || EventType(event.Type) == EventType::LevelStartMultiplayer) {
			// The first character that starts there, as a list shows it, see DrawStart() for the level
			std::uint8_t players = (EventType(event.Type) == EventType::LevelStart ? (event.Params[0] & 0x07) : 0x01);
			spec.Metadata = ((players & 0x01) != 0 || players == 0 ? "Interactive/PlayerJazz" : ((players & 0x02) != 0 ? "Interactive/PlayerSpaz" : "Interactive/PlayerLori"));
			spec.Animation = std::uint32_t(AnimState::Fall);
			return true;
		}
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		bool builtIn = !EventCatalog::IsExtension(descriptor);
		if (builtIn && ResolveEventPreview(event, spec)) {
			return (spec.Metadata != nullptr);
		}
		if (descriptor == nullptr) {
			return false;
		}
		// By the value of a parameter, as the catalog says, or as the type looks
		if (const EventPreviewCase* previewCase = EventCatalog::FindPreviewCase(*descriptor, event)) {
			if (previewCase->Metadata == nullptr) {
				return false;
			}
			spec.Metadata = previewCase->Metadata;
			spec.Animation = previewCase->Animation;
			spec.FlipX = previewCase->FlipX;
			spec.Offset = Vector2f(float(previewCase->OffsetX), float(previewCase->OffsetY));
		} else if (descriptor->PreviewMetadata != nullptr) {
			spec.Metadata = descriptor->PreviewMetadata;
			spec.Animation = descriptor->PreviewAnimation;
		} else {
			// An event of the game described again without a sprite (as MLLE describes checkpoints) is the
			// object of the game still, the converter left its parameters in their places
			return (!builtIn && ResolveEventPreview(event, spec) && spec.Metadata != nullptr);
		}
		if (builtIn && EventType(event.Type) == EventType::BossUterus) {
			// Its sprite lies on its side, Uterus::OnUpdate() stands it up by a quarter turn
			spec.QuarterTurns = 1;
		}
		return true;
	}

	GraphicResource* EventPreviews::GetSprite(std::uint16_t type, const EventPreviewSpec& spec)
	{
		if (spec.Metadata == nullptr) {
			return nullptr;
		}

		// Types the game doesn't know, described from outside of it, are few and share the last list
		auto& entries = _sprites[std::min(std::size_t(type), _sprites.size() - 1)];
		for (const SpriteEntry& entry : entries) {
			if (entry.Animation == spec.Animation && entry.Metadata == StringView(spec.Metadata)) {
				return entry.Resource;
			}
		}

		// Not found yet, a sprite that can't be loaded is remembered too, so it's not tried again
		GraphicResource* resource = nullptr;
		ContentResolver& resolver = ContentResolver::Get();
		if (Metadata* metadata = resolver.RequestMetadata(spec.Metadata)) {
			// Loading the level to play it would release whatever that level doesn't use
			if (std::find(_pinned.begin(), _pinned.end(), metadata) == _pinned.end()) {
				resolver.PinMetadata(metadata, true);
				_pinned.push_back(metadata);
			}
			resource = metadata->FindAnimation(AnimState(spec.Animation));
			if (resource != nullptr && (resource->Base == nullptr || resource->Base->TextureDiffuse == nullptr)) {
				resource = nullptr;
			}
		}
		entries.push_back(SpriteEntry{ String(spec.Metadata), spec.Animation, resource });
		return resource;
	}

	GraphicResource* EventPreviews::GetScriptSprite(const Formats::EventRecord& event)
	{
		if (!ScriptSprite) {
			return nullptr;
		}
		// Only an object has a template, the events that change tiles share the numbers of the original with the bullets
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		if (descriptor == nullptr || (descriptor->Traits & EventTraits::Object) != EventTraits::Object) {
			return nullptr;
		}
		// An object only given other frames animates as the object of the game does (a restyled checkpoint stays closed)
		GraphicResource* scripted = ScriptSprite(event, true, true);
		if (scripted == nullptr || scripted->FrameCount <= 1) {
			return scripted;
		}
		EventPreviewSpec spec;
		GraphicResource* own = (GetPreview(event, spec) ? GetSprite(event.Type, spec) : nullptr);
		// A container animates only if the game's own does (monitors do, crates don't)
		if (descriptor->Category == EventCategory::Container) {
			return (own != nullptr && own->FrameCount > 1 ? scripted : ScriptSprite(event, false, false));
		}
		return (own != nullptr && own->FrameCount <= 1 ? ScriptSprite(event, true, false) : scripted);
	}

	bool EventPreviews::GetPlacedPreview(const Formats::EventRecord& event, Vector2f spawnPos, EventPreviewSpec& spec)
	{
		if (!GetPreview(event, spec)) {
			return false;
		}
		if (!HasBuiltInLayout(event.Type)) {
			return true;
		}
		switch (EventType(event.Type)) {
			case EventType::Ammo:
			case EventType::Carrot:
			case EventType::CarrotFly:
			case EventType::CarrotInvincible:
			case EventType::Coin:
			case EventType::FastFire:
			case EventType::Food:
			case EventType::Gem:
			case EventType::Stopwatch:
				// Collectibles face left or right by their position, see CollectibleBase::SetFacingDirection()
				spec.FlipX ^= ((std::int32_t(spawnPos.X + spawnPos.Y) / 32) & 1) == 0;
				break;
			case EventType::Spring:
				if (event.Params[1] > 3 && IsAreaEmpty) {
					// Spring::OnActivatedAsync() turns a horizontal spring of no orientation by the tile to its right
					AABBf aabb(spawnPos.X + 6.0f, spawnPos.Y - 2.0f, spawnPos.X + 26.0f, spawnPos.Y);
					bool right = (IsAreaEmpty(aabb) != (event.Params[1] == 5));
					spec.FlipX = !right;
				}
				break;
			default:
				break;
		}
		return true;
	}

	bool EventPreviews::HasSceneSprite(const Formats::EventRecord& event)
	{
		// A start isn't an object of the level, it's shown over it
		if (EventType(event.Type) == EventType::LevelStart || EventType(event.Type) == EventType::LevelStartMultiplayer) {
			return false;
		}
		if (GetScriptSprite(event) != nullptr) {
			return true;
		}
		EventPreviewSpec spec;
		return (GetPreview(event, spec) && GetSprite(event.Type, spec) != nullptr);
	}

	bool EventPreviews::DrawInScene(UI::Canvas& canvas, const Formats::EventRecord& event, Vector2f spawnPos, float alpha, float animTime, std::uint16_t z)
	{
		if (EventType(event.Type) == EventType::LevelStart || EventType(event.Type) == EventType::LevelStartMultiplayer) {
			return false;
		}
		DrawTarget target;
		target.Canvas = &canvas;
		target.Z = z;
		if (GraphicResource* scripted = GetScriptSprite(event)) {
			DrawSprite(target, scripted, EventPreviewSpec(), spawnPos, alpha, animTime);
			return true;
		}
		EventPreviewSpec spec;
		if (!GetPlacedPreview(event, spawnPos, spec)) {
			return false;
		}
		GraphicResource* resource = GetSprite(event.Type, spec);
		if (resource == nullptr) {
			return false;
		}
		DrawPlacedSprite(target, event, resource, spec, spawnPos, alpha, animTime);
		return true;
	}

	void EventPreviews::DrawPlacedSprite(const DrawTarget& target, const Formats::EventRecord& event, GraphicResource* resource, const EventPreviewSpec& spec,
		Vector2f spawnPos, float alpha, float animTime)
	{
		if (EventType(event.Type) == EventType::Bridge && HasBuiltInLayout(event.Type)) {
			// All of its pieces, as long as it is, each a frame of its own in turn, see Bridge::OnDraw()
			SmallVector<std::int32_t, 64> offsets;
			std::int32_t width = std::int32_t(event.Params[0] | (event.Params[1] << 8)) * 16;
			Actors::Solid::Bridge::GetPieceOffsets(event.Params[2], width, resource, offsets);
			std::int32_t frameCount = std::max(resource->FrameCount, std::int32_t(1));
			for (std::size_t i = 0; i < offsets.size(); i++) {
				Vector2f piece(spawnPos.X + float(offsets[i]), spawnPos.Y - 6.0f);
				DrawSprite(target, resource, spec, piece, alpha, animTime, resource->FrameOffset + std::int32_t(i % std::size_t(frameCount)));
			}
			return;
		}
		if (EventType(event.Type) == EventType::GemRing && HasBuiltInLayout(event.Type)) {
			DrawGemRing(target, event, resource, spawnPos, alpha, animTime);
			return;
		}
		// Objects are spawned at the center of their tile, and drawn at their hotspot, with the parts they add to
		// themselves in front of them or behind them
		SmallVector<ExtraPart, 0> parts;
		GetExtraParts(event, spec, animTime, parts);
		std::size_t i = 0;
		for (; i < parts.size() && parts[i].Depth < 0; i++) {
			DrawExtraPart(target, event.Type, parts[i], spawnPos, alpha, animTime);
		}
		DrawSprite(target, resource, spec, spawnPos + spec.Offset, alpha, animTime);
		for (; i < parts.size(); i++) {
			DrawExtraPart(target, event.Type, parts[i], spawnPos, alpha, animTime);
		}
	}

	void EventPreviews::GetExtraParts(const Formats::EventRecord& event, const EventPreviewSpec& spec, float animTime, SmallVectorImpl<ExtraPart>& parts)
	{
		if (!HasBuiltInLayout(event.Type)) {
			return;
		}
		switch (EventType(event.Type)) {
			case EventType::EnemyLizardFloat: {
				// Its copter, see LizardFloat::OnActivatedAsync() and OnUpdate()
				ExtraPart& copter = parts.emplace_back();
				copter.Spec.Metadata = "Enemy/LizardFloat";
				copter.Spec.Animation = 1;
				copter.Spec.Offset = spec.Offset + Vector2f(0.0f, 4.0f);
				copter.Depth = -2;
				break;
			}
			case EventType::BossBolly: {
				// The chain swinging behind it and the part below it, see Bolly::OnActivatedAsync() and OnUpdate(),
				// the chain swings as fast as there, by 0.06 a frame
				using Actors::Bosses::Bolly;
				SmallVector<Vector2f, Bolly::NormalChainLength> offsets;
				Bolly::GetChainOffsets(animTime * (0.06f / UI::Canvas::AnimTimeMultiplier), Bolly::NormalChainLength, offsets);
				// The small pieces are further than the balls
				for (std::int32_t pass = 0; pass < 2; pass++) {
					for (std::int32_t j = 0; j < std::int32_t(offsets.size()); j++) {
						if (Bolly::IsChainBall(j) != (pass == 1)) {
							continue;
						}
						ExtraPart& piece = parts.emplace_back();
						piece.Spec.Metadata = "Boss/Bolly";
						piece.Spec.Animation = (pass == 1 ? Bolly::ChainBallAnimation : Bolly::ChainPieceAnimation);
						piece.Spec.Offset = spec.Offset + offsets[j];
						piece.Depth = (pass == 1 ? -2 : -4);
					}
				}
				ExtraPart& bottom = parts.emplace_back();
				bottom.Spec.Metadata = "Boss/Bolly";
				bottom.Spec.Animation = Bolly::BottomAnimation;
				bottom.Spec.FlipX = spec.FlipX;
				bottom.Spec.Offset = spec.Offset;
				bottom.Depth = 2;
				break;
			}
			default:
				break;
		}
	}

	void EventPreviews::DrawExtraPart(const DrawTarget& target, std::uint16_t type, const ExtraPart& part, Vector2f spawnPos, float alpha, float animTime)
	{
		GraphicResource* resource = GetSprite(type, part.Spec);
		if (resource == nullptr) {
			return;
		}
		DrawTarget partTarget = target;
		if (partTarget.Canvas != nullptr) {
			partTarget.Z = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(target.Z) + part.Depth, 0, 0xFFFF));
		}
		DrawSprite(partTarget, resource, part.Spec, spawnPos + part.Spec.Offset, alpha, animTime);
	}

	void EventPreviews::DrawPlaced(UiPainter& painter, const Formats::EventRecord& event, const Rectf& tileRect, float scale, float alpha, float animTime,
		Vector2f spawnPos, bool inScene, bool offGrid)
	{
		if (DrawStart(painter, event, tileRect, scale, alpha, animTime, offGrid)) {
			return;
		}

		if (GraphicResource* scripted = GetScriptSprite(event)) {
			if (!inScene) {
				DrawTarget target;
				target.Painter = &painter;
				target.Scale = scale;
				target.Origin = Vector2f(tileRect.X + tileRect.W * 0.5f, tileRect.Y + tileRect.H * 0.5f) - spawnPos * scale;
				DrawSprite(target, scripted, EventPreviewSpec(), spawnPos, alpha, animTime);
			}
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
			DrawCornerMark(painter, tileRect, GetCategoryColor(descriptor != nullptr ? descriptor->Category : EventCategory::Other), alpha, offGrid);
			return;
		}

		EventPreviewSpec spec;
		if (GetPlacedPreview(event, spawnPos, spec)) {
			if (GraphicResource* resource = GetSprite(event.Type, spec)) {
				if (!inScene) {
					// The center of the tile on the screen is where the object is spawned in the level
					DrawTarget target;
					target.Painter = &painter;
					target.Scale = scale;
					target.Origin = Vector2f(tileRect.X + tileRect.W * 0.5f, tileRect.Y + tileRect.H * 0.5f) - spawnPos * scale;
					DrawPlacedSprite(target, event, resource, spec, spawnPos, alpha, animTime);
				}

				const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
				DrawCornerMark(painter, tileRect, GetCategoryColor(descriptor != nullptr ? descriptor->Category : EventCategory::Other), alpha, offGrid);
				return;
			}
		}

		float inset = std::max(1.0f, std::round(tileRect.W / 16.0f));
		DrawBox(painter, event.Type, event.Params, Rectf(tileRect.X + inset, tileRect.Y + inset, tileRect.W - inset * 2.0f, tileRect.H - inset * 2.0f), alpha, true);
		// The box itself shows the tile, only one off the grid needs the mark
		if (offGrid) {
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
			DrawCornerMark(painter, tileRect, GetCategoryColor(descriptor != nullptr ? descriptor->Category : EventCategory::Other), alpha, true);
		}
	}

	bool EventPreviews::DrawStart(UiPainter& painter, const Formats::EventRecord& event, const Rectf& tileRect, float scale, float alpha, float animTime, bool offGrid)
	{
		// The characters that start there, next to each other
		std::uint8_t players;
		if (EventType(event.Type) == EventType::LevelStart) {
			players = (event.Params[0] & 0x07);
		} else if (EventType(event.Type) == EventType::LevelStartMultiplayer) {
			players = 0x01;
		} else {
			return false;
		}
		if (players == 0) {
			return false;
		}

		static const char* const Characters[] = { "Interactive/PlayerJazz", "Interactive/PlayerSpaz", "Interactive/PlayerLori" };
		std::int32_t count = ((players & 0x01) != 0) + ((players & 0x02) != 0) + ((players & 0x04) != 0);
		constexpr float Spacing = 12.0f;
		float offset = -Spacing * float(count - 1) * 0.5f;
		Vector2f center(tileRect.X + tileRect.W * 0.5f, tileRect.Y + tileRect.H * 0.5f);
		bool drawn = false;
		for (std::int32_t i = 0; i < 3; i++) {
			if ((players & (1 << i)) == 0) {
				continue;
			}
			EventPreviewSpec spec;
			spec.Metadata = Characters[i];
			// Falling, as the player comes into the level, and see-through, it's not there yet
			spec.Animation = std::uint32_t(AnimState::Fall);
			spec.Offset = Vector2f(offset, 0.0f);
			if (GraphicResource* resource = GetSprite(event.Type, spec)) {
				DrawTarget target;
				target.Painter = &painter;
				target.Scale = scale;
				target.Origin = center;
				DrawSprite(target, resource, spec, spec.Offset, alpha * 0.7f, animTime);
				drawn = true;
			}
			offset += Spacing;
		}
		if (!drawn) {
			return false;
		}

		// Which tile the start is at
		DrawCornerMark(painter, tileRect, GetCategoryColor(EventCategory::Basic), alpha, offGrid);
		return true;
	}

	void EventPreviews::DrawFitted(UiPainter& painter, std::uint16_t type, const Rectf& rect, float animTime, float alpha)
	{
		DrawFitted(painter, EventCatalog::MakeDefault(EventType(type)), rect, animTime, alpha);
	}

	void EventPreviews::DrawFitted(UiPainter& painter, const Formats::EventRecord& event, const Rectf& rect, float animTime, float alpha)
	{
		EventPreviewSpec spec;
		GraphicResource* resource = GetScriptSprite(event);
		bool scripted = (resource != nullptr);
		if (!scripted) {
			resource = (GetPreview(event, spec) ? GetSprite(event.Type, spec) : nullptr);
		}
		if (resource == nullptr) {
			DrawBox(painter, event.Type, event.Params, rect, alpha, false);
			// What the game passes to the scripts of the level, as kept or as the level describes it
			bool passthrough = (EventType(event.Type) == EventType::PassthroughEvent ||
				EventCatalog::IsLevelExtension(EventCatalog::Find(EventType(event.Type), event.Params)));
			painter.Icon(passthrough ? EditorIcon::Passthrough : EditorIcon::Event, rect, Colorf(1.0f, 1.0f, 1.0f, 0.7f * alpha), UiLayer::Overlay);
			return;
		}

		// The parts an object adds to itself are shown too, in place, so it doesn't move while it's animated
		SmallVector<ExtraPart, 0> parts;
		if (!scripted) {
			GetExtraParts(event, spec, 0.0f, parts);
		}
		if (!parts.empty()) {
			DrawFittedWithParts(painter, event.Type, resource, spec, parts, rect, animTime, alpha);
			return;
		}
		if (!scripted && EventType(event.Type) == EventType::GemRing && HasBuiltInLayout(event.Type)) {
			float extent = GetGemRingExtent(resource);
			float ringScale = std::min(rect.W, rect.H) / (extent * 2.0f);
			DrawTarget target;
			target.Painter = &painter;
			target.Scale = (ringScale >= 1.0f ? std::floor(ringScale) : ringScale);
			target.Origin = Vector2f(std::round(rect.X + rect.W * 0.5f), std::round(rect.Y + rect.H * 0.5f));
			DrawGemRing(target, event, resource, Vector2f::Zero, alpha, animTime);
			return;
		}

		// The whole cell fits, so the sprite doesn't jump around while it's animated
		GenericGraphicResource* base = resource->Base;
		Vector2f cell = base->FrameDimensions.As<float>();
		if (cell.X <= 0.0f || cell.Y <= 0.0f) {
			return;
		}
		// A cell on its side fits by its other sides
		bool sideways = ((spec.QuarterTurns & 1) != 0);
		float scale = (sideways ? std::min(rect.W / cell.Y, rect.H / cell.X) : std::min(rect.W / cell.X, rect.H / cell.Y));
		if (scale >= 1.0f) {
			scale = std::floor(scale);
		}

		std::int32_t frame = resource->GetFrameForTime(animTime);
		Recti frameRect = base->GetFrameRect(frame);
		Vector2i frameOffset = base->GetFrameOffset(frame);
		if (spec.FlipX) {
			frameOffset.X = base->FrameDimensions.X - frameOffset.X - frameRect.W;
		}
		if (spec.FlipY) {
			frameOffset.Y = base->FrameDimensions.Y - frameOffset.Y - frameRect.H;
		}
		Vector2f cellPos(std::round(rect.X + (rect.W - cell.X * scale) * 0.5f), std::round(rect.Y + (rect.H - cell.Y * scale) * 0.5f));
		Rectf target(cellPos.X + frameOffset.X * scale, cellPos.Y + frameOffset.Y * scale, frameRect.W * scale, frameRect.H * scale);

		Vector2i texSize = base->TextureDiffuse->GetSize();
		Vector4f texCoords(float(frameRect.W) / float(texSize.X), float(frameRect.X) / float(texSize.X),
			float(frameRect.H) / float(texSize.Y), float(frameRect.Y) / float(texSize.Y));
		if (spec.FlipX) {
			texCoords.Y += texCoords.X;
			texCoords.X = -texCoords.X;
		}
		if (spec.FlipY) {
			texCoords.W += texCoords.Z;
			texCoords.Z = -texCoords.Z;
		}
		// Turned around the center of the cell, which is the center of the rectangle
		float angle = 0.0f;
		if ((spec.QuarterTurns & 3) != 0) {
			angle = TurnRect(target, Vector2f(cellPos.X + cell.X * scale * 0.5f, cellPos.Y + cell.Y * scale * 0.5f), spec.QuarterTurns, true);
		}
		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? resource->PaletteOffset : -1);
		painter.Image(*base->TextureDiffuse, target, texCoords, Colorf(1.0f, 1.0f, 1.0f, alpha), paletteOffset, UiLayer::Content, angle);
	}

	void EventPreviews::DrawFittedWithParts(UiPainter& painter, std::uint16_t type, GraphicResource* resource, const EventPreviewSpec& spec,
		ArrayView<const ExtraPart> parts, const Rectf& rect, float animTime, float alpha)
	{
		// Whole cells of all of them around their hotspots, in pixels of the level from the position of the object
		const auto getCellBounds = [](GraphicResource* resource, const EventPreviewSpec& spec) {
			GenericGraphicResource* base = resource->Base;
			Vector2f size = base->FrameDimensions.As<float>();
			float x = (spec.FlipX ? float(base->Hotspot.X) - size.X : -float(base->Hotspot.X));
			float y = (spec.FlipY ? float(base->Hotspot.Y) - size.Y : -float(base->Hotspot.Y));
			return Rectf(spec.Offset.X + x, spec.Offset.Y + y, size.X, size.Y);
		};
		Rectf bounds = getCellBounds(resource, spec);
		for (const ExtraPart& part : parts) {
			if (GraphicResource* partResource = GetSprite(type, part.Spec)) {
				Rectf partBounds = getCellBounds(partResource, part.Spec);
				float right = std::max(bounds.X + bounds.W, partBounds.X + partBounds.W);
				float bottom = std::max(bounds.Y + bounds.H, partBounds.Y + partBounds.H);
				bounds.X = std::min(bounds.X, partBounds.X);
				bounds.Y = std::min(bounds.Y, partBounds.Y);
				bounds.W = right - bounds.X;
				bounds.H = bottom - bounds.Y;
			}
		}
		if (bounds.W <= 0.0f || bounds.H <= 0.0f) {
			return;
		}

		float scale = std::min(rect.W / bounds.W, rect.H / bounds.H);
		if (scale >= 1.0f) {
			scale = std::floor(scale);
		}
		DrawTarget target;
		target.Painter = &painter;
		target.Scale = scale;
		target.Origin = Vector2f(std::round(rect.X + rect.W * 0.5f - (bounds.X + bounds.W * 0.5f) * scale),
			std::round(rect.Y + rect.H * 0.5f - (bounds.Y + bounds.H * 0.5f) * scale));

		std::size_t i = 0;
		for (; i < parts.size() && parts[i].Depth < 0; i++) {
			DrawExtraPart(target, type, parts[i], Vector2f::Zero, alpha, animTime);
		}
		DrawSprite(target, resource, spec, spec.Offset, alpha, animTime);
		for (; i < parts.size(); i++) {
			DrawExtraPart(target, type, parts[i], Vector2f::Zero, alpha, animTime);
		}
	}

	void EventPreviews::Clear()
	{
		// Its samples can go away with the metadata
		StopSound();

		ContentResolver& resolver = ContentResolver::Get();
		for (Metadata* metadata : _pinned) {
			resolver.PinMetadata(metadata, false);
		}
		_pinned.clear();

		for (auto& entries : _sprites) {
			entries.clear();
		}
		_boxLabels.clear();
	}

	Colorf EventPreviews::GetCategoryColor(EventCategory category)
	{
		switch (category) {
			case EventCategory::Basic: return Colorf(0.30f, 0.80f, 0.35f, 1.0f);
			case EventCategory::Scenery: return Colorf(0.72f, 0.50f, 0.26f, 1.0f);
			case EventCategory::Modifier: return Colorf(0.25f, 0.70f, 0.88f, 1.0f);
			case EventCategory::Area: return Colorf(0.62f, 0.42f, 0.92f, 1.0f);
			case EventCategory::Trigger: return Colorf(0.96f, 0.55f, 0.20f, 1.0f);
			case EventCategory::Warp: return Colorf(0.32f, 0.50f, 1.00f, 1.0f);
			case EventCategory::Light: return Colorf(1.00f, 0.90f, 0.40f, 1.0f);
			case EventCategory::Environment: return Colorf(0.30f, 0.80f, 0.70f, 1.0f);
			case EventCategory::Enemy: return Colorf(0.92f, 0.30f, 0.30f, 1.0f);
			case EventCategory::Boss: return Colorf(0.78f, 0.15f, 0.35f, 1.0f);
			case EventCategory::Collectible: return Colorf(0.96f, 0.76f, 0.25f, 1.0f);
			case EventCategory::Container: return Colorf(0.76f, 0.60f, 0.40f, 1.0f);
			default: return Colorf(0.60f, 0.60f, 0.66f, 1.0f);
		}
	}

	String EventPreviews::GetEventName(std::uint16_t type, const std::uint8_t* params)
	{
		char buffer[16];
		return GetEventNameView(type, params, buffer);
	}

	void EventPreviews::DrawSprite(const DrawTarget& drawTarget, GraphicResource* resource, const EventPreviewSpec& spec, Vector2f pos, float alpha,
		float animTime, std::int32_t frame)
	{
		GenericGraphicResource* base = resource->Base;
		if (frame < 0) {
			frame = resource->GetFrameForTime(animTime);
		}
		Recti frameRect = base->GetFrameRect(frame);
		// At its hotspot
		Vector2i anchor = base->GetFrameAnchor(frame, spec.FlipX, spec.FlipY);
		Vector2f topLeft = Vector2f(pos.X - float(anchor.X), pos.Y - float(anchor.Y));
		Rectf target(drawTarget.Origin.X + topLeft.X * drawTarget.Scale, drawTarget.Origin.Y + topLeft.Y * drawTarget.Scale,
			frameRect.W * drawTarget.Scale, frameRect.H * drawTarget.Scale);
		// Whole pixels in the interface, so a sprite at a whole scale stays sharp; turned around the position, its
		// hotspot, as the game turns an object around its position
		float angle = 0.0f;
		if ((spec.QuarterTurns & 3) != 0) {
			angle = TurnRect(target, drawTarget.Origin + pos * drawTarget.Scale, spec.QuarterTurns, drawTarget.Painter != nullptr);
		} else if (drawTarget.Painter != nullptr) {
			target.X = std::round(target.X);
			target.Y = std::round(target.Y);
		}

		Vector2i texSize = base->TextureDiffuse->GetSize();
		Vector4f texCoords(float(frameRect.W) / float(texSize.X), float(frameRect.X) / float(texSize.X),
			float(frameRect.H) / float(texSize.Y), float(frameRect.Y) / float(texSize.Y));
		// A negative extent samples the frame backwards
		if (spec.FlipX) {
			texCoords.Y += texCoords.X;
			texCoords.X = -texCoords.X;
		}
		if (spec.FlipY) {
			texCoords.W += texCoords.Z;
			texCoords.Z = -texCoords.Z;
		}
		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? resource->PaletteOffset : -1);
		if (drawTarget.Painter != nullptr) {
			drawTarget.Painter->Image(*base->TextureDiffuse, target, texCoords, Colorf(1.0f, 1.0f, 1.0f, alpha), paletteOffset, UiLayer::Content, angle);
		} else if (drawTarget.Canvas != nullptr) {
			drawTarget.Canvas->DrawTexture(*base->TextureDiffuse, Vector2f(target.X, target.Y), drawTarget.Z, Vector2f(target.W, target.H), texCoords,
				Colorf(1.0f, 1.0f, 1.0f, alpha), UI::BlendMode::Normal, angle, paletteOffset);
		}
	}

	void EventPreviews::DrawCentered(const DrawTarget& drawTarget, GraphicResource* resource, std::int32_t frame, Vector2f pos, float scale, float angle,
		float alpha)
	{
		GenericGraphicResource* base = resource->Base;
		Recti frameRect = base->GetFrameRect(frame);
		Vector2f size(float(frameRect.W) * scale * drawTarget.Scale, float(frameRect.H) * scale * drawTarget.Scale);
		Vector2f center = drawTarget.Origin + pos * drawTarget.Scale;
		Rectf target(center.X - size.X * 0.5f, center.Y - size.Y * 0.5f, size.X, size.Y);

		Vector2i texSize = base->TextureDiffuse->GetSize();
		Vector4f texCoords(float(frameRect.W) / float(texSize.X), float(frameRect.X) / float(texSize.X),
			float(frameRect.H) / float(texSize.Y), float(frameRect.Y) / float(texSize.Y));
		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? resource->PaletteOffset : -1);
		if (drawTarget.Painter != nullptr) {
			drawTarget.Painter->Image(*base->TextureDiffuse, target, texCoords, Colorf(1.0f, 1.0f, 1.0f, alpha), paletteOffset, UiLayer::Content, angle);
		} else if (drawTarget.Canvas != nullptr) {
			drawTarget.Canvas->DrawTexture(*base->TextureDiffuse, Vector2f(target.X, target.Y), drawTarget.Z, Vector2f(target.W, target.H), texCoords,
				Colorf(1.0f, 1.0f, 1.0f, alpha), UI::BlendMode::Normal, angle, paletteOffset);
		}
	}

	void EventPreviews::DrawGemRing(const DrawTarget& target, const Formats::EventRecord& event, GraphicResource* resource, Vector2f center, float alpha,
		float animTime)
	{
		// GemRing::OnActivatedAsync(), OnUpdate() and OnDraw(): smaller see-through gems behind the objects, each its own frame
		std::int32_t length = (event.Params[0] > 0 ? event.Params[0] : 8);
		float speed = float(event.Params[1] > 0 ? event.Params[1] : 8) * 0.00625f;
		float phase = animTime * (speed / UI::Canvas::AnimTimeMultiplier);
		float distance = 8.0f * (4.0f + std::sin(phase * 1.1f));
		DrawTarget pieceTarget = target;
		if (pieceTarget.Canvas != nullptr) {
			pieceTarget.Z = std::uint16_t(std::max<std::int32_t>(std::int32_t(target.Z) - 2, 0));
		}
		std::int32_t frameCount = std::max(resource->FrameCount, std::int32_t(1));
		for (std::int32_t i = 0; i < length; i++) {
			float angle = phase + float(i) * fTwoPi / float(length);
			Vector2f pos(center.X + std::cos(angle) * distance, center.Y + std::sin(angle) * distance);
			DrawCentered(pieceTarget, resource, resource->FrameOffset + (i % frameCount), pos, 0.8f, angle + fPiOver2, alpha * 0.7f);
		}
	}

	float EventPreviews::GetGemRingExtent(GraphicResource* resource)
	{
		Vector2i cell = resource->Base->FrameDimensions;
		return 40.0f + 0.4f * float(std::max(cell.X, cell.Y));
	}

	void EventPreviews::DrawBox(UiPainter& painter, std::uint16_t type, const std::uint8_t* params, const Rectf& rect, float alpha, bool withLabel)
	{
		if (rect.W <= 0.0f || rect.H <= 0.0f) {
			return;
		}

		const EventDescriptor* descriptor = EventCatalog::Find(EventType(type), params);
		Colorf color = (descriptor != nullptr ? GetCategoryColor(descriptor->Category) : Colorf(0.5f, 0.5f, 0.5f, 1.0f));
		Colorf fill(color.R * 0.6f, color.G * 0.6f, color.B * 0.6f, 0.55f * alpha);
		Colorf border(color.R, color.G, color.B, 0.95f * alpha);
		// Many boxes are drawn when the view is far, a rounded one is 18 rectangles, a plain one with a border 5 and one
		// without 1, as small as they are then, they look the same
		if (rect.W >= 20.0f) {
			painter.RoundRect(rect, fill, UiLayer::Content);
			painter.RoundRectBorder(rect, border, UiLayer::Content);
		} else if (rect.W >= 12.0f) {
			painter.FillRect(rect, fill, UiLayer::Content);
			painter.Frame(rect, border, 1.0f, UiLayer::Content);
		} else {
			painter.FillRect(rect, border, UiLayer::Content);
		}

		if (!withLabel || rect.W < 10.0f) {
			return;
		}

		// Measuring the text is what takes the time, and boxes are drawn for many events in every frame, so the layout
		// of a label is kept for the type and the size of the box. The name is compared too, the language can change.
		// Zooming changes the size of the boxes in every frame, so a layout is made for sizes a few percent apart, for
		// the size in the middle of them, and its text is scaled with the box, it gets smaller and larger with it.
		char numberBuffer[16];
		StringView name = GetEventNameView(type, params, numberBuffer);
		auto getPadding = [](float width) {
			return std::max(1.0f, std::round(width / 20.0f));
		};
		float padding = getPadding(rect.W);
		Rectf inner(rect.X + padding, rect.Y + padding, rect.W - padding * 2.0f, rect.H - padding * 2.0f);
		constexpr float StepsPerOctave = 16.0f;
		// log() and pow() instead of std::log2() and std::exp2(), which are C99 and not in `std` on every console
		constexpr float Ln2 = 0.69314718f;
		std::int32_t widthStep = std::int32_t(std::round(std::log(rect.W) / Ln2 * StepsPerOctave));
		std::int32_t heightStep = std::int32_t(std::round(std::log(rect.H) / Ln2 * StepsPerOctave));
		float layoutWidth = std::pow(2.0f, float(widthStep) / StepsPerOctave);
		float layoutHeight = std::pow(2.0f, float(heightStep) / StepsPerOctave);
		// An event of the original game a passthrough event keeps by its number there, past the types of the game
		std::uint64_t key = std::uint64_t(descriptor != nullptr && descriptor->OriginalEvent != 0 ? (0x8000u | descriptor->OriginalEvent) : type) |
			(std::uint64_t(std::clamp<std::int32_t>(widthStep + 0x8000, 0, 0xFFFF)) << 16) |
			(std::uint64_t(std::clamp<std::int32_t>(heightStep + 0x8000, 0, 0xFFFF)) << 32) |
			(std::uint64_t(std::clamp<std::int32_t>(std::int32_t(std::round(painter.GetScale() * 64.0f)), 0, 0x7FFF)) << 48) |
			(UiTheme::ReadableFont ? (std::uint64_t(1) << 63) : 0);
		BoxLabel* label;
		auto it = _boxLabels.find(key);
		if (it != _boxLabels.end() && it->second.Name == name) {
			label = &it->second;
		} else {
			if (it == _boxLabels.end() && _boxLabels.size() >= MaxBoxLabels) {
				_boxLabels.clear();
			}
			label = &_boxLabels[key];
			float layoutPadding = getPadding(layoutWidth);
			Rectf layoutInner(0.0f, 0.0f, layoutWidth - layoutPadding * 2.0f, layoutHeight - layoutPadding * 2.0f);
			LayoutBoxLabel(painter, name, layoutInner, layoutWidth, *label);
		}
		if (label->Size <= 0.0f) {
			return;
		}
		// The text is never larger than at the size of the interface, see LayoutBoxLabel()
		float textSize = std::min(label->Size * rect.W / layoutWidth, 1.0f);

		Colorf textColor = UiTheme::Text;
		textColor.A *= alpha;
		float originalSize = painter.GetTextSize();
		painter.SetTextSize(textSize);
		float lineHeight = painter.GetTextHeight();
		float lineStep = std::round(lineHeight * BoxLabelLineStep);
		painter.PushClip(inner);
		float height = (label->Second.empty() ? lineHeight : lineHeight + lineStep);
		float y = std::round(inner.Y + (inner.H - height) * 0.5f);
		painter.Text(label->First, Vector2f(std::round(inner.X + inner.W * 0.5f), y), textColor, Alignment::Top, UiLayer::OverlayText);
		if (!label->Second.empty()) {
			painter.Text(label->Second, Vector2f(std::round(inner.X + inner.W * 0.5f), y + lineStep), textColor, Alignment::Top, UiLayer::OverlayText);
		}
		painter.PopClip();
		painter.SetTextSize(originalSize);
	}

	void EventPreviews::LayoutBoxLabel(UiPainter& painter, StringView name, const Rectf& inner, float boxWidth, BoxLabel& label)
	{
		// The text gets smaller with the box, a box smaller than a tile at the size of the interface has a smaller one
		// too. If it doesn't fit even at the smallest size, it's broken into two lines, or only the initials are left.
		// The readable font gets blurry sooner than the pixel font.
		const float MinTextSize = (UiTheme::ReadableFont ? 0.85f : 0.75f);
		const float MinInitialsSize = (UiTheme::ReadableFont ? 0.6f : 0.5f);
		label.Name = name;
		label.First = {};
		label.Second = {};
		label.Size = 0.0f;

		float originalSize = painter.GetTextSize();
		float maxSize = std::min(1.0f, boxWidth / (32.0f * painter.GetScale()));
		painter.SetTextSize(1.0f);
		float lineHeight = painter.GetTextHeight();
		constexpr float LineStep = BoxLabelLineStep;

		StringView first = name;
		StringView second;
		String initials;
		float size = std::min({ maxSize, inner.W / std::max(painter.MeasureText(name), 1.0f), inner.H / lineHeight });
		if (size < MinTextSize) {
			// Two lines as long as they can be, broken at the space that makes the longer of them the shortest
			const char* breakAt = nullptr;
			float longest = 0.0f;
			for (const char* c = first.begin(); c != first.end(); c++) {
				if (*c != ' ') {
					continue;
				}
				float width = std::max(painter.MeasureText(StringView(first.begin(), std::size_t(c - first.begin()))),
					painter.MeasureText(StringView(c + 1, std::size_t(first.end() - c - 1))));
				if (breakAt == nullptr || width < longest) {
					breakAt = c;
					longest = width;
				}
			}
			if (breakAt != nullptr) {
				size = std::min({ maxSize, inner.W / std::max(longest, 1.0f), inner.H / (lineHeight * (1.0f + LineStep)) });
			}
			if (breakAt != nullptr && size >= MinTextSize) {
				StringView text = first;
				first = StringView(text.begin(), std::size_t(breakAt - text.begin()));
				second = StringView(breakAt + 1, std::size_t(text.end() - breakAt - 1));
			} else {
				// Initials of the words, or the beginning of a single word, whole characters of UTF-8
				auto charLength = [&name](std::size_t i) {
					std::size_t end = i + 1;
					while (end < name.size() && (std::uint8_t(name[end]) & 0xC0) == 0x80) {
						end++;
					}
					return end - i;
				};
				std::int32_t count = 0;
				bool wordStart = true;
				for (std::size_t i = 0; i < name.size() && count < 3; ) {
					std::size_t length = charLength(i);
					if (name[i] == ' ') {
						wordStart = true;
					} else if (wordStart) {
						initials += name.slice(i, i + length);
						wordStart = false;
						count++;
					}
					i += length;
				}
				if (count == 1) {
					std::size_t end = 0;
					for (std::int32_t c = 0; c < 3 && end < name.size(); c++) {
						end += charLength(end);
					}
					initials = name.prefix(end);
				}
				size = std::min({ maxSize, inner.W / std::max(painter.MeasureText(initials), 1.0f), inner.H / lineHeight });
				if (size < MinInitialsSize) {
					// Nothing fits, the box stays without a label
					painter.SetTextSize(originalSize);
					return;
				}
				first = initials;
				second = {};
			}
		}

		painter.SetTextSize(originalSize);
		// Copied, the views point into the name and the initials, which don't outlive this
		label.First = first;
		label.Second = second;
		label.Size = size;
	}
}

#endif
