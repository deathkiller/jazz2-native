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
		GLenum toGlBlendingFactor(DrawableNode::BlendingFactor blendingFactor)
		{
			switch (blendingFactor) {
				case DrawableNode::BlendingFactor::ZERO: return GL_ZERO;
				case DrawableNode::BlendingFactor::ONE: return GL_ONE;
				case DrawableNode::BlendingFactor::SRC_COLOR: return GL_SRC_COLOR;
				case DrawableNode::BlendingFactor::ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
				case DrawableNode::BlendingFactor::DST_COLOR: return GL_DST_COLOR;
				case DrawableNode::BlendingFactor::ONE_MINUS_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
				case DrawableNode::BlendingFactor::SRC_ALPHA: return GL_SRC_ALPHA;
				case DrawableNode::BlendingFactor::ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
				case DrawableNode::BlendingFactor::DST_ALPHA: return GL_DST_ALPHA;
				case DrawableNode::BlendingFactor::ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
				case DrawableNode::BlendingFactor::CONSTANT_COLOR: return GL_CONSTANT_COLOR;
				case DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_COLOR: return GL_ONE_MINUS_CONSTANT_COLOR;
				case DrawableNode::BlendingFactor::CONSTANT_ALPHA: return GL_CONSTANT_ALPHA;
				case DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_ALPHA: return GL_ONE_MINUS_CONSTANT_ALPHA;
				case DrawableNode::BlendingFactor::SRC_ALPHA_SATURATE: return GL_SRC_ALPHA_SATURATE;
			}
			return GL_ZERO;
		}

		DrawableNode::BlendingFactor fromGlBlendingFactor(GLenum blendingFactor)
		{
			switch (blendingFactor) {
				case GL_ZERO: return DrawableNode::BlendingFactor::ZERO;
				case GL_ONE: return DrawableNode::BlendingFactor::ONE;
				case GL_SRC_COLOR: return DrawableNode::BlendingFactor::SRC_COLOR;
				case GL_ONE_MINUS_SRC_COLOR: return DrawableNode::BlendingFactor::ONE_MINUS_SRC_COLOR;
				case GL_DST_COLOR: return DrawableNode::BlendingFactor::DST_COLOR;
				case GL_ONE_MINUS_DST_COLOR: return DrawableNode::BlendingFactor::ONE_MINUS_DST_COLOR;
				case GL_SRC_ALPHA: return DrawableNode::BlendingFactor::SRC_ALPHA;
				case GL_ONE_MINUS_SRC_ALPHA: return DrawableNode::BlendingFactor::ONE_MINUS_SRC_ALPHA;
				case GL_DST_ALPHA: return DrawableNode::BlendingFactor::DST_ALPHA;
				case GL_ONE_MINUS_DST_ALPHA: return DrawableNode::BlendingFactor::ONE_MINUS_DST_ALPHA;
				case GL_CONSTANT_COLOR: return DrawableNode::BlendingFactor::CONSTANT_COLOR;
				case GL_ONE_MINUS_CONSTANT_COLOR: return DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_COLOR;
				case GL_CONSTANT_ALPHA: return DrawableNode::BlendingFactor::CONSTANT_ALPHA;
				case GL_ONE_MINUS_CONSTANT_ALPHA: return DrawableNode::BlendingFactor::ONE_MINUS_CONSTANT_ALPHA;
				case GL_SRC_ALPHA_SATURATE: return DrawableNode::BlendingFactor::SRC_ALPHA_SATURATE;
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
		renderCommand_.setIdSortKey(id());
	}

	DrawableNode::DrawableNode(SceneNode* parent, const Vector2f& position)
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
			const Viewport* viewport = RenderResources::currentViewport();
			overlaps = aabb_.Overlaps(viewport->cullingRect());
		}

		if (!cullingEnabled || overlaps) {
			renderCommand_.setLayer(absLayer_);
			renderCommand_.setVisitOrder(withVisitOrder_ ? visitOrderIndex_ : 0);
			updateRenderCommand();
			renderQueue.addCommand(&renderCommand_);
		} else {
#if defined(NCINE_PROFILING)
			RenderStatistics::addCulledNode();
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
		return renderCommand_.material().isBlendingEnabled();
	}

	void DrawableNode::setBlendingEnabled(bool blendingEnabled)
	{
		renderCommand_.material().setBlendingEnabled(blendingEnabled);
	}

	DrawableNode::BlendingFactor DrawableNode::srcBlendingFactor() const
	{
		return fromGlBlendingFactor(renderCommand_.material().srcBlendingFactor());
	}

	DrawableNode::BlendingFactor DrawableNode::destBlendingFactor() const
	{
		return fromGlBlendingFactor(renderCommand_.material().destBlendingFactor());
	}

	void DrawableNode::setBlendingPreset(BlendingPreset blendingPreset)
	{
		switch (blendingPreset) {
			case BlendingPreset::DISABLED:
				renderCommand_.material().setBlendingFactors(toGlBlendingFactor(BlendingFactor::ONE), toGlBlendingFactor(BlendingFactor::ZERO));
				break;
			case BlendingPreset::ALPHA:
				renderCommand_.material().setBlendingFactors(toGlBlendingFactor(BlendingFactor::SRC_ALPHA), toGlBlendingFactor(BlendingFactor::ONE_MINUS_SRC_ALPHA));
				break;
			case BlendingPreset::PREMULTIPLIED_ALPHA:
				renderCommand_.material().setBlendingFactors(toGlBlendingFactor(BlendingFactor::ONE), toGlBlendingFactor(BlendingFactor::ONE_MINUS_SRC_ALPHA));
				break;
			case BlendingPreset::ADDITIVE:
				renderCommand_.material().setBlendingFactors(toGlBlendingFactor(BlendingFactor::SRC_ALPHA), toGlBlendingFactor(BlendingFactor::ONE));
				break;
			case BlendingPreset::MULTIPLY:
				renderCommand_.material().setBlendingFactors(toGlBlendingFactor(BlendingFactor::DST_COLOR), toGlBlendingFactor(BlendingFactor::ZERO));
				break;
		}
	}

	void DrawableNode::setBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor)
	{
		renderCommand_.material().setBlendingFactors(toGlBlendingFactor(srcBlendingFactor), toGlBlendingFactor(destBlendingFactor));
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
				const Viewport* viewport = RenderResources::currentViewport();
				const bool overlaps = aabb_.Overlaps(viewport->cullingRect());
				if (overlaps)
					lastFrameRendered_ = theApplication().GetFrameCount();
			}
		}
	}

	DrawableNode::DrawableNode(const DrawableNode& other)
		: SceneNode(other), width_(other.width_), height_(other.height_), renderCommand_(), lastFrameRendered_(0)
	{
		renderCommand_.setIdSortKey(id());
		setBlendingEnabled(other.isBlendingEnabled());
		setBlendingFactors(other.srcBlendingFactor(), other.destBlendingFactor());
		setLayer(other.layer());
	}
}
