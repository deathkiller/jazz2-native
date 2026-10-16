#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2::Editor
{
	/**
		@brief Camera of the level editor

		Moves freely over the level at any scale --- how many pixels of the view one pixel of the level takes.
		Zooming goes through steps that keep pixels crisp, and is animated, keeping still the point it zooms at.
		A dragged view keeps moving for a while after it's let go, and @ref FlyTo() zooms out and back in on
		the way to a distant place (the smooth zoom and pan of van Wijk and Nuij).

		A position in the view is in pixels from its top left corner.
	*/
	class EditorCamera
	{
	public:
		EditorCamera();

		/** @brief Sets the size of the view and the scale at which it shows as much of the level as the game does */
		void SetView(Vector2i viewSize, float gameplayScale);
		/** @brief Sets the bounds of the level, the center of the view stays inside of them */
		void SetLevelBounds(const Rectf& bounds);

		/** @brief Returns the size of the view */
		Vector2i GetViewSize() const {
			return _viewSize;
		}
		/** @brief Returns the center of the view in the level */
		Vector2f GetCenter() const {
			return _center;
		}
		/** @brief Returns how many pixels of the view one pixel of the level takes */
		float GetScale() const {
			return _scale;
		}
		/** @brief Returns the scale at which the view shows as much of the level as the game does */
		float GetGameplayScale() const {
			return _gameplayScale;
		}
		/** @brief Returns the scale the camera is getting to, the same as @ref GetScale() when it's not moving */
		float GetTargetScale() const;
		/** @brief Returns the smallest scale */
		float GetMinScale() const;
		/** @brief Returns the largest scale */
		float GetMaxScale() const;

		/** @brief Converts a position in the view to a position in the level */
		Vector2f ViewToWorld(Vector2f viewPos) const;
		/** @brief Converts a position in the level to a position in the view */
		Vector2f WorldToView(Vector2f worldPos) const;

		/** @brief Moves the camera at once, stopping any movement */
		void JumpTo(Vector2f center, float scale);
		/** @brief Zooms by a number of steps, positive in, keeping still a point of the view */
		void ZoomSteps(Vector2f viewPos, float steps);
		/** @brief Zooms to a scale, keeping still a point of the view */
		void ZoomTo(Vector2f viewPos, float scale);
		/**
		 * @brief Zooms by a factor at once, keeping still a point of the view
		 *
		 * Not snapped to the steps, for zooming smoothly by a little every frame, as with a trigger of a gamepad.
		 */
		void ZoomBy(Vector2f viewPos, float factor);
		/** @brief Moves the view by a distance in pixels of the view, at once */
		void PanBy(Vector2f viewDelta);
		/** @brief Starts dragging the view at a point */
		void BeginDrag(Vector2f viewPos);
		/** @brief Drags the view, the level under the point where it started follows the pointer */
		void DragTo(Vector2f viewPos);
		/** @brief Ends dragging, the view keeps moving as fast as it was dragged for a while */
		void EndDrag();
		/** @brief Returns `true` if the view is being dragged */
		bool IsDragging() const {
			return _dragging;
		}
		/** @brief Flies the camera to a place, zooming out on the way if it's far */
		void FlyTo(Vector2f center, float scale);
		/** @brief Flies the camera so that a part of the level fills the view, or moves it there at once */
		void Fit(const Rectf& worldRect, bool animate);
		/** @brief Stops any movement */
		void Stop();
		/** @brief Returns `true` if the camera is moving by itself */
		bool IsMoving() const;

		/** @brief Advances the movement */
		void Update(float timeMult);

		/** @brief Returns the step of the zoom closest to a scale, by @p direction the next one up or down */
		float SnapScale(float scale, std::int32_t direction) const;

	private:
		Vector2i _viewSize;
		float _gameplayScale;
		Rectf _levelBounds;

		Vector2f _center;
		float _scale;

		// Zooming at a point
		bool _zooming;
		float _targetScale;
		float _zoomStepsLeft;
		Vector2f _zoomAnchorView;
		Vector2f _zoomAnchorWorld;

		// Dragging and the movement it leaves behind
		bool _dragging;
		Vector2f _dragAnchorWorld;
		Vector2f _dragLastView;
		Vector2f _velocity;
		float _dragFrames;

		// Flying
		bool _flying;
		float _flyTime;
		float _flyDuration;
		float _flyLength;
		Vector2f _flyFrom;
		Vector2f _flyDirection;
		float _flyDistance;
		float _flyWidth0;
		float _flyWidth1;
		float _flyR0;

		void ClampCenter();
		void ApplyZoomAnchor();
		float GetFlyWidth(float s) const;
		float GetFlyDistance(float s) const;
	};
}

#endif
