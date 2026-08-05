#include "DrawableNode.h"
#include "RenderQueue.h"
#include "RenderResources.h"
#include "Viewport.h"
#include "../Application.h"
#include "RenderStatistics.h"
#include "../tracy.h"

namespace nCine
{
	const Vector2f DrawableNode::AnchorCenter(0.5f, 0.5f);
	const Vector2f DrawableNode::AnchorBottomLeft(0.0f, 0.0f);
	const Vector2f DrawableNode::AnchorTopLeft(0.0f, 1.0f);
	const Vector2f DrawableNode::AnchorBottomRight(1.0f, 0.0f);
	const Vector2f DrawableNode::AnchorTopRight(1.0f, 1.0f);

	DrawableNode::DrawableNode(SceneNode* parent, float xx, float yy)
		: SceneNode(parent, xx, yy), _width(0.0f), _height(0.0f),
		_renderCommand(),
		_lastFrameRendered(0)
	{
		_renderCommand.SetIdSortKey(id());
	}

	DrawableNode::DrawableNode(SceneNode* parent, Vector2f position)
		: DrawableNode(parent, position.X, position.Y)
	{
	}

	DrawableNode::DrawableNode(SceneNode* parent)
		: DrawableNode(parent, 0.0f, 0.0f)
	{
	}

	DrawableNode::DrawableNode()
		: DrawableNode(nullptr, 0.0f, 0.0f)
	{
	}

	DrawableNode::~DrawableNode() = default;

	//DrawableNode::DrawableNode(DrawableNode&&) = default;

	//DrawableNode& DrawableNode::operator=(DrawableNode&&) = default;

	bool DrawableNode::OnDraw(RenderQueue& renderQueue)
	{
		// Skip rendering a zero area drawable node
		if (_width == 0.0f || _height == 0.0f)
			return false;

		const bool cullingEnabled = theApplication().GetRenderingSettings().cullingEnabled;

		bool overlaps = false;
		if (cullingEnabled && _lastFrameRendered == theApplication().GetFrameCount()) {
			// This frame one of the viewports in the chain might overlap this node
			const Viewport* viewport = RenderResources::GetCurrentViewport();
			overlaps = _aabb.Overlaps(viewport->GetCullingRect());
		}

		if (!cullingEnabled || overlaps) {
			_renderCommand.SetLayer(_absLayer);
			_renderCommand.SetVisitOrder(_withVisitOrder ? _visitOrderIndex : 0);
			updateRenderCommand();
			_dirtyBits.reset(DirtyBitPositions::TransformationUploadBit);
			_dirtyBits.reset(DirtyBitPositions::ColorUploadBit);
			renderQueue.AddCommand(&_renderCommand);
		} else {
#if defined(NCINE_PROFILING)
			RenderStatistics::AddCulledNode();
#endif
			return false;
		}

		return true;
	}

	/**
	 * @note Sets the anchor point relative to the node width and height. To set the anchor point in
	 * pixels use `setAbsAnchorPoint()` instead.
	 */
	void DrawableNode::setAnchorPoint(float xx, float yy)
	{
		const float clampedX = std::clamp(xx, 0.0f, 1.0f);
		const float clampedY = std::clamp(yy, 0.0f, 1.0f);
		_anchorPoint.Set((clampedX - 0.5f) * width(), (clampedY - 0.5f) * height());
	}

	bool DrawableNode::isBlendingEnabled() const
	{
		return _renderCommand.GetMaterial().IsBlendingEnabled();
	}

	void DrawableNode::setBlendingEnabled(bool blendingEnabled)
	{
		_renderCommand.GetMaterial().SetBlendingEnabled(blendingEnabled);
	}

	BlendingFactor DrawableNode::srcBlendingFactor() const
	{
		return _renderCommand.GetMaterial().GetSrcBlendingFactor();
	}

	BlendingFactor DrawableNode::destBlendingFactor() const
	{
		return _renderCommand.GetMaterial().GetDestBlendingFactor();
	}

	void DrawableNode::setBlendingPreset(BlendingPreset blendingPreset)
	{
		switch (blendingPreset) {
			case BlendingPreset::Disabled:
				_renderCommand.GetMaterial().SetBlendingFactors(BlendingFactor::One, BlendingFactor::Zero);
				break;
			case BlendingPreset::Alpha:
				_renderCommand.GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
				break;
			case BlendingPreset::PremultipliedAlpha:
				_renderCommand.GetMaterial().SetBlendingFactors(BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
				break;
			case BlendingPreset::Additive:
				_renderCommand.GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
				break;
			case BlendingPreset::Multiply:
				_renderCommand.GetMaterial().SetBlendingFactors(BlendingFactor::DstColor, BlendingFactor::Zero);
				break;
		}
	}

	void DrawableNode::setBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor)
	{
		_renderCommand.GetMaterial().SetBlendingFactors(srcBlendingFactor, destBlendingFactor);
	}

	void DrawableNode::updateAabb()
	{
		//ZoneScopedC(0x81A861);

		const float width = absWidth();
		const float height = absHeight();

		if (_absRotation > MinRotation || _absRotation < -MinRotation) {
			// Calculate max size for any rotation angle, this will create larger bounding boxes but avoids using sin/cos
			const float maxSize = width + height;
			_aabb = Rectf(_absPosition.X - maxSize, _absPosition.Y - maxSize, maxSize * 2, maxSize * 2);
		} else {
			_aabb = Rectf(_absPosition.X, _absPosition.Y, width, height);
		}
	}

	void DrawableNode::updateCulling()
	{
		const bool cullingEnabled = theApplication().GetRenderingSettings().cullingEnabled;
		if (_drawEnabled && cullingEnabled && _width > 0 && _height > 0) {
			if (_dirtyBits.test(DirtyBitPositions::AabbBit)) {
				updateAabb();
				_dirtyBits.reset(DirtyBitPositions::AabbBit);
			}

			// Check if at least one viewport in the chain overlaps with this node
			if (_lastFrameRendered < theApplication().GetFrameCount()) {
				const Viewport* viewport = RenderResources::GetCurrentViewport();
				const bool overlaps = _aabb.Overlaps(viewport->GetCullingRect());
				if (overlaps)
					_lastFrameRendered = theApplication().GetFrameCount();
			}
		}
	}

	DrawableNode::DrawableNode(const DrawableNode& other)
		: SceneNode(other), _width(other._width), _height(other._height), _renderCommand(), _lastFrameRendered(0)
	{
		_renderCommand.SetIdSortKey(id());
		setBlendingEnabled(other.isBlendingEnabled());
		setBlendingFactors(other.srcBlendingFactor(), other.destBlendingFactor());
		setLayer(other.layer());
	}
}
