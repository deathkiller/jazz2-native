#include "LayerResizeHandles.h"

#if defined(WITH_EDITOR)

#include "LayerDialogs.h"
#include "ShellCommon.h"
#include "../Document/Commands.h"
#include "../Tools/EditorContext.h"
#include "../UI/UiSurface.h"
#include "../../Formats/LevelFile.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Input/IInputManager.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// How far from the frame a handle is taken, in units, more for a finger
		constexpr float GrabDistance = 4.0f;
		constexpr float TouchGrabDistance = 10.0f;
		// Size of the squares of the handles in units
		constexpr float HandleSize = 5.0f;
		constexpr float TouchHandleSize = 8.0f;

		bool IsTouch(const UiElement& element)
		{
			UiSurface* surface = element.GetSurface();
			return (surface != nullptr && surface->TouchInput);
		}

		String FormatChange(std::int32_t delta)
		{
			return (delta > 0 ? String("+"_s + FormatInteger(delta)) : FormatInteger(delta));
		}

		// The modifier keys held now, for what a handle only pointed at would do; on Apple platforms Cmd is Ctrl, as for shortcuts
		std::int32_t GetHeldModifiers()
		{
			const KeyboardState& keyboard = theApplication().GetInputManager().keyboardState();
			std::int32_t modifiers = 0;
			if (keyboard.isKeyDown(Keys::LCtrl) || keyboard.isKeyDown(Keys::RCtrl)) {
				modifiers |= KeyMod::Ctrl;
			}
	#if defined(DEATH_TARGET_APPLE)
			if (keyboard.isKeyDown(Keys::LSuper) || keyboard.isKeyDown(Keys::RSuper)) {
				modifiers |= KeyMod::Ctrl;
			}
	#endif
			if (keyboard.isKeyDown(Keys::LShift) || keyboard.isKeyDown(Keys::RShift)) {
				modifiers |= KeyMod::Shift;
			}
			return modifiers;
		}
	}

	LayerResizeHandles::LayerResizeHandles(IEditorContext& session, Function<Rectf()>&& getViewRect, Function<Rectf(const LayerView&)>&& getFrame)
		: _session(session), _getViewRect(Death::move(getViewRect)), _getFrame(Death::move(getFrame)), _dragging(false), _edges(0), _layer(-1)
	{
	}

	bool LayerResizeHandles::IsActive() const
	{
		if (!_session.ArePanelsShown()) {
			return false;
		}
		// Only with the tools that never paint, chosen for good, not the hand held for a while
		ToolManager& tools = _session.GetTools();
		EditorToolType chosen = tools.GetChosenType();
		if (chosen != EditorToolType::Select && chosen != EditorToolType::Hand) {
			return false;
		}
		EditorTool* active = tools.GetActive();
		if (active == nullptr || active->IsBusy()) {
			return false;
		}
		const LevelDocument& document = _session.GetDocument();
		std::int32_t layer = _session.GetCurrentLayer();
		return (layer >= 0 && layer < std::int32_t(document.Layers.size()) && !IsLayerLocked(document, layer) && _session.IsLayerEditable(layer));
	}

	bool LayerResizeHandles::WantsMove(std::int32_t modifiers) const
	{
		const LevelDocument& document = _session.GetDocument();
		std::int32_t layer = _session.GetCurrentLayer();
		// The sprite layer is always where the game places it, it has no offsets
		return ((modifiers & KeyMod::Ctrl) != 0 && layer >= 0 && layer < std::int32_t(document.Layers.size()) && !document.Layers[layer].IsSpriteLayer());
	}

	void LayerResizeHandles::SetLayerOffset(Vector2f offset)
	{
		const LevelDocument& document = _session.GetDocument();
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[_layer];
		if (layer.Record.OffsetX == offset.X && layer.Record.OffsetY == offset.Y) {
			return;
		}
		Formats::LayerRecord record = layer.Record;
		record.OffsetX = offset.X;
		record.OffsetY = offset.Y;
		// Merged with the moves before it into one step, which undoes to where it started, see EndDrag(); either offset or both
		// change as it's dragged
		// TRANSLATORS: Name of an undo step in the history, a layer moved by dragging its frame with Ctrl, which changes its offsets
		auto command = std::make_unique<SetLayerPropertiesCommand>(document, _layer, record, layer.Name, _x("editor", "Move layer"));
		command->SetMergesAnyField(true);
		_session.Execute(Death::move(command), true);
	}

	bool LayerResizeHandles::GetFrame(Rectf& frame, Rectf& viewRect) const
	{
		std::int32_t layer = _session.GetCurrentLayer();
		if (layer < 0 || layer >= std::int32_t(_session.GetDocument().Layers.size())) {
			return false;
		}
		LayerView view = _session.GetLayerView(layer);
		if (view.Size.X <= 0 || view.Size.Y <= 0) {
			return false;
		}
		frame = _getFrame(view);
		viewRect = _getViewRect();
		return (frame.W > 0.0f && frame.H > 0.0f && viewRect.W > 0.0f && viewRect.H > 0.0f);
	}

	float LayerResizeHandles::GetGrabDistance() const
	{
		UiSurface* surface = GetSurface();
		float scale = (surface != nullptr ? surface->GetScale() : 1.0f);
		return std::round((IsTouch(*this) ? TouchGrabDistance : GrabDistance) * scale);
	}

	std::uint8_t LayerResizeHandles::FindEdges(Vector2f pos) const
	{
		Rectf frame, viewRect;
		if (!GetFrame(frame, viewRect) || !viewRect.Contains(pos)) {
			return 0;
		}
		float grab = GetGrabDistance();
		float left = frame.X, right = frame.X + frame.W, top = frame.Y, bottom = frame.Y + frame.H;
		if (pos.X < left - grab || pos.X > right + grab || pos.Y < top - grab || pos.Y > bottom + grab) {
			return 0;
		}
		// The nearer edge of each axis, a layer small on the screen has them close to each other
		std::uint8_t edges = 0;
		float toLeft = std::abs(pos.X - left), toRight = std::abs(pos.X - right);
		if (std::min(toLeft, toRight) <= grab) {
			edges |= (toLeft < toRight ? EdgeLeft : EdgeRight);
		}
		float toTop = std::abs(pos.Y - top), toBottom = std::abs(pos.Y - bottom);
		if (std::min(toTop, toBottom) <= grab) {
			edges |= (toTop < toBottom ? EdgeTop : EdgeBottom);
		}
		return edges;
	}

	UiElement* LayerResizeHandles::HitTest(Vector2f pos)
	{
		if (!IsShown()) {
			return nullptr;
		}
		// Anywhere while dragging, otherwise only over a handle, the rest of the view is left to the tools
		if (_dragging || (IsActive() && FindEdges(pos) != 0)) {
			return this;
		}
		return nullptr;
	}

	bool LayerResizeHandles::AcceptsPointer() const
	{
		return true;
	}

	bool LayerResizeHandles::YieldsToViewGestures() const
	{
		return true;
	}

	bool LayerResizeHandles::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left || _dragging || !IsActive()) {
			return false;
		}
		std::uint8_t edges = FindEdges(event.Pos);
		Rectf frame, viewRect;
		if (edges == 0 || !GetFrame(frame, viewRect)) {
			return false;
		}
		const DocumentLayer& layer = _session.GetDocument().Layers[_session.GetCurrentLayer()];
		_layer = _session.GetCurrentLayer();
		_startSize = Vector2i(layer.GetWidth(), layer.GetHeight());
		_size = _startSize;
		_offset = Vector2i(0, 0);
		_edges = edges;
		_moving = WantsMove(event.Modifiers);
		_pressPos = event.Pos;
		_startLayerOffset = Vector2f(layer.Record.OffsetX, layer.Record.OffsetY);
		if (_moving) {
			// A step of its own, not merged with a change of the properties just before
			_session.GetHistory().BreakMerge();
		}
		_grab = Vector2f((edges & EdgeLeft) != 0 ? event.Pos.X - frame.X : ((edges & EdgeRight) != 0 ? event.Pos.X - (frame.X + frame.W) : 0.0f),
			(edges & EdgeTop) != 0 ? event.Pos.Y - frame.Y : ((edges & EdgeBottom) != 0 ? event.Pos.Y - (frame.Y + frame.H) : 0.0f));
		_dragging = true;
		if (UiSurface* surface = GetSurface()) {
			surface->SetFocus(this);
		}
		return true;
	}

	void LayerResizeHandles::OnPointerMove(const UiPointerEvent& event)
	{
		Rectf frame, viewRect;
		if (!_dragging || !GetFrame(frame, viewRect) || _startSize.X <= 0 || _startSize.Y <= 0) {
			return;
		}
		if (_moving) {
			// By whole pixels of the level, or tiles with Shift, from where it was pressed; a larger offset moves the layer to
			// the left and up (see Tiles::LayerDescription::GetGameplayOrigin())
			float scale = std::max(_session.GetLayerView(_layer).Scale, 0.001f);
			Vector2f delta((event.Pos.X - _pressPos.X) / scale, (event.Pos.Y - _pressPos.Y) / scale);
			float unit = ((event.Modifiers & KeyMod::Shift) != 0 ? float(Formats::LevelFile::TileSize) : 1.0f);
			delta = Vector2f(std::round(delta.X / unit) * unit, std::round(delta.Y / unit) * unit);
			Vector2f offset(std::clamp(_startLayerOffset.X - delta.X, -32768.0f, 32767.0f), std::clamp(_startLayerOffset.Y - delta.Y, -32768.0f, 32767.0f));
			SetLayerOffset(offset);
			return;
		}
		// By whole tiles, measured from the edge that stays, as the view is now
		float tileX = frame.W / float(_startSize.X), tileY = frame.H / float(_startSize.Y);
		Vector2f p(event.Pos.X - _grab.X, event.Pos.Y - _grab.Y);
		Vector2i size = _startSize;
		if ((_edges & EdgeRight) != 0) {
			size.X = std::int32_t(std::round((p.X - frame.X) / tileX));
		} else if ((_edges & EdgeLeft) != 0) {
			size.X = std::int32_t(std::round((frame.X + frame.W - p.X) / tileX));
		}
		if ((_edges & EdgeBottom) != 0) {
			size.Y = std::int32_t(std::round((p.Y - frame.Y) / tileY));
		} else if ((_edges & EdgeTop) != 0) {
			size.Y = std::int32_t(std::round((frame.Y + frame.H - p.Y) / tileY));
		}
		size.X = std::clamp<std::int32_t>(size.X, 1, UINT16_MAX);
		size.Y = std::clamp<std::int32_t>(size.Y, 1, UINT16_MAX);
		// No larger than a level can have, the axis dragged gives way
		if (std::int64_t(size.X) * std::int64_t(size.Y) > Formats::LevelFile::MaxLayerTiles) {
			if ((_edges & (EdgeLeft | EdgeRight)) != 0) {
				size.X = std::max<std::int32_t>(1, std::int32_t(Formats::LevelFile::MaxLayerTiles / size.Y));
			} else {
				size.Y = std::max<std::int32_t>(1, std::int32_t(Formats::LevelFile::MaxLayerTiles / size.X));
			}
		}
		_size = size;
		// The opposite edge stays, so the tiles move by as much as the layer grows on the left or the top
		_offset = Vector2i((_edges & EdgeLeft) != 0 ? size.X - _startSize.X : 0, (_edges & EdgeTop) != 0 ? size.Y - _startSize.Y : 0);
	}

	void LayerResizeHandles::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (!_dragging) {
			return;
		}
		OnPointerMove(event);
		if (_moving) {
			// It was moved as it was dragged, the step ends here
			EndDrag();
			return;
		}
		std::int32_t layerIndex = _layer;
		Vector2i size = _size, offset = _offset, startSize = _startSize;
		EndDrag();

		const LevelDocument& document = _session.GetDocument();
		if (layerIndex != _session.GetCurrentLayer() || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size()) ||
			(size == startSize && offset == Vector2i(0, 0))) {
			return;
		}
		// Not if the level is played meanwhile, the layer was locked or hidden, or it has another size now, as after an
		// undo, the drag was measured from the size it had
		const DocumentLayer& current = document.Layers[layerIndex];
		if (_session.IsPlaying() || IsLayerLocked(document, layerIndex) || !_session.IsLayerEditable(layerIndex) ||
			current.GetWidth() != startSize.X || current.GetHeight() != startSize.Y) {
			return;
		}
		// As the dialog has it by default, a layer that repeats is most likely to be wanted repeated further
		const DocumentLayer& layer = current;
		bool repeats = (layer.Record.Flags & (Formats::LayerRecord::RepeatX | Formats::LayerRecord::RepeatY)) != 0;
		ResizeLayerAsStep(_session, layerIndex, size, offset, repeats ? LevelDocument::ResizeFill::Repeat : LevelDocument::ResizeFill::Empty);
	}

	void LayerResizeHandles::OnPointerCancel()
	{
		// A layer moved stays where it was moved, as a number dragged in the properties does
		EndDrag();
	}

	bool LayerResizeHandles::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_dragging && event.sym == Keys::Escape) {
			if (_moving) {
				// Back where it was, within the same step
				SetLayerOffset(_startLayerOffset);
			}
			EndDrag();
			return true;
		}
		return false;
	}

	bool LayerResizeHandles::IsFocusable() const
	{
		return _dragging;
	}

	bool LayerResizeHandles::IsTabStop() const
	{
		return false;
	}

	StringView LayerResizeHandles::GetTooltip(Vector2f pos) const
	{
		if (_dragging) {
			return {};
		}
		// The sprite layer can't be moved
		return (WantsMove(KeyMod::Ctrl) ? _x("editor", "Drag to resize the layer - Ctrl+drag moves it, with Shift by whole tiles - Esc cancels")
			: _x("editor", "Drag to resize the layer - Esc cancels"));
	}

	void LayerResizeHandles::EndDrag()
	{
		if (_dragging && _moving) {
			_session.GetHistory().BreakMerge();
		}
		_moving = false;
		_dragging = false;
		if (IsFocused()) {
			if (UiSurface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
			}
		}
	}

	void LayerResizeHandles::Draw(UiPainter& painter)
	{
		if (!_dragging && !IsActive()) {
			return;
		}
		Rectf frame, viewRect;
		if (!GetFrame(frame, viewRect) || _startSize.X < 0) {
			return;
		}

		UiSurface* surface = GetSurface();
		Vector2f pointer = (surface != nullptr ? surface->GetPointerPos() : Vector2f());
		bool touch = IsTouch(*this);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		painter.PushClip(viewRect);

		// Moved by its offsets, the layer moves as it's dragged, so its frame does too
		bool moving = (_dragging ? _moving : WantsMove(GetHeldModifiers()));

		// The frame as it will be, by whole tiles
		Rectf target = frame;
		if (_dragging && !_moving && _startSize.X > 0 && _startSize.Y > 0) {
			float tileX = frame.W / float(_startSize.X), tileY = frame.H / float(_startSize.Y);
			target = Rectf(frame.X - _offset.X * tileX, frame.Y - _offset.Y * tileY, _size.X * tileX, _size.Y * tileY);
			Rectf outer(std::round(target.X) - thickness, std::round(target.Y) - thickness, std::round(target.W) + thickness * 2.0f, std::round(target.H) + thickness * 2.0f);
			painter.Frame(Rectf(outer.X - thickness, outer.Y - thickness, outer.W + thickness * 2.0f, outer.H + thickness * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.5f), thickness, UiLayer::Overlay);
			painter.Frame(outer, UiTheme::Accent, thickness * 2.0f, UiLayer::Overlay);
		}

		// The squares of the handles, the one pointed at or dragged highlighted
		std::uint8_t highlighted = (_dragging ? _edges : (IsHovered() ? FindEdges(pointer) : 0));
		float size = painter.Px(touch ? TouchHandleSize : HandleSize);
		float xs[3] = { target.X, target.X + target.W * 0.5f, target.X + target.W };
		float ys[3] = { target.Y, target.Y + target.H * 0.5f, target.Y + target.H };
		static const std::uint8_t EdgesX[3] = { EdgeLeft, 0, EdgeRight };
		static const std::uint8_t EdgesY[3] = { EdgeTop, 0, EdgeBottom };
		for (std::int32_t y = 0; y < 3; y++) {
			for (std::int32_t x = 0; x < 3; x++) {
				std::uint8_t edges = std::uint8_t(EdgesX[x] | EdgesY[y]);
				if (edges == 0) {
					continue;
				}
				Rectf square(std::round(xs[x] - size * 0.5f), std::round(ys[y] - size * 0.5f), size, size);
				bool lit = (highlighted == edges);
				painter.FillRect(Rectf(square.X - thickness, square.Y - thickness, square.W + thickness * 2.0f, square.H + thickness * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.6f), UiLayer::Overlay);
				painter.FillRect(square, lit ? UiTheme::Accent : Colorf(1.0f, 1.0f, 1.0f, 0.9f), UiLayer::Overlay);
			}
		}

		// Arrows by the pointer, which way the edge goes, as the cursor of a window would show it (the surface draws them
		// over everything and leaves them out for a finger)
		if (highlighted != 0 && surface != nullptr) {
			surface->SetCursorOverlay(moving ? UiCursorOverlay::Move : UiSurface::GetResizeOverlay((highlighted & EdgeLeft) != 0, (highlighted & EdgeTop) != 0,
				(highlighted & EdgeRight) != 0, (highlighted & EdgeBottom) != 0));
		}

		// The offsets it has now, or the size it will have, in a tooltip by the pointer (the surface keeps it below the arrows
		// and on the screen)
		const LevelDocument& document = _session.GetDocument();
		if (_dragging && _moving && surface != nullptr && _layer >= 0 && _layer < std::int32_t(document.Layers.size())) {
			const Formats::LayerRecord& record = document.Layers[_layer].Record;
			// TRANSLATORS: Tooltip while a layer is moved by dragging its frame with Ctrl, its offsets in pixels
			surface->SetPointerTooltip(_fx("editor", "Offset {}, {}", std::int32_t(std::round(record.OffsetX)), std::int32_t(std::round(record.OffsetY))));
		} else if (_dragging && surface != nullptr) {
			// TRANSLATORS: Tooltip while a layer is resized by dragging its edge, the new width and height of the layer in tiles
			String text = _fx("editor", "{} x {} tiles", _size.X, _size.Y);
			Vector2i delta(_size.X - _startSize.X, _size.Y - _startSize.Y);
			if (delta != Vector2i(0, 0)) {
				text += " ("_s + FormatChange(delta.X) + ", "_s + FormatChange(delta.Y) + ")"_s;
			}
			surface->SetPointerTooltip(text);
		}

		painter.PopClip();
	}
}

#endif
