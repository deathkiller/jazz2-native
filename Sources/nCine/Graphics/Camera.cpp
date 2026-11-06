#include "Camera.h"
#include "../Application.h"

namespace nCine
{
	Camera::Camera()
		: _viewValues(0.0f, 0.0f, 0.0f, 1.0f), _view(Matrix4x4f::Identity), _updateFrameProjectionMatrix(0), _updateFrameViewMatrix(0)
	{
		auto res = theApplication().GetResolution();

		_projectionValues.Left = 0.0f;
		_projectionValues.Right = res.X;
		_projectionValues.Top = 0.0f;
		_projectionValues.Bottom = res.Y;

		_projection = Matrix4x4f::Ortho(_projectionValues.Left, _projectionValues.Right,
										_projectionValues.Bottom, _projectionValues.Top,
										_projectionValues.NearClip, _projectionValues.FarClip);
	}

	void Camera::SetOrthoProjection(float left, float right, float top, float bottom)
	{
		_projectionValues.Left = left;
		_projectionValues.Right = right;
		_projectionValues.Top = top;
		_projectionValues.Bottom = bottom;

		_projection = Matrix4x4f::Ortho(_projectionValues.Left, _projectionValues.Right,
										_projectionValues.Bottom, _projectionValues.Top,
										_projectionValues.NearClip, _projectionValues.FarClip);
		_updateFrameProjectionMatrix = theApplication().GetFrameCount();
	}

	void Camera::SetOrthoProjection(const ProjectionValues& values)
	{
		SetOrthoProjection(values.Left, values.Right, values.Top, values.Bottom);
	}

	void Camera::SetView(Vector2f position, float rotation, float scale)
	{
		_viewValues.Position = position;
		_viewValues.Rotation = rotation;
		_viewValues.Scale = scale;

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
		SetView(values.Position, values.Rotation, values.Scale);
	}
}
