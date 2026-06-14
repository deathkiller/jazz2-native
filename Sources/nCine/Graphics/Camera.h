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
			float left;
			float right;
			float top;
			float bottom;
			float nearClip;
			float farClip;

			ProjectionValues()
				: left(0.0f), right(0.0f), top(0.0f), bottom(0.0f), nearClip(-1.0f), farClip(1.0f) {}

			ProjectionValues(float ll, float rr, float tt, float bb)
				: left(ll), right(rr), top(tt), bottom(bb), nearClip(-1.0f), farClip(1.0f) {}
		};

		/**
		 * @brief Values describing the 2D view matrix
		 */
		struct ViewValues
		{
			Vector2f position;
			float rotation;
			float scale;

			ViewValues()
				: position(0.0f, 0.0f), rotation(0.0f), scale(1.0f) {}

			ViewValues(float xx, float yy, float rr, float ss)
				: position(xx, yy), rotation(rr), scale(ss) {}
		};

		/** @brief Creates a camera with default (identity-like) matrices */
		Camera();

		inline const ProjectionValues& GetProjectionValues() const {
			return projectionValues_;
		}
		inline const ViewValues& GetViewValues() const {
			return viewValues_;
		}

		inline const Matrix4x4f& GetProjection() const {
			return projection_;
		}
		inline const Matrix4x4f& GetView() const {
			return view_;
		}

		void SetOrthoProjection(float left, float right, float top, float bottom);
		void SetOrthoProjection(const ProjectionValues& values);

		void SetView(Vector2f pos, float rotation, float scale);
		void SetView(float x, float y, float rotation, float scale);
		void SetView(const ViewValues& values);

		inline std::uint32_t UpdateFrameProjectionMatrix() const {
			return updateFrameProjectionMatrix_;
		}
		inline std::uint32_t UpdateFrameViewMatrix() const {
			return updateFrameViewMatrix_;
		}

	private:
		ProjectionValues projectionValues_;
		ViewValues viewValues_;
		Matrix4x4f projection_;
		Matrix4x4f view_;
		/** @brief Last frame in which the projection matrix was changed */
		std::uint32_t updateFrameProjectionMatrix_;
		/** @brief Last frame in which the view matrix was changed */
		std::uint32_t updateFrameViewMatrix_;
	};

}
