#include "MeasureTool.h"

#if defined(WITH_EDITOR)

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

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
		// How far below its start a jump is followed
		constexpr float MaxDrop = 12.0f * 32.0f;
		constexpr std::int32_t MaxTicks = 400;

		constexpr MeasureTool::JumpArc Arcs[] = {
			{ 4.0f, false },	// Walking
			{ 16.0f, false },	// Dashing
			{ 4.0f, true },		// Spaz, walking
		};
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

	void MeasureTool::GetJumpPath(const JumpArc& arc, Vector2f start, float direction, SmallVectorImpl<Vector2f>& path)
	{
		// The launch is faster the faster the player runs, the original's -(10 + |speed| / 4)
		float speedY = -(JumpSpeed + arc.SpeedX * SpeedJumpScale);
		bool doubleJumped = false;
		Vector2f pos = start;
		path.push_back(pos);
		for (std::int32_t tick = 0; tick < MaxTicks; tick++) {
			pos.X += arc.SpeedX * direction;
			pos.Y += speedY;
			path.push_back(pos);
			if (speedY < 0.0f) {
				speedY += RiseGravity;
			} else {
				if (arc.DoubleJump && !doubleJumped) {
					// Spaz jumps again at the top
					speedY = -DoubleJumpSpeed;
					doubleJumped = true;
					continue;
				}
				speedY = std::min(speedY + FallGravity, FallSpeedCap);
			}
			if (pos.Y > start.Y + MaxDrop) {
				break;
			}
		}
	}

	bool MeasureTool::Reaches(const JumpArc& arc, Vector2f start, Vector2f target)
	{
		float direction = (target.X >= start.X ? 1.0f : -1.0f);
		SmallVector<Vector2f, 0> path;
		GetJumpPath(arc, start, direction, path);
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

		// The jumps from the start towards the end
		static const Colorf ArcColors[] = {
			Colorf(0.55f, 1.0f, 0.5f, 0.85f),
			Colorf(1.0f, 0.7f, 0.3f, 0.85f),
			Colorf(0.5f, 0.8f, 1.0f, 0.85f)
		};
		float direction = (delta.X >= 0.0f ? 1.0f : -1.0f);
		std::int32_t reachingArc = -1;
		for (std::int32_t i = 0; i < std::int32_t(arraySize(Arcs)); i++) {
			SmallVector<Vector2f, 0> path;
			GetJumpPath(Arcs[i], _start, direction, path);
			for (Vector2f& p : path) {
				p = view.WorldToScreen(p);
			}
			painter.Polyline(path, thickness, ArcColors[i], UiLayer::Overlay);
			if (reachingArc < 0 && std::abs(delta.X) >= 1.0f && Reaches(Arcs[i], _start, _end)) {
				reachingArc = i;
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
		if (std::abs(delta.X) >= 1.0f) {
			switch (reachingArc) {
				case 0: reach = _("A walking jump gets there"); break;
				case 1: reach = _("Only a dashing jump gets there"); break;
				case 2: reach = _("Only the double jump of Spaz gets there"); break;
				default: reach = _("No jump gets there"); break;
			}
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
				reachingArc < 0 ? UiTheme::TextDanger : UiTheme::TextAccent, Alignment::Left, UiLayer::OverlayText);
		}
	}

	StringView MeasureTool::GetHint() const
	{
		return _("Drag: measure, with the jumps of the player - Ctrl: snap to tiles - Right: clear");
	}
}

#endif
