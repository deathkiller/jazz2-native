#include "MeasureTool.h"

#if defined(WITH_EDITOR)

#include "../Input/ActionIds.h"
#include "../../EventType.h"
#include "../../PreferencesCache.h"
#include "../../Tiles/TileCollisionParams.h"
#include "../../Tiles/TileMap.h"
#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#if !defined(N_)
#	define N_(text) text
#endif

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// The original game, per tick at 70 Hz, see Actors::Player::LegacyJumpSpeed and the others
		constexpr float JumpSpeed = 10.0f;
		constexpr float SpeedJumpScale = 0.25f;
		constexpr float RiseGravity = 0.375f;
		constexpr float FallGravity = 0.125f;
		constexpr float FallSpeedCap = 12.0f;
		constexpr float DoubleJumpSpeed = 8.0f;
		// The movement applied in a tick is held to this, the speed above it goes on only as momentum, see
		// Actors::Player::LegacyAppliedSpeedCap; a dash reports twice the speed it's seen moving at
		constexpr float AppliedSpeedCap = 8.0f;
		// The air acceleration a double jump builds the speed along back up with, it starts from nothing
		constexpr float WalkAccel = 12000.0f / 65536.0f;
		constexpr float CopterDescentSpeed = 129.0f / 128.0f;
		// A horizontal spring keeps its speed for a few ticks, then the run it starts for a while
		constexpr float SpringHoldTicks = 4.0f;
		constexpr float SpringCarrySpeed = 16.0f;
		constexpr float SpringRunTicks = 80.0f * 70.0f / 60.0f;
		constexpr float WalkSpeed = 4.0f;
		// The Reforged jump, per frame at 60 FPS: a launch speed, an internal force that rises and fades with a third of the
		// gravity a frame, see Actors::Player::BeginStandardJump(), ActorBase::TryStandardMovement() and LevelHandler::GetGravity()
		constexpr float ReforgedGravity = 0.3f;
		constexpr float ReforgedJumpSpeed = 3.6f;
		constexpr float ReforgedJumpForce = 1.02f;
		constexpr float ReforgedDoubleJumpSpeed = 0.6f;
		constexpr float ReforgedDoubleJumpForce = 1.15f;
		constexpr float ReforgedCopterDescentSpeed = 1.5f;
		// Faster than walking, the launch gets a part of the speed above it
		constexpr float ReforgedWalkSpeed = 4.0f;
		constexpr float ReforgedSpeedJumpScale = 0.3f;
		constexpr float ReforgedVerticalSpeedLimit = 16.0f;
		// Springs of Reforged, see Actors::Environment::Spring and Actors::Player::OnHitSpring()
		constexpr float ReforgedHorizontalSpring = 9.5f;
		constexpr float ReforgedSpringFriction = 1.5f;
		constexpr float ReforgedSpringRunFrames = 100.0f;
		// The hitbox of the player above its feet, see Actors::Player::OnUpdateHitbox()
		constexpr float HitboxHalfWidth = 10.0f;
		constexpr float HitboxHeight = 28.0f;
		constexpr float ReforgedHitboxHalfWidth = 11.0f;
		constexpr float ReforgedHitboxHeight = 30.0f;
		// How far below its start a jump is followed
		constexpr float MaxDrop = 12.0f * 32.0f;
		constexpr std::int32_t MaxTicks = 400;

		// Walking, dashing, the double jump of Spaz and the copter of Jazz, see ToolSettings::MeasureArcs
		constexpr MeasureTool::JumpArc Arcs[] = {
			{ 4.0f, false, false },
			{ 16.0f, false, false },
			{ 4.0f, true, false },
			{ 4.0f, false, false, true },
		};
		// See Actors::Player::MaxRunningSpeed and MaxDashingSpeed
		constexpr MeasureTool::JumpArc ReforgedArcs[] = {
			{ 4.0f, false, true },
			{ 9.0f, false, true },
			{ 4.0f, true, true },
			{ 4.0f, false, true, true },
		};
		constexpr std::uint8_t ArcBits[] = { ToolSettings::ArcWalk, ToolSettings::ArcDash, ToolSettings::ArcDoubleJump, ToolSettings::ArcCopter };
		constexpr std::int32_t ArcCount = std::int32_t(sizeof(ArcBits) / sizeof(ArcBits[0]));

		const MeasureTool::JumpArc* GetArcs()
		{
			return (PreferencesCache::EnableReforgedGameplay ? ReforgedArcs : Arcs);
		}

		// Moves the feet of the player, stopped by solid tiles as the player is, the collisions start once the player is
		// out of anything solid it may start in
		struct Mover
		{
			Tiles::TileMap* TileMap;
			bool Reforged;
			bool Active;
			bool HitWall = false;
			bool HitCeiling = false;
			bool Landed = false;

			bool IsFree(Vector2f feet, bool downwards) const
			{
				float halfWidth = (Reforged ? ReforgedHitboxHalfWidth : HitboxHalfWidth);
				float height = (Reforged ? ReforgedHitboxHeight : HitboxHeight);
				AABBf aabb(feet.X - halfWidth, feet.Y - height, feet.X + halfWidth, feet.Y - 0.5f);
				Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, downwards };
				return TileMap->IsTileEmpty(aabb, params);
			}

			void Move(Vector2f& pos, float dx, float dy)
			{
				HitWall = HitCeiling = Landed = false;
				Vector2f next(pos.X + dx, pos.Y + dy);
				if (TileMap == nullptr) {
					pos = next;
					return;
				}
				if (!Active) {
					pos = next;
					Active = IsFree(pos, false);
					return;
				}
				if (IsFree(next, dy >= 0.0f)) {
					pos = next;
					return;
				}
				// Along each axis alone, as the player slides along a wall or a ceiling
				if (dx != 0.0f) {
					if (IsFree(Vector2f(pos.X + dx, pos.Y), false)) {
						pos.X += dx;
					} else {
						HitWall = true;
					}
				}
				if (dy != 0.0f) {
					if (IsFree(Vector2f(pos.X, pos.Y + dy), dy >= 0.0f)) {
						pos.Y += dy;
					} else if (dy > 0.0f) {
						Landed = true;
					} else {
						HitCeiling = true;
					}
				}
			}
		};

		void GetLegacyJumpPath(const MeasureTool::JumpArc& arc, Vector2f start, float direction, Tiles::TileMap* tileMap, SmallVectorImpl<Vector2f>& path)
		{
			Mover mover{ tileMap, false, false };
			float speedX = arc.SpeedX;
			float speedY;
			float springHold = 0.0f, springRun = 0.0f;
			bool springCarry = false;
			if (arc.Spring > 0.0f) {
				// A spring sets the speed outright, the same figure on either axis
				if (arc.SpringHorizontal) {
					speedX = arc.Spring;
					speedY = 0.0f;
					springHold = SpringHoldTicks;
					springRun = SpringRunTicks;
					springCarry = true;
				} else {
					speedX = 0.0f;
					speedY = -arc.Spring;
				}
			} else {
				// The launch is faster the faster the player runs, the original's -(10 + |speed| / 4)
				speedY = -(JumpSpeed + arc.SpeedX * SpeedJumpScale);
			}
			bool doubleJumped = false, rebuilding = false;
			Vector2f pos = start;
			path.push_back(pos);
			for (std::int32_t tick = 0; tick < MaxTicks; tick++) {
				float appliedX = (springCarry ? speedX : std::min(speedX, AppliedSpeedCap));
				float appliedY = std::max(speedY, -AppliedSpeedCap);
				mover.Move(pos, appliedX * direction, appliedY);
				path.push_back(pos);
				if (mover.Landed) {
					break;
				}
				if (mover.HitWall) {
					speedX = 0.0f;
					springCarry = false;
				}
				if (mover.HitCeiling) {
					speedY = 0.0f;
				}
				if (speedY < 0.0f) {
					speedY += RiseGravity;
				} else {
					if (arc.DoubleJump && !doubleJumped) {
						// Spaz jumps again from the top, the speed along is thrown away and built up again in the air
						speedY = -DoubleJumpSpeed;
						speedX = 0.0f;
						doubleJumped = true;
						rebuilding = true;
						continue;
					}
					speedY = std::min(speedY + FallGravity, arc.Copter ? CopterDescentSpeed : FallSpeedCap);
				}
				if (rebuilding) {
					speedX = std::min(speedX + WalkAccel, arc.SpeedX);
				}
				if (springHold > 0.0f) {
					springHold -= 1.0f;
					if (springHold <= 0.0f) {
						speedX = std::min(speedX, SpringCarrySpeed);
					}
				} else if (springRun > 0.0f) {
					springRun -= 1.0f;
					if (springRun <= 0.0f) {
						springCarry = false;
						speedX = std::min(speedX, WalkSpeed);
					}
				}
				if (pos.Y > start.Y + MaxDrop) {
					break;
				}
			}
		}

		void GetReforgedJumpPath(const MeasureTool::JumpArc& arc, Vector2f start, float direction, Tiles::TileMap* tileMap, SmallVectorImpl<Vector2f>& path)
		{
			Mover mover{ tileMap, true, false };
			float speedX = arc.SpeedX;
			float speedY, force, externalX = 0.0f;
			float springRun = 0.0f;
			if (arc.Spring > 0.0f) {
				if (arc.SpringHorizontal) {
					speedX = 1.0f + arc.Spring;
					externalX = arc.Spring * 0.6f;
					speedY = 0.0f;
					force = 0.0f;
					springRun = ReforgedSpringRunFrames;
				} else {
					speedX = 0.0f;
					speedY = -(4.0f + arc.Spring);
					force = -arc.Spring;
				}
			} else {
				speedY = -(ReforgedJumpSpeed + std::max(0.0f, (std::abs(speedX) - ReforgedWalkSpeed) * ReforgedSpeedJumpScale));
				force = -ReforgedJumpForce;
			}
			bool doubleJumped = false, copter = false;
			Vector2f pos = start;
			path.push_back(pos);
			for (std::int32_t tick = 0; tick < MaxTicks; tick++) {
				float moveY;
				if (copter) {
					// The copter holds the descent, gravity is off
					moveY = ReforgedCopterDescentSpeed;
				} else {
					// The force goes into the speed first, half of it and half of the gravity into the move (velocity Verlet)
					speedY = std::clamp(speedY + force, -ReforgedVerticalSpeedLimit, ReforgedVerticalSpeedLimit);
					moveY = speedY + 0.5f * force + 0.5f * ReforgedGravity;
				}
				mover.Move(pos, (speedX + externalX) * direction, moveY);
				path.push_back(pos);
				if (mover.Landed) {
					break;
				}
				if (mover.HitWall) {
					speedX = 0.0f;
					externalX = 0.0f;
				}
				if (mover.HitCeiling) {
					speedY = 0.0f;
					force = 0.0f;
				}
				if (!copter) {
					speedY += ReforgedGravity;
				}
				force = std::min(force + ReforgedGravity * 0.33f, 0.0f);
				externalX = std::max(externalX - ReforgedSpringFriction, 0.0f);
				if (springRun > 0.0f) {
					springRun -= 1.0f;
					if (springRun <= 0.0f) {
						speedX = std::min(speedX, ReforgedWalkSpeed);
					}
				}
				if (arc.DoubleJump && !doubleJumped && speedY >= 0.0f) {
					// Spaz jumps again at the top, most of the speed along is lost
					force = -ReforgedDoubleJumpForce;
					speedY = -(ReforgedDoubleJumpSpeed + std::max(0.0f, (std::abs(speedX) - ReforgedWalkSpeed) * ReforgedSpeedJumpScale));
					speedX = std::clamp(speedX * 0.4f, -1.0f, 1.0f);
					doubleJumped = true;
				}
				if (arc.Copter && !copter && speedY >= 0.0f) {
					copter = true;
				}
				if (pos.Y > start.Y + MaxDrop) {
					break;
				}
			}
		}

		// The strength of a spring by its color, red, green and blue, see Actors::Environment::Spring
		float GetSpringStrength(std::int32_t type, bool horizontal, bool reforged)
		{
			type = std::clamp<std::int32_t>(type, 0, 2);
			if (!reforged) {
				return 16.0f + 8.0f * float(type);
			}
			if (horizontal) {
				return ReforgedHorizontalSpring;
			}
			static const float Vertical[] = { 1.25f, 1.50f, 1.68f };
			return Vertical[type];
		}
	}

	MeasureTool::MeasureTool(IToolHost& host)
		: EditorTool(host), _hasMeasure(false), _dragging(false)
	{
	}

	EditorToolType MeasureTool::GetType() const
	{
		return EditorToolType::Measure;
	}

	Vector2f MeasureTool::GetPoint(const ToolPointer& pointer) const
	{
		Vector2f world = pointer.World;
		if ((pointer.Modifiers & KeyMod::Ctrl) != 0) {
			world = Vector2f(std::floor(world.X / 32.0f) * 32.0f + 16.0f, std::floor(world.Y / 32.0f) * 32.0f + 16.0f);
		}
		return Vector2f(std::round(world.X), std::round(world.Y));
	}

	void MeasureTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button == MouseButton::Right) {
			_hasMeasure = false;
			return;
		}
		if (pointer.Button != MouseButton::Left) {
			return;
		}
		_start = _end = GetPoint(pointer);
		_hasMeasure = true;
		_dragging = true;
	}

	void MeasureTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragging) {
			_end = GetPoint(pointer);
		}
	}

	void MeasureTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (_dragging) {
			_end = GetPoint(pointer);
			_dragging = false;
		}
	}

	void MeasureTool::Cancel()
	{
		_dragging = false;
		_hasMeasure = false;
	}

	bool MeasureTool::IsBusy() const
	{
		return _dragging;
	}

	void MeasureTool::GetJumpPath(const JumpArc& arc, Vector2f start, float direction, SmallVectorImpl<Vector2f>& path, Tiles::TileMap* collisions)
	{
		if (arc.Reforged) {
			GetReforgedJumpPath(arc, start, direction, collisions, path);
		} else {
			GetLegacyJumpPath(arc, start, direction, collisions, path);
		}
	}

	bool MeasureTool::Reaches(const JumpArc& arc, Vector2f start, Vector2f target, Tiles::TileMap* collisions)
	{
		float direction = (target.X >= start.X ? 1.0f : -1.0f);
		SmallVector<Vector2f, 0> path;
		GetJumpPath(arc, start, direction, path, collisions);
		// Where the path passes the target, it has to be on it or above it
		for (std::size_t i = 1; i < path.size(); i++) {
			float x0 = path[i - 1].X, x1 = path[i].X;
			if ((target.X - x0) * direction < 0.0f || (target.X - x1) * direction > 0.0f) {
				continue;
			}
			float t = (x1 != x0 ? (target.X - x0) / (x1 - x0) : 0.0f);
			float y = path[i - 1].Y + (path[i].Y - path[i - 1].Y) * t;
			return (y <= target.Y);
		}
		return false;
	}

	void MeasureTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		std::int32_t spriteLayer = _host.GetDocument().GetSpriteLayerIndex();
		LayerView view = _host.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));

		if (!_hasMeasure) {
			if (hoverValid) {
				// Where it would start
				painter.Circle(view.WorldToScreen(GetPoint(hover)), std::max(2.0f, painter.Px(2.5f)), UiTheme::Cursor, UiLayer::Overlay);
			}
			return;
		}

		Vector2f a = view.WorldToScreen(_start);
		Vector2f b = view.WorldToScreen(_end);
		Vector2f delta = _end - _start;
		const ToolSettings& settings = GetSettings();
		Tiles::TileMap* collisions = (settings.ArcCollisions ? _host.GetTileMap() : nullptr);
		bool reforged = PreferencesCache::EnableReforgedGameplay;
		float direction = (delta.X >= 0.0f ? 1.0f : -1.0f);

		// From a spring, how it throws the player instead of the jumps
		String springText;
		LevelDocument& document = _host.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		Vector2i startCell(std::int32_t(std::floor(_start.X / 32.0f)), std::int32_t(std::floor(_start.Y / 32.0f)));
		if (startCell.X >= 0 && startCell.Y >= 0 && startCell.X < gridSize.X && startCell.Y < gridSize.Y) {
			const Formats::EventRecord& event = document.GetEvent(startCell.X, startCell.Y);
			std::int32_t orientation = event.Params[1];
			// The springs of the original that face either way are told by where the measure goes
			bool horizontal = (orientation == 1 || orientation >= 3);
			if (EventType(event.Type) == EventType::Spring && orientation != 2) {
				JumpArc arc{ 0.0f, false, reforged };
				arc.Spring = GetSpringStrength(event.Params[0], horizontal, reforged);
				arc.SpringHorizontal = horizontal;
				float springDirection = (orientation == 1 ? 1.0f : (orientation == 3 ? -1.0f : direction));
				Vector2f springStart(startCell.X * 32.0f + 16.0f, startCell.Y * 32.0f + 32.0f);
				SmallVector<Vector2f, 0> path;
				GetJumpPath(arc, springStart, springDirection, path, collisions);
				float top = springStart.Y, far = springStart.X;
				for (const Vector2f& p : path) {
					top = std::min(top, p.Y);
					far = (springDirection > 0.0f ? std::max(far, p.X) : std::min(far, p.X));
				}
				for (Vector2f& p : path) {
					p = view.WorldToScreen(p);
				}
				painter.Polyline(path, thickness + 1.0f, UiTheme::OverlayCaution, UiLayer::Overlay);
				char buffer[64];
				if (horizontal) {
					std::snprintf(buffer, sizeof(buffer), "%.1f", std::abs(far - springStart.X) / 32.0f);
					springText = _f("The spring throws the player {} tiles far", buffer);
				} else {
					std::snprintf(buffer, sizeof(buffer), "%.1f", (springStart.Y - top) / 32.0f);
					springText = _f("The spring throws the player {} tiles high", buffer);
				}
			}
		}

		// The jumps from the start towards the end, the first one that gets there is told
		static const char* const ReachTexts[] = {
			N_("Walking jump gets there"), N_("Only dashing jump gets there"), N_("Only double jump gets there"),
			N_("Only copter gets there")
		};
		std::int32_t reachingArc = -1;
		const JumpArc* arcs = GetArcs();
		if (springText.empty()) {
			// From the easiest, walking and dashing go before the double jump and the copter
			for (std::int32_t i = 0; i < ArcCount; i++) {
				if ((settings.ShownArcs & ArcBits[i]) == 0) {
					continue;
				}
				SmallVector<Vector2f, 0> path;
				GetJumpPath(arcs[i], _start, direction, path, collisions);
				for (Vector2f& p : path) {
					p = view.WorldToScreen(p);
				}
				Colorf color = UiTheme::OverlaySeries[i];
				color.A = 0.85f;
				painter.Polyline(path, thickness, color, UiLayer::Overlay);
				if (reachingArc < 0 && std::abs(delta.X) >= 1.0f && Reaches(arcs[i], _start, _end, collisions)) {
					reachingArc = i;
				}
			}
		}

		// The measured line, its ends, and how long it is
		Vector2f line[2] = { a, b };
		painter.Polyline(line, thickness + 1.0f, Colorf::White, UiLayer::Overlay);
		float dot = std::max(2.0f, painter.Px(2.5f));
		painter.Circle(a, dot, UiTheme::Accent, UiLayer::Overlay);
		painter.Circle(b, dot, UiTheme::Accent, UiLayer::Overlay);

		char buffer[96];
		std::snprintf(buffer, sizeof(buffer), "%d x %d px - %.1f x %.1f tiles - %d px", std::int32_t(std::abs(delta.X)), std::int32_t(std::abs(delta.Y)),
			std::abs(delta.X) / 32.0f, std::abs(delta.Y) / 32.0f, std::int32_t(std::round(delta.Length())));
		String text = buffer;
		String reach;
		if (!springText.empty()) {
			reach = springText;
		} else if (std::abs(delta.X) >= 1.0f && settings.ShownArcs != 0) {
			reach = (reachingArc >= 0 ? String(_(ReachTexts[reachingArc])) : String(_("No jump gets there")));
		}

		float padding = painter.Px(UiTheme::Padding);
		float rowHeight = painter.Px(UiTheme::RowHeight);
		float width = std::max(painter.MeasureText(text), painter.MeasureText(reach)) + padding * 2.0f;
		float height = rowHeight * (reach.empty() ? 1.0f : 2.0f);
		// Under the middle of the line, where the arcs above it aren't
		Vector2f labelPos(std::round((a.X + b.X - width) * 0.5f), std::round(std::max(a.Y, b.Y) + painter.Px(10.0f)));
		Rectf label(labelPos.X, labelPos.Y, width, height);
		// Over the arcs and the line
		painter.FillRect(label, UiTheme::PanelFill, UiLayer::Overlay);
		painter.Frame(label, UiTheme::PanelBorderDim, thickness, UiLayer::Overlay);
		painter.TextInRect(text, Rectf(label.X + padding, label.Y, label.W - padding * 2.0f, rowHeight), UiTheme::Text, Alignment::Left, UiLayer::OverlayText);
		if (!reach.empty()) {
			painter.TextInRect(reach, Rectf(label.X + padding, label.Y + rowHeight, label.W - padding * 2.0f, rowHeight),
				(reachingArc < 0 && springText.empty()) ? UiTheme::TextDanger : UiTheme::TextAccent, Alignment::Left, UiLayer::OverlayText);
		}
	}

	StringView MeasureTool::GetHint() const
	{
		return _("Drag: measure, with the jumps of the player, from a spring how it throws - Ctrl: snap to tiles - Right: clear");
	}

	void MeasureTool::OnDeactivate()
	{
		Cancel();
	}

	void MeasureTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		const ToolOptionItem arcs[] = {
			// TRANSLATORS: Short names of the jumps of the player the measure tool shows, in the toolbar
			{ _("Walk"), EditorIcon::None, ActionIds::MeasureWalk },
			{ _("Dash"), EditorIcon::None, ActionIds::MeasureDash },
			{ _("Double"), EditorIcon::None, ActionIds::MeasureDoubleJump },
			{ _("Copter"), EditorIcon::None, ActionIds::MeasureCopter },
		};
		// TRANSLATORS: Label of the jumps the measure tool shows
		builder.AddToggles(_("Jumps"), arcs);
		builder.AddSeparator();
		// TRANSLATORS: Toggle in the toolbar, short for the jumps stopping at solid tiles
		builder.AddAction(ActionIds::MeasureCollisions, _("Stop at walls"));
	}
}

#endif
