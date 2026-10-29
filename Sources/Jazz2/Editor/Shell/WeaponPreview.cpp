#include "WeaponPreview.h"

#if defined(WITH_EDITOR)

#include "../Files/EditorSettings.h"
#include "../Tools/EditorContext.h"
#include "../UI/UiPainter.h"
#include "../UI/UiSurface.h"
#include "../UI/UiTheme.h"
#include "../../ContentResolver.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float Pi = 3.14159265f;
		constexpr float LeftWallX = -30.0f;
		// The part of the range shown, in pixels of the level
		constexpr float ViewLeft = -40.0f, ViewTop = FiringRange::CeilingY - 8.0f, ViewWidth = 360.0f,
			ViewHeight = FiringRange::FloorY + 8.0f - ViewTop;

		// An even spread of -1 to 1 in place of random numbers, so the preview doesn't change while it's looked at
		float SpreadSample(std::int32_t i)
		{
			float x = std::fmod(0.5f + float(i) * 0.618034f, 1.0f);
			return x * 2.0f - 1.0f;
		}

		bool IsInside(const Rectf& rect, Vector2f p)
		{
			return (p.X >= rect.X && p.X <= rect.X + rect.W && p.Y >= rect.Y && p.Y <= rect.Y + rect.H);
		}

		Vector2f ClampToRoom(Vector2f p)
		{
			return Vector2f(std::clamp(p.X, LeftWallX, FiringRange::WallX), std::clamp(p.Y, FiringRange::CeilingY, FiringRange::FloorY));
		}

		bool IsOutsideRoom(Vector2f p)
		{
			return (p.X >= FiringRange::WallX || p.X <= LeftWallX || p.Y >= FiringRange::FloorY || p.Y <= FiringRange::CeilingY);
		}

		void SimulateBeam(const WeaponShotOptions& options, float angle, const FiringRange& range, SimulatedShot& shot)
		{
			Vector2f direction = Vector2f(std::cos(angle), std::sin(angle));
			float length = std::abs(options.Speed) * options.Lifetime;
			Rectf target = range.GetTarget();
			target = Rectf(target.X - 4.0f, target.Y - 4.0f, target.W + 8.0f, target.H + 8.0f);
			bool hit = false, stopped = false;
			Vector2f end = range.Muzzle;
			for (float t = 0.0f; t <= length; t += 2.0f) {
				Vector2f p = range.Muzzle + direction * t;
				if (!options.PassesWalls && IsOutsideRoom(p)) {
					stopped = true;
					break;
				}
				end = p;
				hit = (hit || IsInside(target, p));
			}
			shot.Points.push_back(range.Muzzle);
			shot.Points.push_back(end);
			shot.Angles.push_back(angle);
			shot.Angles.push_back(angle);
			shot.Frames = std::max<std::int32_t>(std::int32_t(options.Lifetime), 1);
			if (hit && (options.Strength > 0 || options.FreezeDuration > 0.0f)) {
				shot.TargetHits = 1;
				shot.End = SimulatedShotEnd::Target;
			} else {
				shot.End = (stopped ? SimulatedShotEnd::Wall : SimulatedShotEnd::Lifetime);
			}
		}

		void SimulateShot(const WeaponShotOptions& options, float angle, const FiringRange& range, SimulatedShot& shot)
		{
			if (options.Look == ShotLook::Beam) {
				SimulateBeam(options, angle, range, shot);
				return;
			}

			const Rectf target = range.GetTarget();
			const Vector2f targetCenter = Vector2f(target.X + target.W * 0.5f, target.Y + target.H * 0.5f);
			Vector2f pos = range.Muzzle;
			Vector2f speed = Vector2f(std::cos(angle) * options.Speed, std::sin(angle) * options.Speed);
			std::int32_t pierceLeft = options.Pierce;
			std::int32_t bouncesLeft = options.Bounces;
			bool wasInTarget = false;
			bool resting = false;
			std::int32_t frames = std::min<std::int32_t>(std::int32_t(options.Lifetime), FiringRange::MaxFrames);

			auto finish = [&shot](SimulatedShotEnd end) {
				shot.End = end;
				shot.Frames = std::int32_t(shot.Points.size());
			};

			shot.Points.push_back(pos);
			shot.Angles.push_back(angle);
			for (std::int32_t frame = 0; frame < frames; frame++) {
				if (options.Trigger == ShotTrigger::Proximity && (targetCenter - pos).Length() <= options.TriggerRadius) {
					finish(SimulatedShotEnd::Triggered);
					return;
				}

				if (options.HomingTurnRate > 0.0f && (targetCenter - pos).Length() <= options.HomingRange && !resting) {
					float length = std::max(speed.Length(), options.Speed);
					float current = std::atan2(speed.Y, speed.X);
					float wanted = std::atan2(targetCenter.Y - pos.Y, targetCenter.X - pos.X);
					float delta = wanted - current;
					while (delta > Pi) {
						delta -= Pi * 2.0f;
					}
					while (delta < -Pi) {
						delta += Pi * 2.0f;
					}
					current += std::clamp(delta, -options.HomingTurnRate, options.HomingTurnRate);
					speed = Vector2f(std::cos(current) * length, std::sin(current) * length);
				}

				if (!resting) {
					speed.X = std::clamp(speed.X, -16.0f, 16.0f);
					speed.Y = std::clamp(speed.Y + options.Gravity, -16.0f, 16.0f);
				}
				Vector2f next = pos + speed;

				if (!options.PassesWalls && !resting) {
					bool hitX = (next.X >= FiringRange::WallX || next.X <= LeftWallX);
					bool hitY = (next.Y >= FiringRange::FloorY || next.Y <= FiringRange::CeilingY);
					if (hitX || hitY) {
						if (bouncesLeft > 0) {
							if (hitX) {
								speed.X = -speed.X * options.Elasticity;
							}
							if (hitY) {
								speed.Y = -speed.Y * options.Elasticity;
							}
							bouncesLeft--;
							next = pos + speed;
						} else if (options.Trigger == ShotTrigger::Contact) {
							shot.Points.push_back(ClampToRoom(next));
							shot.Angles.push_back(shot.Angles.back());
							finish(SimulatedShotEnd::Wall);
							return;
						} else {
							// A mine stays where it fell
							resting = true;
							speed = Vector2f::Zero;
							next = ClampToRoom(next);
						}
					}
				} else if (next.X > FiringRange::WallX + 40.0f || next.X < LeftWallX - 40.0f || next.Y > FiringRange::FloorY + 40.0f ||
					next.Y < FiringRange::CeilingY - 40.0f) {
					finish(SimulatedShotEnd::Lifetime);
					return;
				}

				bool inTarget = IsInside(target, next);
				if (inTarget && !wasInTarget) {
					if (options.Trigger != ShotTrigger::Contact) {
						shot.Points.push_back(next);
						shot.Angles.push_back(shot.Angles.back());
						finish(SimulatedShotEnd::Triggered);
						return;
					}
					if (options.Strength > 0 || options.FreezeDuration > 0.0f) {
						shot.TargetHits++;
						if (pierceLeft <= 0) {
							shot.Points.push_back(next);
							shot.Angles.push_back(shot.Angles.back());
							finish(SimulatedShotEnd::Target);
							return;
						}
						pierceLeft--;
					}
				}
				wasInTarget = inTarget;

				pos = next;
				shot.Points.push_back(pos);
				shot.Angles.push_back(resting ? shot.Angles.back() : std::atan2(speed.Y, speed.X));
			}
			finish(options.Trigger == ShotTrigger::Timer ? SimulatedShotEnd::Triggered : SimulatedShotEnd::Lifetime);
		}

		Vector4f GetTexCoords(const GenericGraphicResource& base, std::int32_t frame, bool flippedX, bool flippedY = false)
		{
			Recti frameRect = base.GetFrameRect(frame);
			Vector2i texSize = base.TextureDiffuse->GetSize();
			float w = float(frameRect.W) / float(texSize.X);
			float x = float(frameRect.X) / float(texSize.X);
			float h = float(frameRect.H) / float(texSize.Y);
			float y = float(frameRect.Y) / float(texSize.Y);
			return Vector4f(flippedX ? -w : w, flippedX ? x + w : x, flippedY ? -h : h, flippedY ? y + h : y);
		}

		std::int32_t GetPaletteOffset(const GraphicResource& resource)
		{
			return ((resource.Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? resource.PaletteOffset : -1);
		}

		// An animation played once stays at its last frame, as on an actor
		std::int32_t GetFrameSinceStart(const GraphicResource& resource, float time)
		{
			if (resource.LoopMode == AnimationLoopMode::Once && resource.FrameCount > 1 && resource.AnimDuration > 0.0f) {
				std::int32_t frame = std::int32_t(time * float(resource.FrameCount) / resource.AnimDuration);
				return resource.FrameOffset + std::min(frame, resource.FrameCount - 1);
			}
			return resource.GetFrameForTime(time);
		}

		bool IsDrawable(const GraphicResource* resource)
		{
			return (resource != nullptr && resource->Base != nullptr && resource->Base->TextureDiffuse != nullptr);
		}

		// A frame with its hotspot at a point of the screen, turned around the hotspot
		void DrawAnchored(UiPainter& painter, const GraphicResource& resource, std::int32_t frame, Vector2f at, float scale, bool flippedX,
			float angle, const Colorf& color, UI::BlendMode blend, UiLayer layer, bool flippedY = false)
		{
			const GenericGraphicResource& base = *resource.Base;
			Recti frameRect = base.GetFrameRect(frame);
			Vector2i anchor = base.GetFrameAnchor(frame, flippedX, flippedY);
			Vector2f size = Vector2f(frameRect.W * scale, frameRect.H * scale);
			Vector2f center = Vector2f(size.X * 0.5f - anchor.X * scale, size.Y * 0.5f - anchor.Y * scale);
			float c = std::cos(angle), s = std::sin(angle);
			Vector2f rotated = at + Vector2f(center.X * c - center.Y * s, center.X * s + center.Y * c);
			Rectf rect(rotated.X - size.X * 0.5f, rotated.Y - size.Y * 0.5f, size.X, size.Y);
			painter.Image(*base.TextureDiffuse, rect, GetTexCoords(base, frame, flippedX, flippedY), color, GetPaletteOffset(resource), layer, angle, blend);
		}
	}

	const GraphicResource* FindWeaponAnimation(StringView metadataPath, std::uint32_t animState)
	{
		Metadata* metadata = (metadataPath.empty() ? nullptr : ContentResolver::Get().RequestMetadata(metadataPath));
		if (metadata == nullptr) {
			return nullptr;
		}
		const GraphicResource* resource = metadata->FindAnimation(AnimState(animState));
		return (resource != nullptr ? resource : metadata->FindAnimation(AnimState::Default));
	}

	void SimulateWeapon(const WeaponDefinition& weapon, bool powered, SmallVectorImpl<SimulatedShot>& shots, const FiringRange& range, std::int32_t volley)
	{
		shots.clear();
		const WeaponShotOptions& options = (powered ? weapon.ShotPowered : weapon.Shot);
		std::int32_t count = std::max<std::int32_t>(weapon.Fire.ShotsPerFire, 1);
		for (std::int32_t i = 0; i < count; i++) {
			std::int32_t sample = volley * count + i;
			float offset = (count > 1 ? weapon.Fire.SpreadAngle * (float(i) / float(count - 1) - 0.5f) : 0.0f);
			offset += weapon.Fire.RandomSpread * SpreadSample(sample);
			SimulatedShot& shot = shots.emplace_back();
			SimulateShot(options, range.FacingLeft ? Pi + offset : offset, range, shot);
			// What's random is evenly spread too
			shot.AltSprite = (!options.AltSprite.IsEmpty() && SpreadSample(sample * 3 + 1) > 0.0f);
			shot.FlippedX = (options.FlipX == ShotFlip::Flipped || (options.FlipX == ShotFlip::Random && SpreadSample(sample * 3 + 2) > 0.0f));
			shot.FlippedY = (options.FlipY == ShotFlip::Flipped || (options.FlipY == ShotFlip::Random && SpreadSample(sample * 3 + 3) > 0.0f));
		}
	}

	void SimulateHeldWeapon(const WeaponDefinition& weapon, bool powered, SmallVectorImpl<SimulatedShot>& shots, const FiringRange& range)
	{
		// Fire held for most of a second, at most a dozen presses
		constexpr float HeldFrames = 45.0f;
		constexpr std::int32_t MaxVolleys = 12;

		shots.clear();
		float cooldown = std::max(weapon.Fire.Cooldown, 1.0f);
		std::int32_t volleys = (weapon.Options.Style == WeaponStyle::Missile ? 1 : std::clamp<std::int32_t>(std::int32_t(std::ceil(HeldFrames / cooldown)), 1, MaxVolleys));
		SmallVector<SimulatedShot, 4> volleyShots;
		for (std::int32_t v = 0; v < volleys; v++) {
			SimulateWeapon(weapon, powered, volleyShots, range, v);
			for (SimulatedShot& shot : volleyShots) {
				shot.StartFrame = std::int32_t(std::round(float(v) * cooldown));
				shots.push_back(Death::move(shot));
			}
		}
	}

	void DrawFittedFrame(UiPainter& painter, const GraphicResource& resource, std::int32_t frame, const Rectf& rect, bool additive)
	{
		if (!IsDrawable(&resource)) {
			return;
		}
		const GenericGraphicResource& base = *resource.Base;
		Recti frameRect = base.GetFrameRect(frame);
		Vector2i cell = base.FrameDimensions;
		if (frameRect.W <= 0 || frameRect.H <= 0 || cell.X <= 0 || cell.Y <= 0) {
			return;
		}
		// The whole cell around the hotspot fits, so all frames are at the same scale, a hotspot outside of it is ignored
		Vector2i hotspot = base.Hotspot;
		if (hotspot.X < 0 || hotspot.Y < 0 || hotspot.X > cell.X || hotspot.Y > cell.Y) {
			hotspot = Vector2i(cell.X / 2, cell.Y / 2);
		}
		float extentX = 2.0f * float(std::max(hotspot.X, cell.X - hotspot.X));
		float extentY = 2.0f * float(std::max(hotspot.Y, cell.Y - hotspot.Y));
		float scale = std::min({ rect.W / extentX, rect.H / extentY, std::max(std::floor(painter.GetScale()), 1.0f) });
		scale = (scale >= 1.0f ? std::floor(scale) : scale);
		Vector2i offset = base.GetFrameOffset(frame);
		Vector2f center = Vector2f(std::round(rect.X + rect.W * 0.5f), std::round(rect.Y + rect.H * 0.5f));
		Rectf target(std::round(center.X - float(hotspot.X - offset.X) * scale), std::round(center.Y - float(hotspot.Y - offset.Y) * scale),
			frameRect.W * scale, frameRect.H * scale);
		painter.Image(*base.TextureDiffuse, target, GetTexCoords(base, frame, false), Colorf::White, GetPaletteOffset(resource), UiLayer::Content, 0.0f,
			additive ? UI::BlendMode::Additive : UI::BlendMode::Normal);
	}

	WeaponPreview::WeaponPreview(IEditorContext& session, Function<const WeaponsDescription*()>&& getWeapons, Function<WeaponType()>&& getSlot,
		Function<bool()>&& isPowered)
		: _session(session), _getWeapons(Death::move(getWeapons)), _getSlot(Death::move(getSlot)), _isPowered(Death::move(isPowered))
	{
		Tooltip = _x("editor", "Drag the player or the target to move them");
	}

	const WeaponDefinition* WeaponPreview::GetWeapon()
	{
		const WeaponsDescription* weapons = _getWeapons();
		WeaponType slot = _getSlot();
		if (weapons == nullptr || slot >= WeaponType::Count) {
			return nullptr;
		}
		const WeaponDefinition& weapon = weapons->Weapons[std::int32_t(slot)];
		return (weapon.Kind == WeaponKind::Custom ? &weapon : nullptr);
	}

	void WeaponPreview::UpdateRange()
	{
		_range.FacingLeft = (_range.Target.X < _playerPos.X);
		_range.Muzzle = Vector2f(_playerPos.X + (_range.FacingLeft ? -_gunOffset.X : _gunOffset.X), _playerPos.Y + _gunOffset.Y);
	}

	void WeaponPreview::Simulate(const WeaponDefinition& weapon, bool powered)
	{
		UpdateRange();
		if (!_hasSimulation || powered != _simulatedPowered || !(_range == _simulatedRange) || !(weapon == _simulated)) {
			_simulated = weapon;
			_simulatedPowered = powered;
			_simulatedRange = _range;
			_hasSimulation = true;
			SimulateHeldWeapon(weapon, powered, _shots, _range);
		}
	}

	String WeaponPreview::GetSummary()
	{
		const WeaponDefinition* weapon = GetWeapon();
		if (weapon == nullptr) {
			return {};
		}
		Simulate(*weapon, _isPowered());

		std::int32_t hits = 0, longest = 0;
		float reach = 0.0f;
		for (const SimulatedShot& shot : _shots) {
			hits += shot.TargetHits;
			longest = std::max(longest, shot.StartFrame + shot.Frames);
			if (!shot.Points.empty()) {
				reach = std::max(reach, (shot.Points.back() - _range.Muzzle).Length());
			}
		}
		// TRANSLATORS: Under the preview of a custom weapon in the Weapons dialog: shots fired while fire is held, hits on the target, how far they reach and for how long
		return _fx("editor", "Shots {}, hits {}, reach {} px, time {:.2} s", std::int32_t(_shots.size()), hits, std::int32_t(reach), float(longest) / 60.0f);
	}

	Vector2f WeaponPreview::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f size = GetFixedSize(context);
		return Vector2f(size.X >= 0.0f ? size.X : context.Px(ViewWidth), size.Y >= 0.0f ? size.Y : context.Px(ViewHeight));
	}

	float WeaponPreview::GetScale() const
	{
		return std::min(Bounds.W / ViewWidth, Bounds.H / ViewHeight);
	}

	Vector2f WeaponPreview::GetOrigin() const
	{
		float scale = GetScale();
		return Vector2f(Bounds.X + (Bounds.W - ViewWidth * scale) * 0.5f - ViewLeft * scale,
			Bounds.Y + (Bounds.H - ViewHeight * scale) * 0.5f - ViewTop * scale);
	}

	Vector2f WeaponPreview::ToRange(Vector2f screen) const
	{
		float scale = std::max(GetScale(), 0.01f);
		Vector2f origin = GetOrigin();
		return Vector2f((screen.X - origin.X) / scale, (screen.Y - origin.Y) / scale);
	}

	Rectf WeaponPreview::GetPlayerRect() const
	{
		// The hitbox of the player in the game around its hotspot, see Player::OnUpdateHitbox()
		return Rectf(_playerPos.X - 11.0f, _playerPos.Y - 10.0f, 22.0f, 30.0f);
	}

	WeaponPreview::DragTarget WeaponPreview::HitTestDraggable(Vector2f screen) const
	{
		Vector2f p = ToRange(screen);
		if (IsInside(_range.GetTarget(), p)) {
			return DragTarget::Target;
		}
		if (IsInside(GetPlayerRect(), p)) {
			return DragTarget::Player;
		}
		return DragTarget::None;
	}

	bool WeaponPreview::AcceptsPointer() const
	{
		return true;
	}

	bool WeaponPreview::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		_dragging = HitTestDraggable(event.Pos);
		if (_dragging == DragTarget::None) {
			return false;
		}
		_dragOffset = ToRange(event.Pos) - (_dragging == DragTarget::Player ? _playerPos : _range.Target);
		return true;
	}

	void WeaponPreview::OnPointerMove(const UiPointerEvent& event)
	{
		Vector2f p = ToRange(event.Pos) - _dragOffset;
		p = Vector2f(std::round(p.X), std::round(p.Y));
		if (_dragging == DragTarget::Player) {
			// Its hitbox stays in the room, the player stands on the floor at the lowest
			_playerPos = Vector2f(std::clamp(p.X, LeftWallX + 11.0f, FiringRange::WallX - 11.0f),
				std::clamp(p.Y, FiringRange::CeilingY + 10.0f, FiringRange::FloorY - 20.0f));
		} else if (_dragging == DragTarget::Target) {
			_range.Target = Vector2f(std::clamp(p.X, LeftWallX + 12.0f, FiringRange::WallX - 12.0f),
				std::clamp(p.Y, FiringRange::CeilingY + 32.0f, FiringRange::FloorY));
		}
	}

	void WeaponPreview::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		_dragging = DragTarget::None;
	}

	void WeaponPreview::OnPointerCancel()
	{
		_dragging = DragTarget::None;
	}

	StringView WeaponPreview::GetTooltip(Vector2f pos) const
	{
		return (_dragging == DragTarget::None && HitTestDraggable(pos) != DragTarget::None ? StringView(Tooltip) : StringView());
	}

	void WeaponPreview::Update(float timeMult)
	{
		UiElement::Update(timeMult);
		UiSurface* surface = GetSurface();
		_pointedAt = (surface != nullptr && IsHovered() && GetWeapon() != nullptr ? HitTestDraggable(surface->GetPointerPos()) : DragTarget::None);
		if (surface != nullptr && (_dragging != DragTarget::None || _pointedAt != DragTarget::None)) {
			surface->SetCursorOverlay(UiCursorOverlay::Move);
		}
	}

	void WeaponPreview::Draw(UiPainter& painter)
	{
		painter.RoundRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
		painter.RoundRectBorder(Bounds, UiTheme::PanelBorderDim, UiLayer::Fill);

		const WeaponsDescription* weapons = _getWeapons();
		WeaponType slot = _getSlot();
		const WeaponDefinition* current = GetWeapon();
		if (current == nullptr) {
			painter.TextInRect(_x("editor", "Only a custom weapon is shown here"), Bounds, UiTheme::TextDim, Alignment::Center);
			return;
		}
		const WeaponDefinition& weapon = *current;
		bool powered = _isPowered();

		// The character the level is played with, the shots start at the gun of its sprite
		static constexpr StringView PlayerPaths[] = { "Interactive/PlayerJazz"_s, "Interactive/PlayerSpaz"_s, "Interactive/PlayerLori"_s };
		std::int32_t character = std::clamp<std::int32_t>(_session.GetSettings().PlayCharacter - std::int32_t(PlayerType::Jazz), 0, 2);
		const GraphicResource* player = FindWeaponAnimation(PlayerPaths[character], std::uint32_t(AnimState::Shoot));
		bool hasGun = (IsDrawable(player) && player->Base->Gunspot != Vector2i(ContentResolver::InvalidValue, ContentResolver::InvalidValue));
		if (hasGun) {
			_gunOffset = Vector2f(float(player->Base->Gunspot.X - player->Base->Hotspot.X), float(player->Base->Gunspot.Y - player->Base->Hotspot.Y));
		}
		Simulate(weapon, powered);

		float scale = GetScale();
		Vector2f origin = GetOrigin();
		auto toScreen = [origin, scale](Vector2f p) {
			return Vector2f(origin.X + p.X * scale, origin.Y + p.Y * scale);
		};
		auto toScreenRect = [&toScreen, scale](const Rectf& rect) {
			Vector2f topLeft = toScreen(Vector2f(rect.X, rect.Y));
			return Rectf(topLeft.X, topLeft.Y, rect.W * scale, rect.H * scale);
		};
		float animTime = _session.GetAnimTime();
		bool facingLeft = _range.FacingLeft;

		float inset = painter.Px(2.0f);
		painter.PushClip(Rectf(Bounds.X + inset, Bounds.Y + inset, Bounds.W - inset * 2.0f, Bounds.H - inset * 2.0f));
		const WeaponShotOptions& options = (powered ? weapon.ShotPowered : weapon.Shot);
		Colorf wallColor = Colorf(1.0f, 1.0f, 1.0f, options.PassesWalls ? 0.15f : 0.4f);
		painter.Line(toScreen(Vector2f(LeftWallX, FiringRange::FloorY)), toScreen(Vector2f(FiringRange::WallX, FiringRange::FloorY)), 1.0f, wallColor);
		painter.Line(toScreen(Vector2f(LeftWallX, FiringRange::CeilingY)), toScreen(Vector2f(FiringRange::WallX, FiringRange::CeilingY)), 1.0f, wallColor);
		painter.Line(toScreen(Vector2f(LeftWallX, FiringRange::CeilingY)), toScreen(Vector2f(LeftWallX, FiringRange::FloorY)), 1.0f, wallColor);
		painter.Line(toScreen(Vector2f(FiringRange::WallX, FiringRange::CeilingY)), toScreen(Vector2f(FiringRange::WallX, FiringRange::FloorY)), 1.0f, wallColor);

		if (hasGun) {
			DrawAnchored(painter, *player, player->GetFrameForTime(animTime), toScreen(_playerPos), scale, facingLeft, 0.0f,
				Colorf::White, UI::BlendMode::Normal, UiLayer::Content);
		} else {
			painter.FillRect(toScreenRect(GetPlayerRect()), Colorf(0.4f, 0.7f, 1.0f, 0.35f), UiLayer::Content);
		}

		// The target, turned to the player
		Rectf target = _range.GetTarget();
		const GraphicResource* enemy = FindWeaponAnimation("Enemy/Turtle"_s, std::uint32_t(AnimState::Idle));
		if (IsDrawable(enemy)) {
			std::int32_t frame = enemy->GetFrameForTime(animTime);
			Recti frameRect = enemy->Base->GetFrameRect(frame);
			Vector2f bottom = toScreen(_range.Target);
			Rectf rect(std::round(bottom.X - frameRect.W * scale * 0.5f), std::round(bottom.Y - frameRect.H * scale), frameRect.W * scale, frameRect.H * scale);
			painter.Image(*enemy->Base->TextureDiffuse, rect, GetTexCoords(*enemy->Base, frame, !facingLeft), Colorf::White, GetPaletteOffset(*enemy), UiLayer::Content);
		} else {
			painter.FillRect(toScreenRect(target), Colorf(1.0f, 0.35f, 0.3f, 0.35f), UiLayer::Content);
		}
		// The hitbox that's dragged, dimmed while it's only pointed at
		DragTarget shown = (_dragging != DragTarget::None ? _dragging : _pointedAt);
		if (shown != DragTarget::None) {
			Rectf hitbox = toScreenRect(shown == DragTarget::Target ? target : GetPlayerRect());
			painter.FillRect(hitbox, Colorf(1.0f, 1.0f, 1.0f, _dragging != DragTarget::None ? 0.12f : 0.06f), UiLayer::Overlay);
			painter.Frame(hitbox, Colorf(1.0f, 1.0f, 1.0f, _dragging != DragTarget::None ? 0.45f : 0.22f), 1.0f);
		}

		for (const SimulatedShot& shot : _shots) {
			SmallVector<Vector2f, 0> points;
			points.reserve(shot.Points.size());
			for (Vector2f p : shot.Points) {
				points.push_back(toScreen(p));
			}
			if (points.size() >= 2) {
				painter.Polyline(points, 1.0f, Colorf(1.0f, 0.85f, 0.4f, 0.35f));
			}
			if (!shot.Points.empty() && (shot.End != SimulatedShotEnd::Lifetime || options.Trigger == ShotTrigger::Timer) && options.AreaRadius > 0.0f) {
				painter.Circle(toScreen(shot.Points.back()), options.AreaRadius * scale, Colorf(1.0f, 0.5f, 0.2f, 0.18f), UiLayer::Overlay);
			}
		}

		// The sprite of the shot, or of the weapon it was made from
		String path = weapons->GetSpriteMetadataPath(options.Sprite);
		std::uint32_t state = options.Sprite.AnimState;
		if (path.empty()) {
			WeaponDefinition stock = WeaponDefinition::MakeCustom(slot, weapon.BasedOn != WeaponType::Unknown ? weapon.BasedOn : slot);
			const WeaponSprite& stockSprite = (powered ? stock.ShotPowered.Sprite : stock.Shot.Sprite);
			path = stockSprite.Metadata;
			state = stockSprite.AnimState;
		}
		const GraphicResource* mainResource = FindWeaponAnimation(path, state);
		const GraphicResource* altResource = (options.AltSprite.IsEmpty() ? nullptr
			: FindWeaponAnimation(weapons->GetSpriteMetadataPath(options.AltSprite), options.AltSprite.AnimState));

		// The fire is held again and again, with a short pause between, each shot is animated from when it's fired
		std::int32_t longest = 1;
		for (const SimulatedShot& shot : _shots) {
			longest = std::max(longest, shot.StartFrame + shot.Frames);
		}
		std::int32_t loopFrame = std::int32_t(animTime * 60.0f) % (longest + 20);
		UI::BlendMode blend = (options.Look == ShotLook::Glow ? UI::BlendMode::Additive : UI::BlendMode::Normal);
		for (const SimulatedShot& shot : _shots) {
			std::int32_t frame = loopFrame - shot.StartFrame;
			if (frame < 0 || frame >= shot.Frames || shot.Points.empty()) {
				continue;
			}
			const GraphicResource* resource = (shot.AltSprite && IsDrawable(altResource) ? altResource : mainResource);
			std::int32_t spriteFrame = (resource != nullptr ? GetFrameSinceStart(*resource, float(frame) / 60.0f) : 0);
			if (!IsDrawable(resource)) {
				painter.Circle(toScreen(shot.Points[std::min<std::int32_t>(frame, std::int32_t(shot.Points.size()) - 1)]),
					std::max(2.0f, 3.0f * scale), Colorf(1.0f, 0.9f, 0.5f, 0.9f), UiLayer::Overlay);
				continue;
			}

			if (options.Look == ShotLook::Beam) {
				// From the muzzle, the sprite of the bolt is drawn as it is and turned as aimed
				float angle = shot.Angles[0];
				bool flipped = (std::cos(angle) < 0.0f);
				float turned = (flipped ? std::atan2(-std::sin(angle), -std::cos(angle)) : angle);
				DrawAnchored(painter, *resource, spriteFrame, toScreen(shot.Points[0]), scale, flipped != shot.FlippedX, turned, Colorf::White, blend,
					UiLayer::Overlay, shot.FlippedY);
				continue;
			}

			if (options.Look == ShotLook::Electro) {
				// Rings of sparks left behind, each smaller and fainter as it gets older, see ElectroShot::CreateParticles()
				constexpr std::int32_t Lifetime = 10;
				std::int32_t frameCount = std::max<std::int32_t>(resource->FrameCount, 1);
				for (std::int32_t age = Lifetime - 1; age >= 0; age--) {
					std::int32_t step = frame - age;
					if (step < 2) {
						continue;
					}
					Vector2f at = shot.Points[std::min<std::int32_t>(step, std::int32_t(shot.Points.size()) - 1)];
					float fade = 1.0f - float(age) / float(Lifetime);
					float size = (8.0f + float(step - 2) * 0.2f) * fade;
					float dist = 2.0f + float(step - 2) * 0.01f;
					for (std::int32_t i = 0; i < 6; i++) {
						float angle = float(step - 2) * 0.3f + float(i) * 0.6f;
						if (std::cos(shot.Angles[0]) < 0.0f) {
							angle = -angle;
						}
						Vector2f p = toScreen(at + Vector2f(std::cos(angle) * dist, std::sin(angle) * dist));
						std::int32_t particle = resource->FrameOffset + ((powered ? 2 : 0) + ((step + i) & 1)) % frameCount;
						Rectf rect(p.X - size * scale * 0.5f, p.Y - size * scale * 0.5f, size * scale, size * scale);
						painter.Image(*resource->Base->TextureDiffuse, rect, GetTexCoords(*resource->Base, particle, false), Colorf(1.0f, 1.0f, 1.0f, fade),
							GetPaletteOffset(*resource), UiLayer::Overlay, angle);
					}
				}
				continue;
			}

			float angle = shot.Angles[std::min<std::int32_t>(frame, std::int32_t(shot.Angles.size()) - 1)];
			bool flipped = (std::cos(angle) < 0.0f);
			float turned = (!options.RotatesWithSpeed ? 0.0f : (flipped ? std::atan2(-std::sin(angle), -std::cos(angle)) : angle));
			DrawAnchored(painter, *resource, spriteFrame, toScreen(shot.Points[std::min<std::int32_t>(frame, std::int32_t(shot.Points.size()) - 1)]),
				scale, flipped != shot.FlippedX, turned, Colorf::White, blend, UiLayer::Overlay, shot.FlippedY);
		}
		painter.PopClip();
	}

	WeaponSpriteView::WeaponSpriteView(IEditorContext& session, Function<const GraphicResource*(bool&)>&& getResource)
		: _session(session), _getResource(Death::move(getResource))
	{
	}

	Vector2f WeaponSpriteView::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f size = GetFixedSize(context);
		return Vector2f(size.X >= 0.0f ? size.X : context.Px(36.0f), size.Y >= 0.0f ? size.Y : context.Px(UiTheme::ButtonHeight + 6.0f));
	}

	void WeaponSpriteView::Draw(UiPainter& painter)
	{
		painter.RoundRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
		bool additive = false;
		const GraphicResource* resource = _getResource(additive);
		if (!IsDrawable(resource)) {
			return;
		}
		float inset = painter.Px(2.0f);
		Rectf inner(Bounds.X + inset, Bounds.Y + inset, Bounds.W - inset * 2.0f, Bounds.H - inset * 2.0f);
		painter.PushClip(inner);
		DrawFittedFrame(painter, *resource, resource->GetFrameForTime(_session.GetAnimTime()), inner, additive);
		painter.PopClip();
	}
}

#endif
