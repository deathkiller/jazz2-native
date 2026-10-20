#include "EditorCamera.h"

#if defined(WITH_EDITOR)

#include "../UI/UiTheme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iterator>

namespace Jazz2::Editor
{
	namespace
	{
		// Steps of the zoom, scales above 1 are whole numbers, so every pixel of the level is just as large
		const float ScaleSteps[] = {
			1.0f / 32.0f, 1.0f / 24.0f, 1.0f / 16.0f, 1.0f / 12.0f, 1.0f / 10.0f,
			1.0f / 8.0f, 1.0f / 6.0f, 1.0f / 5.0f, 1.0f / 4.0f, 1.0f / 3.0f, 1.0f / 2.0f, 2.0f / 3.0f, 3.0f / 4.0f,
			1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f, 20.0f, 24.0f, 28.0f, 32.0f
		};

		// The farthest the view gets, a tile is a pixel there
		constexpr float MinScale = 1.0f / 32.0f;
		// How much closer the view than in the game can get
		constexpr float MaxZoomOfGameplay = 8.0f;
		// The curvature of the flight, how much it rather zooms out than pans (rho of van Wijk and Nuij)
		constexpr float FlyCurvature = 1.42f;
		// How fast a flight goes, in widths of the view per second
		constexpr float FlySpeed = 1.6f;
		constexpr float MinFlyDuration = 0.35f;
		constexpr float MaxFlyDuration = 1.2f;
		// Part of the remaining zoom done each frame (at 60 FPS)
		constexpr float ZoomSmoothing = 0.3f;
		// Part of the speed kept each frame after the view was let go
		constexpr float MomentumDecay = 0.88f;

		float EaseInOut(float t)
		{
			return t * t * (3.0f - 2.0f * t);
		}
	}

	EditorCamera::EditorCamera()
		: _viewSize(1, 1), _gameplayScale(1.0f), _levelBounds(), _center(), _scale(1.0f),
			_zooming(false), _targetScale(1.0f), _zoomStepsLeft(0.0f), _dragging(false), _dragFrames(0.0f),
			_flying(false), _flyTime(0.0f), _flyDuration(0.0f), _flyLength(0.0f), _flyDistance(0.0f),
			_flyWidth0(1.0f), _flyWidth1(1.0f), _flyR0(0.0f)
	{
	}

	void EditorCamera::SetView(Vector2i viewSize, float gameplayScale)
	{
		_viewSize = Vector2i(std::max<std::int32_t>(viewSize.X, 1), std::max<std::int32_t>(viewSize.Y, 1));
		_gameplayScale = (gameplayScale > 0.0f ? gameplayScale : 1.0f);
		_scale = std::clamp(_scale, GetMinScale(), GetMaxScale());
		_targetScale = std::clamp(_targetScale, GetMinScale(), GetMaxScale());
	}

	void EditorCamera::SetLevelBounds(const Rectf& bounds)
	{
		_levelBounds = bounds;
		ClampCenter();
	}

	float EditorCamera::GetTargetScale() const
	{
		return (_zooming ? _targetScale : _scale);
	}

	float EditorCamera::GetMinScale() const
	{
		// From far away the layers are drawn as images (see TileLod), so the whole level fits however large it is
		float fitScale = 1.0f;
		if (_levelBounds.W > 0.0f && _levelBounds.H > 0.0f) {
			fitScale = std::min(_viewSize.X / _levelBounds.W, _viewSize.Y / _levelBounds.H) * 0.5f;
		}
		return std::max(MinScale, std::min(fitScale, 1.0f));
	}

	float EditorCamera::GetMaxScale() const
	{
		return std::max(_gameplayScale * MaxZoomOfGameplay, 4.0f);
	}

	Vector2f EditorCamera::ViewToWorld(Vector2f viewPos) const
	{
		return (viewPos - Vector2f(float(_viewSize.X), float(_viewSize.Y)) * 0.5f) / _scale + _center;
	}

	Vector2f EditorCamera::WorldToView(Vector2f worldPos) const
	{
		return (worldPos - _center) * _scale + Vector2f(float(_viewSize.X), float(_viewSize.Y)) * 0.5f;
	}

	void EditorCamera::JumpTo(Vector2f center, float scale)
	{
		Stop();
		_center = center;
		_scale = std::clamp(scale, GetMinScale(), GetMaxScale());
		_targetScale = _scale;
		ClampCenter();
	}

	void EditorCamera::ZoomSteps(Vector2f viewPos, float steps)
	{
		_zoomStepsLeft += steps;
		float targetScale = GetTargetScale();
		while (_zoomStepsLeft >= 1.0f) {
			targetScale = SnapScale(targetScale, 1);
			_zoomStepsLeft -= 1.0f;
		}
		while (_zoomStepsLeft <= -1.0f) {
			targetScale = SnapScale(targetScale, -1);
			_zoomStepsLeft += 1.0f;
		}
		if (targetScale != GetTargetScale()) {
			ZoomTo(viewPos, targetScale);
		}
	}

	void EditorCamera::ZoomTo(Vector2f viewPos, float scale)
	{
		_flying = false;
		_targetScale = std::clamp(scale, GetMinScale(), GetMaxScale());
		_zoomAnchorView = viewPos;
		_zoomAnchorWorld = ViewToWorld(viewPos);
		_zooming = (_targetScale != _scale);
		if (_dragging) {
			// The dragged point stays under the pointer, it's the one zoomed at
			_dragAnchorWorld = ViewToWorld(_dragLastView);
		}
		if (_zooming && UiTheme::ReducedMotion) {
			// With reduced motion, at once
			_scale = _targetScale;
			_zooming = false;
			ApplyZoomAnchor();
		}
	}

	void EditorCamera::ZoomBy(Vector2f viewPos, float factor)
	{
		_flying = false;
		_zoomStepsLeft = 0.0f;
		_zoomAnchorView = viewPos;
		_zoomAnchorWorld = ViewToWorld(viewPos);
		// Wherever an animated zoom was going, it's from here now
		_scale = std::clamp(_scale * factor, GetMinScale(), GetMaxScale());
		_targetScale = _scale;
		_zooming = false;
		ApplyZoomAnchor();
	}

	void EditorCamera::PanBy(Vector2f viewDelta)
	{
		_flying = false;
		_center += viewDelta / _scale;
		if (_zooming) {
			_zoomAnchorWorld = ViewToWorld(_zoomAnchorView);
		}
		ClampCenter();
	}

	void EditorCamera::BeginDrag(Vector2f viewPos)
	{
		_flying = false;
		_dragging = true;
		_dragAnchorWorld = ViewToWorld(viewPos);
		_dragLastView = viewPos;
		_velocity = Vector2f::Zero;
		_dragFrames = 0.0f;
	}

	void EditorCamera::DragTo(Vector2f viewPos)
	{
		if (!_dragging) {
			return;
		}

		Vector2f prevCenter = _center;
		_center = _dragAnchorWorld - (viewPos - Vector2f(float(_viewSize.X), float(_viewSize.Y)) * 0.5f) / _scale;
		_dragLastView = viewPos;
		ClampCenter();

		Vector2f velocity = (_center - prevCenter) / std::max(_dragFrames, 1.0f);
		_velocity = _velocity * 0.4f + velocity * 0.6f;
		_dragFrames = 0.0f;

		if (_zooming) {
			_zoomAnchorView = viewPos;
			_zoomAnchorWorld = _dragAnchorWorld;
		}
	}

	void EditorCamera::EndDrag()
	{
		if (!_dragging) {
			return;
		}
		_dragging = false;
		// The pointer stood still before it was let go, so the view stays where it is, with reduced motion always
		if (_dragFrames > 4.0f || UiTheme::ReducedMotion) {
			_velocity = Vector2f::Zero;
		}
	}

	void EditorCamera::FlyTo(Vector2f center, float scale)
	{
		scale = std::clamp(scale, GetMinScale(), GetMaxScale());
		if (UiTheme::ReducedMotion) {
			// With reduced motion, the view is there at once
			JumpTo(center, scale);
			return;
		}

		_zooming = false;
		_dragging = false;
		_velocity = Vector2f::Zero;
		_zoomStepsLeft = 0.0f;

		_flyFrom = _center;
		_flyWidth0 = _viewSize.X / _scale;
		_flyWidth1 = _viewSize.X / scale;
		Vector2f delta = center - _center;
		_flyDistance = delta.Length();
		_flyDirection = (_flyDistance > 0.001f ? delta / _flyDistance : Vector2f::Zero);

		constexpr float rho = FlyCurvature;
		constexpr float rho2 = rho * rho;
		constexpr float rho4 = rho2 * rho2;
		if (_flyDistance < 0.5f) {
			// Only zooming, the flight is a straight zoom in or out
			_flyR0 = 0.0f;
			_flyLength = std::abs(std::log(_flyWidth1 / _flyWidth0)) / rho;
		} else {
			float w0 = _flyWidth0, w1 = _flyWidth1, u1 = _flyDistance;
			float b0 = (w1 * w1 - w0 * w0 + rho4 * u1 * u1) / (2.0f * w0 * rho2 * u1);
			float b1 = (w1 * w1 - w0 * w0 - rho4 * u1 * u1) / (2.0f * w1 * rho2 * u1);
			// log(-b + sqrt(b² + 1)) of the paper is -asinh(b), which doesn't lose the precision for a long flight from close up
			_flyR0 = -std::asinh(b0);
			float r1 = -std::asinh(b1);
			_flyLength = (r1 - _flyR0) / rho;
		}

		if (!(_flyLength > 0.0001f) || !std::isfinite(_flyLength)) {
			JumpTo(center, scale);
			return;
		}

		_flying = true;
		_flyTime = 0.0f;
		_flyDuration = std::clamp(_flyLength / FlySpeed, MinFlyDuration, MaxFlyDuration);
		_targetScale = scale;
	}

	void EditorCamera::Fit(const Rectf& worldRect, bool animate)
	{
		if (worldRect.W <= 0.0f || worldRect.H <= 0.0f) {
			return;
		}

		float scale = std::min(_viewSize.X / worldRect.W, _viewSize.Y / worldRect.H) * 0.95f;
		// The largest step that fits, so it's still crisp
		float snapped = SnapScale(scale * 1.0001f, -1);
		if (snapped <= scale) {
			scale = snapped;
		}
		Vector2f center = Vector2f(worldRect.X + worldRect.W * 0.5f, worldRect.Y + worldRect.H * 0.5f);
		if (animate) {
			FlyTo(center, scale);
		} else {
			JumpTo(center, scale);
		}
	}

	void EditorCamera::Stop()
	{
		_zooming = false;
		_flying = false;
		_dragging = false;
		_velocity = Vector2f::Zero;
		_zoomStepsLeft = 0.0f;
		_targetScale = _scale;
	}

	bool EditorCamera::IsMoving() const
	{
		return (_zooming || _flying || _velocity != Vector2f::Zero);
	}

	void EditorCamera::Update(float timeMult)
	{
		if (_flying) {
			_flyTime += timeMult / 60.0f;
			float t = std::min(_flyTime / _flyDuration, 1.0f);
			if (t >= 1.0f) {
				Vector2f target = _flyFrom + _flyDirection * _flyDistance;
				float scale = _targetScale;
				_flying = false;
				_center = target;
				_scale = scale;
				ClampCenter();
				return;
			}

			float s = EaseInOut(t) * _flyLength;
			_center = _flyFrom + _flyDirection * GetFlyDistance(s);
			_scale = _viewSize.X / GetFlyWidth(s);
			return;
		}

		if (_zooming) {
			float logScale = std::log(_scale);
			float logTarget = std::log(_targetScale);
			float factor = 1.0f - std::pow(1.0f - ZoomSmoothing, timeMult);
			logScale += (logTarget - logScale) * factor;
			if (std::abs(logTarget - logScale) < 0.002f) {
				logScale = logTarget;
				_zooming = false;
			}
			_scale = (_zooming ? std::exp(logScale) : _targetScale);
			ApplyZoomAnchor();
		}

		if (_dragging) {
			_dragFrames += timeMult;
		} else if (_velocity != Vector2f::Zero) {
			_center += _velocity * timeMult;
			_velocity *= std::pow(MomentumDecay, timeMult);
			if (_velocity.SqrLength() * _scale * _scale < 0.01f) {
				_velocity = Vector2f::Zero;
			}
			if (_zooming) {
				_zoomAnchorWorld = ViewToWorld(_zoomAnchorView);
			}
		}

		ClampCenter();
	}

	float EditorCamera::SnapScale(float scale, std::int32_t direction) const
	{
		float minScale = GetMinScale();
		float maxScale = GetMaxScale();
		float result = scale;

		if (direction > 0) {
			result = maxScale;
			for (float step : ScaleSteps) {
				if (step > scale * 1.001f) {
					result = step;
					break;
				}
			}
		} else if (direction < 0) {
			result = minScale;
			for (std::size_t i = std::size(ScaleSteps); i > 0; i--) {
				if (ScaleSteps[i - 1] < scale * 0.999f) {
					result = ScaleSteps[i - 1];
					break;
				}
			}
		} else {
			float bestDistance = FLT_MAX;
			for (float step : ScaleSteps) {
				float distance = std::abs(std::log(step / scale));
				if (distance < bestDistance) {
					bestDistance = distance;
					result = step;
				}
			}
		}

		return std::clamp(result, minScale, maxScale);
	}

	void EditorCamera::ClampCenter()
	{
		if (_levelBounds.W > 0.0f && _levelBounds.H > 0.0f) {
			// Half of the view can be outside of the level
			_center.X = std::clamp(_center.X, _levelBounds.X, _levelBounds.X + _levelBounds.W);
			_center.Y = std::clamp(_center.Y, _levelBounds.Y, _levelBounds.Y + _levelBounds.H);
		}
	}

	void EditorCamera::ApplyZoomAnchor()
	{
		if (_dragging) {
			_center = _dragAnchorWorld - (_dragLastView - Vector2f(float(_viewSize.X), float(_viewSize.Y)) * 0.5f) / _scale;
		} else {
			_center = _zoomAnchorWorld - (_zoomAnchorView - Vector2f(float(_viewSize.X), float(_viewSize.Y)) * 0.5f) / _scale;
		}
		ClampCenter();
	}

	float EditorCamera::GetFlyWidth(float s) const
	{
		constexpr float rho = FlyCurvature;
		if (_flyDistance < 0.5f) {
			float k = (_flyWidth1 < _flyWidth0 ? -1.0f : 1.0f);
			return _flyWidth0 * std::exp(k * rho * s);
		}
		return _flyWidth0 * std::cosh(_flyR0) / std::cosh(rho * s + _flyR0);
	}

	float EditorCamera::GetFlyDistance(float s) const
	{
		constexpr float rho = FlyCurvature;
		if (_flyDistance < 0.5f) {
			return 0.0f;
		}
		return _flyWidth0 / (rho * rho) * (std::cosh(_flyR0) * std::tanh(rho * s + _flyR0) - std::sinh(_flyR0));
	}
}

#endif
