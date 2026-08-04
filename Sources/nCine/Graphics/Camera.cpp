#include "Camera.h"
#include "../Application.h"

namespace nCine
{
	Camera::Camera()
		: _viewValues(0.0f, 0.0f, 0.0f, 1.0f), _view(Matrix4x4f::Identity), _updateFrameProjectionMatrix(0), _updateFrameViewMatrix(0)
	{
		auto res = theApplication().GetResolution();

		_projectionValues.left = 0.0f;
		_projectionValues.right = res.X;
		_projectionValues.top = 0.0f;
		_projectionValues.bottom = res.Y;

		_projection = Matrix4x4f::Ortho(_projectionValues.left, _projectionValues.right,
										_projectionValues.bottom, _projectionValues.top,
										_projectionValues.nearClip, _projectionValues.farClip);
	}

	void Camera::SetOrthoProjection(float left, float right, float top, float bottom)
	{
		_projectionValues.left = left;
		_projectionValues.right = right;
		_projectionValues.top = top;
		_projectionValues.bottom = bottom;

		_projection = Matrix4x4f::Ortho(_projectionValues.left, _projectionValues.right,
										_projectionValues.bottom, _projectionValues.top,
										_projectionValues.nearClip, _projectionValues.farClip);
		_updateFrameProjectionMatrix = theApplication().GetFrameCount();
	}

	void Camera::SetOrthoProjection(const ProjectionValues& values)
	{
		SetOrthoProjection(values.left, values.right, values.top, values.bottom);
	}

	void Camera::SetView(Vector2f position, float rotation, float scale)
	{
		_viewValues.position = position;
		_viewValues.rotation = rotation;
		_viewValues.scale = scale;

		_view = Matrix4x4f::Translation(-position.X, -position.Y, 0.0f);
		_view.RotateZ(-rotation);
		_view.Scale(scale, scale, 1.0f);
		_updateFrameViewMatrix = theApplication().GetFrameCount();
	}

	void Camera::SetView(float x, float y, float rotation, float scale)
	{
		SetView(Vector2f(x, y), rotation, scale);
	}

	void Camera::SetView(const ViewValues& values)
	{
		SetView(values.position, values.rotation, values.scale);
	}
}
