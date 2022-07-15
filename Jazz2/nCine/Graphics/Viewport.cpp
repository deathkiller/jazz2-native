#include "Viewport.h"
#include "RenderQueue.h"
#include "RenderResources.h"
#include "../Application.h"
#include "SceneNode.h"
#include "Camera.h"
#include "GL/GLFramebuffer.h"
#include "Texture.h"
#include "GL/GLClearColor.h"
#include "GL/GLViewport.h"
#include "GL/GLScissorTest.h"
#include "GL/GLDebug.h"

#ifdef WITH_QT5
#include "Qt5GfxDevice.h"
#endif

namespace nCine
{
	namespace {
		/// The string used to output OpenGL debug group information
		static char debugString[64];
	}

	Texture::Format colorFormatToTexFormat(Viewport::ColorFormat format)
	{
		switch (format) {
			case Viewport::ColorFormat::RGB8:
				return Texture::Format::RGB8;
			case Viewport::ColorFormat::RGBA8:
				return Texture::Format::RGBA8;

			default:
				return Texture::Format::Unknown;
		}
	}

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
				return GL_DEPTH_ATTACHMENT;
			case Viewport::DepthStencilFormat::DEPTH24:
				return GL_DEPTH_ATTACHMENT;
			case Viewport::DepthStencilFormat::DEPTH24_STENCIL8:
				return GL_DEPTH_STENCIL_ATTACHMENT;
			default:
				return GL_DEPTH_STENCIL_ATTACHMENT;
		}
	}

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Viewport::Viewport()
		: type_(Type::NO_TEXTURE), width_(0), height_(0),
		viewportRect_(0, 0, 0, 0), scissorRect_(0, 0, 0, 0),
		colorFormat_(ColorFormat::RGB8), depthStencilFormat_(DepthStencilFormat::NONE),
		clearMode_(ClearMode::EVERY_FRAME), clearColor_(Colorf::Black),
		renderQueue_(std::make_unique<RenderQueue>(*this)),
		fbo_(nullptr), texture_(nullptr),
		rootNode_(nullptr), camera_(nullptr), nextViewport_(nullptr)
	{
	}

	Viewport::Viewport(int width, int height, ColorFormat colorFormat, DepthStencilFormat depthStencilFormat)
		: type_(Type::REGULAR), width_(0), height_(0),
		viewportRect_(0, 0, width, height), scissorRect_(0, 0, 0, 0),
		colorFormat_(colorFormat), depthStencilFormat_(depthStencilFormat),
		clearMode_(ClearMode::EVERY_FRAME), clearColor_(Colorf::Black),
		renderQueue_(std::make_unique<RenderQueue>(*this)),
		fbo_(nullptr), texture_(std::make_unique<Texture>()),
		rootNode_(nullptr), camera_(nullptr), nextViewport_(nullptr)
	{
		const bool isInitialized = initTexture(width, height, colorFormat, depthStencilFormat);
		//ASSERT(isInitialized);
	}

	Viewport::Viewport(const Vector2i& size, ColorFormat colorFormat, DepthStencilFormat depthStencilFormat)
		: Viewport(size.X, size.Y, colorFormat, depthStencilFormat)
	{
	}

	Viewport::Viewport(int width, int height)
		: Viewport(width, height, ColorFormat::RGB8, DepthStencilFormat::DEPTH24_STENCIL8)
	{
	}

	Viewport::Viewport(const Vector2i& size)
		: Viewport(size.X, size.Y, ColorFormat::RGB8, DepthStencilFormat::DEPTH24_STENCIL8)
	{
	}

	Viewport::~Viewport() = default;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool Viewport::initTexture(int width, int height, ColorFormat colorFormat, DepthStencilFormat depthStencilFormat)
	{
		//ASSERT(width > 0 && height > 0);

		if (type_ == Type::SCREEN)
			return false;

		if (type_ == Type::NO_TEXTURE) {
			viewportRect_.Set(0, 0, width, height);
			texture_ = std::make_unique<Texture>();
			type_ = Type::REGULAR;
		}
		texture_->init("Viewport", colorFormatToTexFormat(colorFormat), width, height);

		fbo_ = std::make_unique<GLFramebuffer>();
		fbo_->attachTexture(*texture_->glTexture_, GL_COLOR_ATTACHMENT0);
		if (depthStencilFormat != DepthStencilFormat::NONE)
			fbo_->attachRenderbuffer(depthStencilFormatToGLFormat(depthStencilFormat), width, height, depthStencilFormatToGLAttachment(depthStencilFormat));

		const bool isStatusComplete = fbo_->isStatusComplete();
		if (isStatusComplete) {
			width_ = width;
			height_ = height;
			colorFormat_ = colorFormat;
			depthStencilFormat_ = depthStencilFormat;
		}

		return isStatusComplete;
	}

	bool Viewport::initTexture(const Vector2i& size, ColorFormat colorFormat, DepthStencilFormat depthStencilFormat)
	{
		return initTexture(size.X, size.Y, colorFormat, depthStencilFormat);
	}

	bool Viewport::initTexture(int width, int height)
	{
		return initTexture(width, height, colorFormat_, depthStencilFormat_);
	}

	bool Viewport::initTexture(const Vector2i& size)
	{
		return initTexture(size.X, size.Y, colorFormat_, depthStencilFormat_);
	}

	void Viewport::resize(int width, int height)
	{
		if (width == width_ && height == height_)
			return;

		if (type_ == Type::REGULAR)
			initTexture(width, height);

		viewportRect_.Set(0, 0, width, height);

		if (camera_ != nullptr) {
			//camera_->setOrthoProjection(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
			camera_->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (+0.5f), height * (-0.5f));
		} else if (type_ == Type::SCREEN) {
			//RenderResources::defaultCamera_->setOrthoProjection(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
			RenderResources::defaultCamera_->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (+0.5f), height * (-0.5f));
		}

		width_ = width;
		height_ = height;
	}

	void Viewport::setNextViewport(Viewport* nextViewport)
	{
		if (nextViewport != nullptr) {
			//ASSERT(nextViewport->type_ != Type::SCREEN);
			if (nextViewport->type_ == Type::SCREEN) {
				return;
			}

			if (nextViewport->type_ == Type::NO_TEXTURE) {
				nextViewport->width_ = width_;
				nextViewport->height_ = height_;
				nextViewport->viewportRect_ = viewportRect_;
				nextViewport->colorFormat_ = colorFormat_;
				nextViewport->depthStencilFormat_ = depthStencilFormat_;
				nextViewport->clearMode_ = ClearMode::NEVER;
			}
		}

		nextViewport_ = nextViewport;
	}

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	void Viewport::calculateCullingRect()
	{
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

				const float viewportWidthRatio = width_ / static_cast<float>(viewportRect_.W);
				const float viewportHeightRatio = height_ / static_cast<float>(viewportRect_.H);
				scissorRectFloat.X *= viewportWidthRatio;
				scissorRectFloat.Y *= viewportHeightRatio;
				scissorRectFloat.W *= viewportWidthRatio;
				scissorRectFloat.H *= viewportHeightRatio;
			}

			scissorRectFloat.X = (scissorRectFloat.X * projWidth / static_cast<float>(width_)) + projValues.left;
			scissorRectFloat.Y = (scissorRectFloat.Y * projHeight / static_cast<float>(height_)) + projValues.top;
			scissorRectFloat.W *= projWidth / static_cast<float>(width_);
			scissorRectFloat.H *= projHeight / static_cast<float>(height_);

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
		if (nextViewport_) {
			nextViewport_->update();
		}

		if (rootNode_ && rootNode_->lastFrameUpdated() < theApplication().numFrames()) {
			rootNode_->OnUpdate(theApplication().timeMult());
		}
	}

	void Viewport::visit()
	{
		if (nextViewport_) {
			nextViewport_->visit();
		}

		if (rootNode_) {
			calculateCullingRect();
			unsigned int visitOrderIndex = 0;
			rootNode_->OnVisit(*renderQueue_, visitOrderIndex);
		}
	}

	void Viewport::sortAndCommitQueue()
	{
		if (nextViewport_) {
			nextViewport_->sortAndCommitQueue();
		}

		if (!renderQueue_->empty()) {
			renderQueue_->sortAndCommit();
		}
	}

	void Viewport::draw()
	{
		if (nextViewport_ && nextViewport_->type_ == Type::REGULAR) {
			nextViewport_->draw();
		}

#if _DEBUG
		// TODO
		/*if (type_ == Type::SCREEN)
			sprintf_s(debugString, "Draw root viewport (0x%lx)", uintptr_t(this));
		else if (type_ == Type::REGULAR && texture_->name() != nullptr)
			sprintf_s(debugString, "Draw viewport \"%s\" (0x%lx)", texture_->name(), uintptr_t(this));
		else
			sprintf_s(debugString, "Draw viewport (0x%lx)", uintptr_t(this));
		GLDebug::ScopedGroup(debugString);*/
#endif

		if (type_ == Type::REGULAR)
			fbo_->bind(GL_DRAW_FRAMEBUFFER);

		if (type_ == Type::SCREEN || type_ == Type::REGULAR) {
			GLClearColor::State clearColorState = GLClearColor::state();
			GLClearColor::setColor(clearColor_);
			if (clearMode_ == ClearMode::EVERY_FRAME || clearMode_ == ClearMode::THIS_FRAME_ONLY)
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			GLClearColor::setState(clearColorState);
		}

		// This allows for sub-viewports that only change the camera and the OpenGL viewport
		if (nextViewport_ && nextViewport_->type_ == Type::NO_TEXTURE) {
			nextViewport_->clearMode_ = ClearMode::NEVER;
			nextViewport_->draw();
		}

		RenderResources::setCamera(camera_);

		if (!renderQueue_->empty()) {
			const bool viewportRectNonZeroArea = (viewportRect_.W > 0 && viewportRect_.H > 0);
			GLViewport::State viewportState = GLViewport::state();
			if (viewportRectNonZeroArea) {
				GLViewport::setRect(viewportRect_.X, viewportRect_.Y, viewportRect_.W, viewportRect_.H);
			}

			const bool scissorRectNonZeroArea = (scissorRect_.W > 0 && scissorRect_.H > 0);
			GLScissorTest::State scissorTestState = GLScissorTest::state();
			if (scissorRectNonZeroArea) {
				GLScissorTest::enable(scissorRect_.X, scissorRect_.Y, scissorRect_.W, scissorRect_.H);
			}

			renderQueue_->draw();

			if (scissorRectNonZeroArea)
				GLScissorTest::setState(scissorTestState);
			if (viewportRectNonZeroArea)
				GLViewport::setState(viewportState);
		}

		if (type_ == Type::REGULAR && depthStencilFormat_ != DepthStencilFormat::NONE &&
			!theApplication().appConfiguration().withGlDebugContext) {
			const GLenum invalidAttachment = depthStencilFormatToGLAttachment(depthStencilFormat_);
			fbo_->invalidate(1, &invalidAttachment);
		}

		if (clearMode_ == ClearMode::THIS_FRAME_ONLY)
			clearMode_ = ClearMode::NEVER;
		else if (clearMode_ == ClearMode::NEXT_FRAME_ONLY)
			clearMode_ = ClearMode::THIS_FRAME_ONLY;

		if (type_ == Type::REGULAR) {
#ifdef WITH_QT5
			Qt5GfxDevice& gfxDevice = static_cast<Qt5GfxDevice&>(theApplication().gfxDevice());
			gfxDevice.bindDefaultDrawFramebufferObject();
#else
			fbo_->unbind(GL_DRAW_FRAMEBUFFER);
#endif
		}
	}

}
