#include "Viewport.h"
#include "RenderQueue.h"
#include "RenderResources.h"
#include "../Application.h"
#include "../IAppEventHandler.h"
#include "DrawableNode.h"
#include "Camera.h"
#include "GL/GLFramebuffer.h"
#include "Texture.h"
#include "GL/GLClearColor.h"
#include "GL/GLViewport.h"
#include "GL/GLScissorTest.h"
#include "GL/GLDebug.h"
#include "../ServiceLocator.h"
#include "../tracy.h"
#include "../../Common.h"

#ifdef WITH_QT5
#include "Qt5GfxDevice.h"
#endif

namespace nCine
{
#if _DEBUG
	namespace
	{
		/// The string used to output OpenGL debug group information
		static char debugString[64];
	}
#endif

	GLenum depthStencilFormatToGLFormat(Viewport::DepthStencilFormat format)
	{
		switch (format) {
			case Viewport::DepthStencilFormat::DEPTH16:
				return GL_DEPTH_COMPONENT16;
			case Viewport::DepthStencilFormat::DEPTH24:
				return GL_DEPTH_COMPONENT24;
			case Viewport::DepthStencilFormat::DEPTH24_STENCIL8:
				return GL_DEPTH24_STENCIL8;
			default:
				return GL_DEPTH24_STENCIL8;
		}
	}

	GLenum depthStencilFormatToGLAttachment(Viewport::DepthStencilFormat format)
	{
		switch (format) {
			case Viewport::DepthStencilFormat::DEPTH16:
			case Viewport::DepthStencilFormat::DEPTH24:
				return GL_DEPTH_ATTACHMENT;
			case Viewport::DepthStencilFormat::DEPTH24_STENCIL8:
				return GL_DEPTH_STENCIL_ATTACHMENT;
			default:
				return GL_DEPTH_STENCIL_ATTACHMENT;
		}
	}

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	SmallVector<Viewport*> Viewport::chain_;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Viewport::Viewport(const char* name, Texture* texture, DepthStencilFormat depthStencilFormat)
		: type_(Type::NO_TEXTURE), width_(0), height_(0),
		viewportRect_(0, 0, 0, 0), scissorRect_(0, 0, 0, 0),
		depthStencilFormat_(DepthStencilFormat::NONE), lastFrameCleared_(0),
		clearMode_(ClearMode::EVERY_FRAME), clearColor_(Colorf::Black),
		renderQueue_(std::make_unique<RenderQueue>()),
		fbo_(nullptr), rootNode_(nullptr), camera_(nullptr),
		stateBits_(0), numColorAttachments_(0)
	{
		for (unsigned int i = 0; i < MaxNumTextures; i++)
			textures_[i] = nullptr;

		if (texture != nullptr) {
			const bool texAdded = setTexture(texture);
			if (texAdded) {
				fbo_->setObjectLabel(name);
				if (depthStencilFormat != DepthStencilFormat::NONE) {
					const bool depthStencilAdded = setDepthStencilFormat(depthStencilFormat);
					if (!depthStencilAdded) {
						setTexture(nullptr);
					}
				}
			}
		}
	}

	Viewport::Viewport(Texture* texture, DepthStencilFormat depthStencilFormat)
		: Viewport(nullptr, texture, depthStencilFormat)
	{
	}

	Viewport::Viewport(const char* name, Texture* texture)
		: Viewport(name, texture, DepthStencilFormat::NONE)
	{
	}

	Viewport::Viewport(Texture* texture)
		: Viewport(nullptr, texture, DepthStencilFormat::NONE)
	{
	}

	Viewport::Viewport()
		: Viewport(nullptr, nullptr, DepthStencilFormat::NONE)
	{
	}

	Viewport::~Viewport() = default;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	/*! \note Adding more textures enables the use of multiple render targets (MRTs) */
	bool Viewport::setTexture(unsigned int index, Texture* texture)
	{
		if (type_ == Type::SCREEN)
			return false;

		static const int MaxColorAttachments = theServiceLocator().gfxCapabilities().value(IGfxCapabilities::GLIntValues::MAX_COLOR_ATTACHMENTS);
		const bool indexOutOfRange = (index >= static_cast<unsigned int>(MaxColorAttachments) || index >= MaxNumTextures);
		const bool widthDiffers = texture != nullptr && (width_ > 0 && texture->width() != width_);
		const bool heightDiffers = texture != nullptr && (height_ > 0 && texture->height() != height_);
		if (indexOutOfRange || textures_[index] == texture || widthDiffers || heightDiffers)
			return false;

		bool result = false;
		if (texture != nullptr) {
			// Adding a new texture
			if (fbo_ == nullptr)
				fbo_ = std::make_unique<GLFramebuffer>();

			fbo_->attachTexture(*texture->glTexture_, GL_COLOR_ATTACHMENT0 + index);
			const bool isStatusComplete = fbo_->isStatusComplete();
			if (isStatusComplete) {
				type_ = Type::WITH_TEXTURE;
				textures_[index] = texture;
				numColorAttachments_++;

				if (width_ == 0 || height_ == 0) {
					width_ = texture->width();
					height_ = texture->height();
					viewportRect_.Set(0, 0, width_, height_);
				}
			}
			result = isStatusComplete;
		} else {
			// Remove an existing texture
			if (fbo_ != nullptr) {
				fbo_->detachTexture(GL_COLOR_ATTACHMENT0 + index);
				textures_[index] = nullptr;
				numColorAttachments_--;

				if (numColorAttachments_ == 0 && depthStencilFormat_ == DepthStencilFormat::NONE)
					type_ = Type::NO_TEXTURE;
			}
			result = true;
		}

		return result;
	}

	/*! \note It can remove the depth and stencil render buffer of the viewport's FBO by specifying `DepthStencilFormat::NONE` */
	bool Viewport::setDepthStencilFormat(DepthStencilFormat depthStencilFormat)
	{
		if (depthStencilFormat_ == depthStencilFormat || width_ == 0 || height_ == 0)
			return false;

		bool result = false;
		if (depthStencilFormat != Viewport::DepthStencilFormat::NONE) {
			// Adding a depth/stencil render target
			if (fbo_ == nullptr)
				fbo_ = std::make_unique<GLFramebuffer>();

			if (depthStencilFormat_ != Viewport::DepthStencilFormat::NONE)
				fbo_->detachRenderbuffer(depthStencilFormatToGLAttachment(depthStencilFormat_));
			fbo_->attachRenderbuffer(depthStencilFormatToGLFormat(depthStencilFormat), width_, height_, depthStencilFormatToGLAttachment(depthStencilFormat));

			const bool isStatusComplete = fbo_->isStatusComplete();
			if (isStatusComplete) {
				type_ = Type::WITH_TEXTURE;
				depthStencilFormat_ = depthStencilFormat;
			}
			result = isStatusComplete;
		} else {
			// Removing the depth/stencil render target
			if (fbo_ != nullptr) {
				fbo_->detachRenderbuffer(depthStencilFormatToGLAttachment(depthStencilFormat_));
				depthStencilFormat_ = Viewport::DepthStencilFormat::NONE;

				if (numColorAttachments_ == 0)
					type_ = Type::NO_TEXTURE;
			}

			result = true;
		}

		return result;
	}

	bool Viewport::removeAllTextures()
	{
		if (type_ == Type::SCREEN)
			return false;

		if (fbo_ != nullptr) {
			for (unsigned int i = 0; i < MaxNumTextures; i++) {
				if (textures_[i] != nullptr) {
					fbo_->detachTexture(GL_COLOR_ATTACHMENT0 + i);
					textures_[i] = nullptr;
				}
			}
			numColorAttachments_ = 0;

			if (depthStencilFormat_ != DepthStencilFormat::NONE) {
				fbo_->detachRenderbuffer(depthStencilFormatToGLAttachment(depthStencilFormat_));
				depthStencilFormat_ = DepthStencilFormat::NONE;
			}
		}

		type_ = Type::NO_TEXTURE;
		width_ = 0;
		height_ = 0;
		return true;
	}

	Texture* Viewport::texture(unsigned int index)
	{
		//ASSERT(index < MaxNumTextures);

		Texture* texture = nullptr;
		if (index < MaxNumTextures)
			texture = textures_[index];

		return texture;
	}

	void Viewport::setGLFramebufferLabel(const char* label)
	{
		if (fbo_) {
			fbo_->setObjectLabel(label);
		}
	}

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	void Viewport::calculateCullingRect()
	{
		ZoneScoped;

		const int width = (width_ != 0) ? width_ : viewportRect_.W;
		const int height = (height_ != 0) ? height_ : viewportRect_.H;

		const Camera* vieportCamera = camera_ ? camera_ : RenderResources::currentCamera();
		const Camera::ProjectionValues projValues = vieportCamera->projectionValues();
		const float projWidth = projValues.right - projValues.left;
		const float projHeight = projValues.bottom - projValues.top;
		cullingRect_.Set(projValues.left, projValues.top, projWidth, projHeight);

		const bool scissorRectNonZeroArea = (scissorRect_.W > 0 && scissorRect_.H > 0);
		if (scissorRectNonZeroArea) {
			Rectf scissorRectFloat(scissorRect_.X, scissorRect_.Y, scissorRect_.W, scissorRect_.H);

			const bool viewportRectNonZeroArea = (viewportRect_.W > 0 && viewportRect_.H > 0);
			if (viewportRectNonZeroArea) {
				scissorRectFloat.X -= viewportRect_.X;
				scissorRectFloat.Y -= viewportRect_.Y;

				const float viewportWidthRatio = width / static_cast<float>(viewportRect_.W);
				const float viewportHeightRatio = height / static_cast<float>(viewportRect_.H);
				scissorRectFloat.X *= viewportWidthRatio;
				scissorRectFloat.Y *= viewportHeightRatio;
				scissorRectFloat.W *= viewportWidthRatio;
				scissorRectFloat.H *= viewportHeightRatio;
			}

			scissorRectFloat.X = (scissorRectFloat.X * projWidth / static_cast<float>(width)) + projValues.left;
			scissorRectFloat.Y = (scissorRectFloat.Y * projHeight / static_cast<float>(height)) + projValues.top;
			scissorRectFloat.W *= projWidth / static_cast<float>(width);
			scissorRectFloat.H *= projHeight / static_cast<float>(height);

			cullingRect_.Intersect(scissorRectFloat);
		}

		const Camera::ViewValues viewValues = vieportCamera->viewValues();
		if (viewValues.scale != 0.0f && viewValues.scale != 1.0f) {
			const float invScale = 1.0f / viewValues.scale;
			cullingRect_.X = (cullingRect_.X + viewValues.position.X) * invScale;
			cullingRect_.Y = (cullingRect_.Y + viewValues.position.Y) * invScale;
			cullingRect_.W *= invScale;
			cullingRect_.H *= invScale;
		} else {
			cullingRect_.X += viewValues.position.X;
			cullingRect_.Y += viewValues.position.Y;
		}

		if (viewValues.rotation > SceneNode::MinRotation || viewValues.rotation < -SceneNode::MinRotation) {
			const float sinRot = sinf(-viewValues.rotation * fDegToRad);
			const float cosRot = cosf(-viewValues.rotation * fDegToRad);
			const float rotatedWidth = fabsf(cullingRect_.W * cosRot) + fabsf(cullingRect_.H * sinRot);
			const float rotatedHeight = fabsf(cullingRect_.W * sinRot) + fabsf(cullingRect_.H * cosRot);

			const Vector2f center = cullingRect_.Center();
			// Using the inverse rotation angle
			const float rotatedX = cosRot * (center.X) + sinRot * (center.Y);
			const float rotatedY = -sinRot * (center.X) + cosRot * (center.Y);

			cullingRect_ = Rectf::FromCenterSize(rotatedX, rotatedY, rotatedWidth, rotatedHeight);
		}
	}

	void Viewport::update()
	{
		RenderResources::setCurrentViewport(this);
		RenderResources::setCurrentCamera(camera_);

		calculateCullingRect();
		if (rootNode_) {
			ZoneScoped;
			if (rootNode_->lastFrameUpdated() < theApplication().numFrames())
				rootNode_->OnUpdate(theApplication().timeMult());
			// AABBs should update after nodes have been transformed
			updateCulling(rootNode_);
		}

		stateBits_.set(StateBitPositions::UpdatedBit);
	}

	void Viewport::visit()
	{
		RenderResources::setCurrentViewport(this);

		if (rootNode_) {
			ZoneScoped;
			unsigned int visitOrderIndex = 0;
			rootNode_->OnVisit(*renderQueue_, visitOrderIndex);
		}

		stateBits_.set(StateBitPositions::VisitedBit);
	}

	void Viewport::sortAndCommitQueue()
	{
		RenderResources::setCurrentViewport(this);

		if (!renderQueue_->empty()) {
			ZoneScoped;
			renderQueue_->sortAndCommit();
		}

		stateBits_.set(StateBitPositions::CommittedBit);
	}

	void Viewport::draw(unsigned int nextIndex)
	{
		Viewport* nextViewport = (nextIndex < chain_.size()) ? chain_[nextIndex] : nullptr;
		//FATAL_ASSERT(nextViewport == nullptr || nextViewport->type_ != Type::SCREEN);

		if (nextViewport && nextViewport->type_ == Type::WITH_TEXTURE)
			nextViewport->draw(nextIndex + 1);

		ZoneScoped;
#if _DEBUG
		// TODO
		/*if (type_ == Type::SCREEN)
			sprintf_s(debugString, "Draw screen viewport (0x%lx)", uintptr_t(this));
		else if (type_ == Type::WITH_TEXTURE && textures_[0]->name() != nullptr)
			sprintf_s(debugString, "Draw viewport \"%s\" (0x%lx)", textures_[0]->name(), uintptr_t(this));
		else
			sprintf_s(debugString, "Draw viewport (0x%lx)", uintptr_t(this));
		GLDebug::ScopedGroup(debugString);*/
#endif

		RenderResources::setCurrentViewport(this);
		{
			ZoneScopedN("onDrawViewport");
			theApplication().appEventHandler_->onDrawViewport(*this);
			//LOGV_X("IAppEventHandler::onDrawViewport() invoked with viewport 0x%lx", uintptr_t(this));
		}

		if (type_ == Type::WITH_TEXTURE) {
			fbo_->bind(GL_DRAW_FRAMEBUFFER);
			fbo_->drawBuffers(numColorAttachments_);
		}

		if (type_ == Type::SCREEN || type_ == Type::WITH_TEXTURE) {
			const unsigned long int numFrames = theApplication().numFrames();
			if ((lastFrameCleared_ < numFrames && (clearMode_ == ClearMode::EVERY_FRAME || clearMode_ == ClearMode::THIS_FRAME_ONLY)) ||
				clearMode_ == ClearMode::EVERY_DRAW) {
				const GLClearColor::State clearColorState = GLClearColor::state();
				GLClearColor::setColor(clearColor_);

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
				lastFrameCleared_ = numFrames;

				GLClearColor::setState(clearColorState);
			}
		}

		// This allows for sub-viewports that only change the camera and the OpenGL viewport
		if (nextViewport && nextViewport->type_ == Type::NO_TEXTURE) {
			const bool viewportRectNonZeroArea = (nextViewport->viewportRect_.W > 0 && nextViewport->viewportRect_.H > 0);
			if (viewportRectNonZeroArea == false)
				nextViewport->viewportRect_ = viewportRect_;
			nextViewport->clearMode_ = ClearMode::NEVER;

			nextViewport->draw(nextIndex + 1);
		}

		RenderResources::setCurrentCamera(camera_);
		RenderResources::updateCameraUniforms();

		if (!renderQueue_->empty()) {
			const bool viewportRectNonZeroArea = (viewportRect_.W > 0 && viewportRect_.H > 0);
			const GLViewport::State viewportState = GLViewport::state();
			if (viewportRectNonZeroArea)
				GLViewport::setRect(viewportRect_.X, viewportRect_.Y, viewportRect_.W, viewportRect_.H);

			const bool scissorRectNonZeroArea = (scissorRect_.W > 0 && scissorRect_.H > 0);
			GLScissorTest::State scissorTestState = GLScissorTest::state();
			if (scissorRectNonZeroArea)
				GLScissorTest::enable(scissorRect_.X, scissorRect_.Y, scissorRect_.W, scissorRect_.H);

			renderQueue_->draw();

			if (scissorRectNonZeroArea)
				GLScissorTest::setState(scissorTestState);
			if (viewportRectNonZeroArea)
				GLViewport::setState(viewportState);
		}

		if (type_ == Type::WITH_TEXTURE && depthStencilFormat_ != DepthStencilFormat::NONE &&
			theApplication().appConfiguration().withGlDebugContext == false) {
			const GLenum invalidAttachment = depthStencilFormatToGLAttachment(depthStencilFormat_);
			fbo_->invalidate(1, &invalidAttachment);
		}

		if (clearMode_ == ClearMode::THIS_FRAME_ONLY)
			clearMode_ = ClearMode::NEVER;
		else if (clearMode_ == ClearMode::NEXT_FRAME_ONLY)
			clearMode_ = ClearMode::THIS_FRAME_ONLY;

		if (type_ == Type::WITH_TEXTURE) {
#ifdef WITH_QT5
			Qt5GfxDevice& gfxDevice = static_cast<Qt5GfxDevice&>(theApplication().gfxDevice());
			gfxDevice.bindDefaultDrawFramebufferObject();
#else
			fbo_->unbind(GL_DRAW_FRAMEBUFFER);
#endif
		}
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void Viewport::updateCulling(SceneNode* node)
	{
		for (SceneNode* child : node->children())
			updateCulling(child);

		if (node->type() != Object::ObjectType::SCENENODE &&
			node->type() != Object::ObjectType::PARTICLE_SYSTEM) {
			DrawableNode* drawable = static_cast<DrawableNode*>(node);
			drawable->updateCulling();
		}
	}

}
