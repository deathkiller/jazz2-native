#include "DrawableNode.h"
#include "RenderQueue.h"
#include "RenderResources.h"
#include "Viewport.h"
#include "../Application.h"
#include "RenderStatistics.h"
#include "../tracy.h"

namespace nCine
{
	namespace
	{
		Rhi::BlendFactor toGapiBlendFactor(DrawableNode::BlendingFactor blendingFactor)
		{
			switch (blendingFactor) {
				case DrawableNode::BlendingFactor::ZERO:                  return Rhi::BlendFactor::Zero;
				case DrawableNode::BlendingFactor::ONE:                   return Rhi::BlendFactor::One;
				case DrawableNode::BlendingFactor::SRC_COLOR:             return Rhi::BlendFactor::SrcColor;
				case DrawableNode::BlendingFactor::ONE_MINUS_SRC_COLOR:   return Rhi::BlendFactor::OneMinusSrcColor;
				case DrawableNode::BlendingFactor::DST_COLOR:             return Rhi::BlendFactor::DstColor;
				case DrawableNode::BlendingFactor::ONE_MINUS_DST_COLOR:   return Rhi::BlendFactor::OneMinusDstColor;
				case DrawableNode::BlendingFactor::SRC_ALPHA:             return Rhi::BlendFactor::SrcAlpha;
				case DrawableNode::BlendingFactor::ONE_MINUS_SRC_ALPHA:   return Rhi::BlendFactor::OneMinusSrcAlpha;
				case DrawableNode::BlendingFactor::DST_ALPHA:             return Rhi::BlendFactor::DstAlpha;
				case DrawableNode::BlendingFactor::ONE_MINUS_DST_ALPHA:   return Rhi::BlendFactor::OneMinusDstAlpha;
				case DrawableNode::BlendingFactor::CONSTANT_COLOR:        return Rhi::BlendFactor::ConstantColor;
				case DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_COLOR: return Rhi::BlendFactor::OneMinusConstantColor;
				case DrawableNode::BlendingFactor::CONSTANT_ALPHA:        return Rhi::BlendFactor::ConstantAlpha;
				case DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_ALPHA: return Rhi::BlendFactor::OneMinusConstantAlpha;
				case DrawableNode::BlendingFactor::SRC_ALPHA_SATURATE:    return Rhi::BlendFactor::SrcAlphaSaturate;
			}
			return Rhi::BlendFactor::Zero;
		}

		DrawableNode::BlendingFactor fromGapiBlendFactor(Rhi::BlendFactor blendFactor)
		{
			switch (blendFactor) {
				case Rhi::BlendFactor::Zero:                  return DrawableNode::BlendingFactor::ZERO;
				case Rhi::BlendFactor::One:                   return DrawableNode::BlendingFactor::ONE;
				case Rhi::BlendFactor::SrcColor:              return DrawableNode::BlendingFactor::SRC_COLOR;
				case Rhi::BlendFactor::OneMinusSrcColor:      return DrawableNode::BlendingFactor::ONE_MINUS_SRC_COLOR;
				case Rhi::BlendFactor::DstColor:              return DrawableNode::BlendingFactor::DST_COLOR;
				case Rhi::BlendFactor::OneMinusDstColor:      return DrawableNode::BlendingFactor::ONE_MINUS_DST_COLOR;
				case Rhi::BlendFactor::SrcAlpha:              return DrawableNode::BlendingFactor::SRC_ALPHA;
				case Rhi::BlendFactor::OneMinusSrcAlpha:      return DrawableNode::BlendingFactor::ONE_MINUS_SRC_ALPHA;
				case Rhi::BlendFactor::DstAlpha:              return DrawableNode::BlendingFactor::DST_ALPHA;
				case Rhi::BlendFactor::OneMinusDstAlpha:      return DrawableNode::BlendingFactor::ONE_MINUS_DST_ALPHA;
				case Rhi::BlendFactor::ConstantColor:         return DrawableNode::BlendingFactor::CONSTANT_COLOR;
				case Rhi::BlendFactor::OneMinusConstantColor: return DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_COLOR;
				case Rhi::BlendFactor::ConstantAlpha:         return DrawableNode::BlendingFactor::CONSTANT_ALPHA;
				case Rhi::BlendFactor::OneMinusConstantAlpha: return DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_ALPHA;
				case Rhi::BlendFactor::SrcAlphaSaturate:      return DrawableNode::BlendingFactor::SRC_ALPHA_SATURATE;
			}
			return DrawableNode::BlendingFactor::ZERO;
		}
	}

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

	/*! \note This method sets the anchor point relative to the node width and height.
	 * To set the anchor point in pixels use `setAbsAnchorPoint()` instead. */
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

	DrawableNode::BlendingFactor DrawableNode::srcBlendingFactor() const
	{
		return fromGapiBlendFactor(renderCommand_.GetMaterial().GetSrcBlendingFactor());
	}

	DrawableNode::BlendingFactor DrawableNode::destBlendingFactor() const
	{
		return fromGapiBlendFactor(renderCommand_.GetMaterial().GetDestBlendingFactor());
	}

	void DrawableNode::setBlendingPreset(BlendingPreset blendingPreset)
	{
		switch (blendingPreset) {
			case BlendingPreset::DISABLED:
				renderCommand_.GetMaterial().SetBlendingFactors(toGapiBlendFactor(BlendingFactor::ONE), toGapiBlendFactor(BlendingFactor::ZERO));
				break;
			case BlendingPreset::ALPHA:
				renderCommand_.GetMaterial().SetBlendingFactors(toGapiBlendFactor(BlendingFactor::SRC_ALPHA), toGapiBlendFactor(BlendingFactor::ONE_MINUS_SRC_ALPHA));
				break;
			case BlendingPreset::PREMULTIPLIED_ALPHA:
				renderCommand_.GetMaterial().SetBlendingFactors(toGapiBlendFactor(BlendingFactor::ONE), toGapiBlendFactor(BlendingFactor::ONE_MINUS_SRC_ALPHA));
				break;
			case BlendingPreset::ADDITIVE:
				renderCommand_.GetMaterial().SetBlendingFactors(toGapiBlendFactor(BlendingFactor::SRC_ALPHA), toGapiBlendFactor(BlendingFactor::ONE));
				break;
			case BlendingPreset::MULTIPLY:
				renderCommand_.GetMaterial().SetBlendingFactors(toGapiBlendFactor(BlendingFactor::DST_COLOR), toGapiBlendFactor(BlendingFactor::ZERO));
				break;
		}
	}

	void DrawableNode::setBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor)
	{
		renderCommand_.GetMaterial().SetBlendingFactors(toGapiBlendFactor(srcBlendingFactor), toGapiBlendFactor(destBlendingFactor));
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
