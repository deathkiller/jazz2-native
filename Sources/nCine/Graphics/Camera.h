#pragma once

#include "../Primitives/Matrix4x4.h"
#include "../Primitives/Rect.h"

namespace nCine
{
	/**
		@brief Provides the projection and view matrices used by shaders
		
		Holds an orthographic projection and a 2D view transform (position, rotation, scale) together with the
		matrices they produce. A viewport uses the camera matrices when rendering its scene; the frame in which
		each matrix last changed is tracked so dependent uniforms can be refreshed only when needed.
	*/
	class Camera
	{
	public:
		/**
		 * @brief Values describing the orthographic projection matrix
		 */
		struct ProjectionValues
		{
			float Left;
			float Right;
			float Top;
			float Bottom;
			float NearClip;
			float FarClip;

			ProjectionValues()
				: Left(0.0f), Right(0.0f), Top(0.0f), Bottom(0.0f), NearClip(-1.0f), FarClip(1.0f) {}

			ProjectionValues(float ll, float rr, float tt, float bb)
				: Left(ll), Right(rr), Top(tt), Bottom(bb), NearClip(-1.0f), FarClip(1.0f) {}
		};

		/**
		 * @brief Values describing the 2D view matrix
		 */
		struct ViewValues
		{
			Vector2f Position;
			float Rotation;
			float Scale;

			ViewValues()
				: Position(0.0f, 0.0f), Rotation(0.0f), Scale(1.0f) {}

			ViewValues(float xx, float yy, float rr, float ss)
				: Position(xx, yy), Rotation(rr), Scale(ss) {}
		};

		/** @brief Creates a camera with default (identity-like) matrices */
		Camera();

		inline const ProjectionValues& GetProjectionValues() const {
			return _projectionValues;
		}
		inline const ViewValues& GetViewValues() const {
			return _viewValues;
		}

		inline const Matrix4x4f& GetProjection() const {
			return _projection;
		}
		inline const Matrix4x4f& GetView() const {
			return _view;
		}

		void SetOrthoProjection(float left, float right, float top, float bottom);
		void SetOrthoProjection(const ProjectionValues& values);

		void SetView(Vector2f pos, float rotation, float scale);
		void SetView(float x, float y, float rotation, float scale);
		void SetView(const ViewValues& values);

		inline std::uint32_t UpdateFrameProjectionMatrix() const {
			return _updateFrameProjectionMatrix;
		}
		inline std::uint32_t UpdateFrameViewMatrix() const {
			return _updateFrameViewMatrix;
		}

	private:
		ProjectionValues _projectionValues;
		ViewValues _viewValues;
		Matrix4x4f _projection;
		Matrix4x4f _view;
		/** @brief Last frame in which the projection matrix was changed */
		std::uint32_t _updateFrameProjectionMatrix;
		/** @brief Last frame in which the view matrix was changed */
		std::uint32_t _updateFrameViewMatrix;
	};

}
