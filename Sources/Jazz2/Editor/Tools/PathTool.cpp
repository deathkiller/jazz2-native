#include "PathTool.h"

#if defined(WITH_EDITOR)

#include "EventTools.h"
#include "../Common/ToolMath.h"
#include "../Document/Commands.h"
#include "../Events/EventPreviews.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		using ToolMath::CatmullRom;

		// Events along a path at most, a point far out with the pointer doesn't stop the editor
		constexpr std::size_t MaxPlacements = 4096;
	}

	PathTool::PathTool(IToolHost& host)
		: EditorTool(host), _options(std::make_shared<Options>())
	{
	}

	EditorToolType PathTool::GetType() const
	{
		return EditorToolType::Path;
	}

	void PathTool::OnDeactivate()
	{
		// The points stay, the hand can be taken for a while in the middle of a path
	}

	Vector2f PathTool::GetPoint(const ToolPointer& pointer) const
	{
		Vector2f world = pointer.World;
		if ((pointer.Modifiers & KeyMod::Ctrl) != 0) {
			world = Vector2f(std::floor(world.X / 32.0f) * 32.0f + 16.0f, std::floor(world.Y / 32.0f) * 32.0f + 16.0f);
		}
		return Vector2f(std::round(world.X), std::round(world.Y));
	}

	void PathTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button == MouseButton::Right) {
			Place();
			return;
		}
		if (pointer.Button != MouseButton::Left) {
			return;
		}
		if (_host.GetEventBrush().IsEmpty()) {
			_host.ShowMessage(_x("editor", "Choose the event to place first, in the list of events"));
			return;
		}
		_points.push_back(GetPoint(pointer));
	}

	void PathTool::Cancel()
	{
		_points.clear();
	}

	bool PathTool::IsBusy() const
	{
		return !_points.empty();
	}

	bool PathTool::HasPointerCapture() const
	{
		// The points are clicked one by one, the view can be moved between them
		return false;
	}

	void PathTool::GetLine(ArrayView<const Vector2f> points, bool smooth, SmallVectorImpl<Vector2f>& line)
	{
		if (points.empty()) {
			return;
		}
		// The path as a fine line, through the points smoothly if asked
		line.push_back(points[0]);
		for (std::size_t i = 0; i + 1 < points.size(); i++) {
			if (!smooth || points.size() < 3) {
				line.push_back(points[i + 1]);
				continue;
			}
			Vector2f p0 = points[i > 0 ? i - 1 : i], p1 = points[i], p2 = points[i + 1], p3 = points[i + 2 < points.size() ? i + 2 : i + 1];
			constexpr std::int32_t Subdivisions = 16;
			for (std::int32_t s = 1; s <= Subdivisions; s++) {
				line.push_back(CatmullRom(p0, p1, p2, p3, float(s) / float(Subdivisions)));
			}
		}
	}

	void PathTool::GetPlacements(ArrayView<const Vector2f> points, bool smooth, float spacing, SmallVectorImpl<Vector2f>& placements)
	{
		SmallVector<Vector2f, 0> line;
		GetLine(points, smooth, line);
		if (line.empty()) {
			return;
		}

		// Along it at even distances
		spacing = std::max(spacing, 4.0f);
		placements.push_back(line[0]);
		float carried = 0.0f;
		for (std::size_t i = 0; i + 1 < line.size(); i++) {
			Vector2f from = line[i], to = line[i + 1];
			float length = (to - from).Length();
			float position = spacing - carried;
			while (position <= length) {
				if (placements.size() >= MaxPlacements) {
					return;
				}
				placements.push_back(from + (to - from) * (position / length));
				position += spacing;
			}
			carried = length - (position - spacing);
		}
	}

	void PathTool::Place()
	{
		if (_points.empty()) {
			return;
		}
		const EventStamp& brush = _host.GetEventBrush();
		LevelDocument& document = _host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (brush.IsEmpty() || spriteLayer < 0) {
			_points.clear();
			return;
		}
		if (!_host.IsLayerEditable(spriteLayer)) {
			_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return;
		}

		SmallVector<Vector2f, 0> placements;
		GetPlacements(_points, _options->Smooth, _options->Spacing, placements);
		// A stamp of only events off the grid has no first one on it, its first one is placed
		Formats::EventRecord event = *brush.GetFirst();
		LevelDocument::NormalizeEvent(event);
		Vector2i gridSize = document.GetEventGridSize();
		std::int32_t placed = 0;

		if (_options->OnGrid) {
			// Each tile once, tiles that have an event keep it
			// TRANSLATORS: Name of an undo step in the history, after events were placed
			auto command = std::make_unique<SetEventsCommand>(document, _x("editor", "Place Events"));
			SmallVector<std::uint32_t, 0> cells;
			for (const Vector2f& p : placements) {
				std::int32_t x = std::int32_t(std::floor(p.X / 32.0f)), y = std::int32_t(std::floor(p.Y / 32.0f));
				if (x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y || !LevelDocument::IsEmptyEvent(document.GetEvent(x, y))) {
					continue;
				}
				cells.push_back(std::uint32_t(x + y * gridSize.X));
			}
			std::sort(cells.begin(), cells.end());
			cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
			for (std::uint32_t cell : cells) {
				command->Set(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)), event);
				placed++;
			}
			if (placed > 0) {
				_host.Execute(Death::move(command));
			}
		} else {
			SmallVector<Formats::OffGridEventRecord, 0> events = CopyOffGridEvents(document);
			for (const Vector2f& p : placements) {
				if (p.X < 0.0f || p.Y < 0.0f || p.X >= gridSize.X * 32.0f || p.Y >= gridSize.Y * 32.0f) {
					continue;
				}
				Formats::OffGridEventRecord& record = events.emplace_back();
				record.X = std::uint32_t(std::round(p.X));
				record.Y = std::uint32_t(std::round(p.Y));
				record.Event = event;
				placed++;
			}
			if (placed > 0) {
				_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), _x("editor", "Place Events")));
			}
		}
		_host.ShowMessage(_fnx("editor", "{} event placed", "{} events placed", placed, placed));
		_points.clear();
	}

	void PathTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		std::shared_ptr<Options> options = _options;
		// TRANSLATORS: Label of the distance between two events placed along a path, in pixels
		builder.AddNumber(_x("editor", "Distance"), _x("editor", "Distance between two events along the path in pixels, 32 is a tile"), 4.0f, 1024.0f, 1.0f,
			[options]() { return options->Spacing; }, [options](float value, bool final) { options->Spacing = std::clamp(value, 4.0f, 1024.0f); });
		builder.AddSeparator();
		// TRANSLATORS: Toggle in the toolbar, the path is a smooth curve through its points
		builder.AddToggle({ _x("editor", "Smooth"), EditorIcon::None, nullptr, _x("editor", "Through the points in a curve, otherwise straight from one to the next") },
			[options]() { return options->Smooth; }, [options](bool value) { options->Smooth = value; });
		// TRANSLATORS: Toggle in the toolbar, the events are placed on the tiles the path goes through
		builder.AddToggle({ _x("editor", "On tiles"), EditorIcon::None, nullptr, _x("editor", "Each on the tile the path goes through, otherwise off the grid, exactly on the path") },
			[options]() { return options->OnGrid; }, [options](bool value) { options->OnGrid = value; });
	}

	void PathTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		std::int32_t spriteLayer = _host.GetDocument().GetSpriteLayerIndex();
		LayerView view = _host.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));

		// The path with where the pointer is, as it would be with another point there
		SmallVector<Vector2f, 0> points;
		for (const Vector2f& p : _points) {
			points.push_back(p);
		}
		if (hoverValid) {
			points.push_back(GetPoint(hover));
		}
		if (points.empty()) {
			return;
		}

		SmallVector<Vector2f, 0> placements;
		GetPlacements(points, _options->Smooth, _options->Spacing, placements);
		const EventStamp& brush = _host.GetEventBrush();
		EventPreviews& previews = _host.GetEventPreviews();
		float tile = 32.0f * view.Scale;
		for (const Vector2f& p : placements) {
			Vector2f world = p;
			if (_options->OnGrid) {
				world = Vector2f(std::floor(p.X / 32.0f) * 32.0f + 16.0f, std::floor(p.Y / 32.0f) * 32.0f + 16.0f);
			}
			Vector2f screen = view.WorldToScreen(world);
			Rectf rect(screen.X - tile * 0.5f, screen.Y - tile * 0.5f, tile, tile);
			if (const Formats::EventRecord* first = brush.GetFirst()) {
				previews.DrawPlaced(painter, *first, rect, view.Scale, 0.6f, _host.GetAnimTime(), world);
			}
		}

		// The path, curved as it will be, and its points
		SmallVector<Vector2f, 0> line;
		GetLine(points, _options->Smooth, line);
		for (Vector2f& p : line) {
			p = view.WorldToScreen(p);
		}
		painter.Polyline(line, thickness, Colorf(1.0f, 1.0f, 1.0f, 0.5f), UiLayer::Overlay);
		for (const Vector2f& p : _points) {
			painter.Circle(view.WorldToScreen(p), std::max(2.0f, painter.Px(2.5f)), UiTheme::Accent, UiLayer::Overlay);
		}
	}

	StringView PathTool::GetHint() const
	{
		return _x("editor", "Click: add a point - Ctrl: middle of a tile - Right: place the events - Esc: start again");
	}
}

#endif
