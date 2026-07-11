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
		: SceneNode(parent, xx, yy), width_(0.0f), height_(0.0f),
		renderCommand_(),
		lastFrameRendered_(0)
	{
		renderCommand_.SetIdSortKey(id());
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
		if (width_ == 0.0f || height_ == 0.0f)
			return false;

		const bool cullingEnabled = theApplication().GetRenderingSettings().cullingEnabled;

		bool overlaps = false;
		if (cullingEnabled && lastFrameRendered_ == theApplication().GetFrameCount()) {
			// This frame one of the viewports in the chain might overlap this node
			const Viewport* viewport = RenderResources::GetCurrentViewport();
			overlaps = aabb_.Overlaps(viewport->GetCullingRect());
		}

		if (!cullingEnabled || overlaps) {
			renderCommand_.SetLayer(absLayer_);
			renderCommand_.SetVisitOrder(withVisitOrder_ ? visitOrderIndex_ : 0);
			updateRenderCommand();
			dirtyBits_.reset(DirtyBitPositions::TransformationUploadBit);
			dirtyBits_.reset(DirtyBitPositions::ColorUploadBit);
			renderQueue.AddCommand(&renderCommand_);
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
		anchorPoint_.Set((clampedX - 0.5f) * width(), (clampedY - 0.5f) * height());
	}

	bool DrawableNode::isBlendingEnabled() const
	{
		return renderCommand_.GetMaterial().IsBlendingEnabled();
	}

	void DrawableNode::setBlendingEnabled(bool blendingEnabled)
	{
		renderCommand_.GetMaterial().SetBlendingEnabled(blendingEnabled);
	}

	BlendingFactor DrawableNode::srcBlendingFactor() const
	{
		return renderCommand_.GetMaterial().GetSrcBlendingFactor();
	}

	BlendingFactor DrawableNode::destBlendingFactor() const
	{
		return renderCommand_.GetMaterial().GetDestBlendingFactor();
	}

	void DrawableNode::setBlendingPreset(BlendingPreset blendingPreset)
	{
		switch (blendingPreset) {
			case BlendingPreset::DISABLED:
				renderCommand_.GetMaterial().SetBlendingFactors(BlendingFactor::One, BlendingFactor::Zero);
				break;
			case BlendingPreset::ALPHA:
				renderCommand_.GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
				break;
			case BlendingPreset::PREMULTIPLIED_ALPHA:
				renderCommand_.GetMaterial().SetBlendingFactors(BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
				break;
			case BlendingPreset::ADDITIVE:
				renderCommand_.GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
				break;
			case BlendingPreset::MULTIPLY:
				renderCommand_.GetMaterial().SetBlendingFactors(BlendingFactor::DstColor, BlendingFactor::Zero);
				break;
		}
	}

	void DrawableNode::setBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor)
	{
		renderCommand_.GetMaterial().SetBlendingFactors(srcBlendingFactor, destBlendingFactor);
	}

	void DrawableNode::updateAabb()
	{
		//ZoneScopedC(0x81A861);

		const float width = absWidth();
		const float height = absHeight();

		if (absRotation_ > MinRotation || absRotation_ < -MinRotation) {
			// Calculate max size for any rotation angle, this will create larger bounding boxes but avoids using sin/cos
			const float maxSize = width + height;
			aabb_ = Rectf(absPosition_.X - maxSize, absPosition_.Y - maxSize, maxSize * 2, maxSize * 2);
		} else {
			aabb_ = Rectf(absPosition_.X, absPosition_.Y, width, height);
		}
	}

	void DrawableNode::updateCulling()
	{
		const bool cullingEnabled = theApplication().GetRenderingSettings().cullingEnabled;
		if (drawEnabled_ && cullingEnabled && width_ > 0 && height_ > 0) {
			if (dirtyBits_.test(DirtyBitPositions::AabbBit)) {
				updateAabb();
				dirtyBits_.reset(DirtyBitPositions::AabbBit);
			}

			// Check if at least one viewport in the chain overlaps with this node
			if (lastFrameRendered_ < theApplication().GetFrameCount()) {
				const Viewport* viewport = RenderResources::GetCurrentViewport();
				const bool overlaps = aabb_.Overlaps(viewport->GetCullingRect());
				if (overlaps)
					lastFrameRendered_ = theApplication().GetFrameCount();
			}
		}
	}

	DrawableNode::DrawableNode(const DrawableNode& other)
		: SceneNode(other), width_(other.width_), height_(other.height_), renderCommand_(), lastFrameRendered_(0)
	{
		renderCommand_.SetIdSortKey(id());
		setBlendingEnabled(other.isBlendingEnabled());
		setBlendingFactors(other.srcBlendingFactor(), other.destBlendingFactor());
		setLayer(other.layer());
	}
}
