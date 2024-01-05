#include "Camera.h"
#include "../Application.h"

namespace nCine
{
	Camera::Camera()
		: viewValues_(0.0f, 0.0f, 0.0f, 1.0f), view_(Matrix4x4f::Identity), updateFrameProjectionMatrix_(0), updateFrameViewMatrix_(0)
	{
		int width = theApplication().width();
		int height = theApplication().height();

		projectionValues_.left = 0.0f;
		projectionValues_.right = width;
		projectionValues_.top = 0.0f;
		projectionValues_.bottom = height;

		projection_ = Matrix4x4f::Ortho(projectionValues_.left, projectionValues_.right,
										projectionValues_.bottom, projectionValues_.top,
										projectionValues_.nearClip, projectionValues_.farClip);
	}

	void Camera::setOrthoProjection(float left, float right, float top, float bottom)
	{
		projectionValues_.left = left;
		projectionValues_.right = right;
		projectionValues_.top = top;
		projectionValues_.bottom = bottom;

		projection_ = Matrix4x4f::Ortho(projectionValues_.left, projectionValues_.right,
										projectionValues_.bottom, projectionValues_.top,
										projectionValues_.nearClip, projectionValues_.farClip);
		updateFrameProjectionMatrix_ = theApplication().numFrames();
	}

	void Camera::setOrthoProjection(const ProjectionValues& values)
	{
		setOrthoProjection(values.left, values.right, values.top, values.bottom);
	}

	void Camera::setView(const Vector2f& position, float rotation, float scale)
	{
		viewValues_.position = position;
		viewValues_.rotation = rotation;
		viewValues_.scale = scale;

		view_ = Matrix4x4f::Translation(-position.X, -position.Y, 0.0f);
		view_.RotateZ(-rotation);
		view_.Scale(scale, scale, 1.0f);
		updateFrameViewMatrix_ = theApplication().numFrames();
	}

	void Camera::setView(float x, float y, float rotation, float scale)
	{
		setView(Vector2f(x, y), rotation, scale);
	}

	void Camera::setView(const ViewValues& values)
	{
		setView(values.position, values.rotation, values.scale);
	}
}
