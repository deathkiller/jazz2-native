#include "Viewport.h"
#include "RenderQueue.h"
#include "RenderResources.h"
#include "../Application.h"
#include "../IAppEventHandler.h"
#include "DrawableNode.h"
#include "Camera.h"
#include "RHI/Rhi.h"
#include "Texture.h"
#include "../ServiceLocator.h"
#include "../tracy.h"
#include "../../Main.h"

#if defined(WITH_QT5)
#	include "Qt5GfxDevice.h"
#endif

namespace nCine
{
	SmallVector<Viewport*> Viewport::chain_;

	Viewport::Viewport(const char* name, Texture* texture, DepthStencilFormat depthStencilFormat)
		: type_(Type::NoTexture), width_(0), height_(0), viewportRect_(0, 0, 0, 0), scissorRect_(0, 0, 0, 0),
			depthStencilFormat_(DepthStencilFormat::None), lastFrameCleared_(0), clearMode_(ClearMode::EveryFrame),
			clearColor_(Colorf::Black), fbo_(nullptr), rootNode_(nullptr),
			camera_(nullptr), stateBits_(0), numColorAttachments_(0)
	{
		for (std::uint32_t i = 0; i < MaxNumTextures; i++) {
			textures_[i] = nullptr;
		}

		if (texture != nullptr) {
			const bool texAdded = SetTexture(texture);
			if (texAdded) {
				fbo_->SetObjectLabel(name);
				if (depthStencilFormat != DepthStencilFormat::None) {
					const bool depthStencilAdded = SetDepthStencilFormat(depthStencilFormat);
					if (!depthStencilAdded) {
						SetTexture(nullptr);
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
		: Viewport(name, texture, DepthStencilFormat::None)
	{
	}

	Viewport::Viewport(Texture* texture)
		: Viewport(nullptr, texture, DepthStencilFormat::None)
	{
	}

	Viewport::Viewport()
		: Viewport(nullptr, nullptr, DepthStencilFormat::None)
	{
	}

	Viewport::~Viewport() = default;

	/** @note Adding more textures enables the use of multiple render targets (MRTs) */
	bool Viewport::SetTexture(std::uint32_t index, Texture* texture)
	{
		if (type_ == Type::Screen) {
			return false;
		}

		if (type_ != Type::NoTexture) {
			static const std::int32_t MaxColorAttachments = theServiceLocator().GetGfxCapabilities().GetValue(IGfxCapabilities::IntValues::MAX_COLOR_ATTACHMENTS);
			const bool indexOutOfRange = (index >= std::uint32_t(MaxColorAttachments) || index >= MaxNumTextures);
			const bool widthDiffers = texture != nullptr && (width_ > 0 && texture->GetWidth() != width_);
			const bool heightDiffers = texture != nullptr && (height_ > 0 && texture->GetHeight() != height_);
			if (indexOutOfRange || textures_[index] == texture || widthDiffers || heightDiffers)
				return false;
		}

		bool result = false;
		if (texture != nullptr) {
			// Adding a new texture
			if (fbo_ == nullptr) {
				fbo_ = std::make_unique<Rhi::RenderTarget>();
			}

			fbo_->AttachColorTexture(*texture->rhiTexture_, index);
			const bool isStatusComplete = fbo_->IsStatusComplete();
			if (isStatusComplete) {
				type_ = Type::WithTexture;
				textures_[index] = texture;
				numColorAttachments_++;

				if (width_ == 0 || height_ == 0) {
					width_ = texture->GetWidth();
					height_ = texture->GetHeight();
					viewportRect_.Set(0, 0, width_, height_);
				}
			}
			result = isStatusComplete;
		} else {
			// Remove an existing texture
			if (fbo_ != nullptr) {
				fbo_->DetachColorTexture(index);
				textures_[index] = nullptr;
				numColorAttachments_--;

				if (numColorAttachments_ == 0) {
					// Removing the depth/stencil render target
					if (depthStencilFormat_ != DepthStencilFormat::None) {
						fbo_->DetachDepthStencil(depthStencilFormat_);
						depthStencilFormat_ = Viewport::DepthStencilFormat::None;
					}

					type_ = Type::NoTexture;
					width_ = 0;
					height_ = 0;
				}
			}
			result = true;
		}

		return result;
	}

	/** @note Specifying `DepthStencilFormat::None` removes the depth and stencil renderbuffer of the viewport's FBO */
	bool Viewport::SetDepthStencilFormat(DepthStencilFormat depthStencilFormat)
	{
		if (depthStencilFormat_ == depthStencilFormat || type_ == Type::NoTexture)
			return false;

		bool result = false;
		if (depthStencilFormat != Viewport::DepthStencilFormat::None) {
			// Adding a depth/stencil render target
			if (fbo_ == nullptr) {
				fbo_ = std::make_unique<Rhi::RenderTarget>();
			}
			if (depthStencilFormat_ != Viewport::DepthStencilFormat::None) {
				fbo_->DetachDepthStencil(depthStencilFormat_);
			}
			fbo_->AttachDepthStencil(depthStencilFormat, width_, height_);

			const bool isStatusComplete = fbo_->IsStatusComplete();
			if (isStatusComplete) {
				depthStencilFormat_ = depthStencilFormat;
			}
			result = isStatusComplete;
		} else {
			// Removing the depth/stencil render target
			if (fbo_ != nullptr) {
				fbo_->DetachDepthStencil(depthStencilFormat_);
				depthStencilFormat_ = Viewport::DepthStencilFormat::None;
			}

			result = true;
		}

		return result;
	}

	bool Viewport::RemoveAllTextures()
	{
		if (type_ == Type::Screen) {
			return false;
		}

		if (fbo_ != nullptr) {
			for (std::uint32_t i = 0; i < MaxNumTextures; i++) {
				if (textures_[i] != nullptr) {
					fbo_->DetachColorTexture(i);
					textures_[i] = nullptr;
				}
			}
			numColorAttachments_ = 0;

			if (depthStencilFormat_ != DepthStencilFormat::None) {
				fbo_->DetachDepthStencil(depthStencilFormat_);
				depthStencilFormat_ = DepthStencilFormat::None;
			}
		}

		type_ = Type::NoTexture;
		width_ = 0;
		height_ = 0;
		return true;
	}

	Texture* Viewport::GetTexture(std::uint32_t index)
	{
		DEATH_ASSERT(index < MaxNumTextures);

		Texture* texture = nullptr;
		if (index < MaxNumTextures) {
			texture = textures_[index];
		}

		return texture;
	}

	void Viewport::SetRenderTargetLabel(const char* label)
	{
		if (fbo_ != nullptr) {
			fbo_->SetObjectLabel(label);
		}
	}

	void Viewport::CalculateCullingRect()
	{
		ZoneScopedC(0x81A861);

		const std::int32_t width = (width_ != 0 ? width_ : viewportRect_.W);
		const std::int32_t height = (height_ != 0 ? height_ : viewportRect_.H);

		const Camera* vieportCamera = (camera_ != nullptr ? camera_ : RenderResources::GetCurrentCamera());
		Camera::ProjectionValues projValues = vieportCamera->GetProjectionValues();
		if (projValues.top > projValues.bottom) std::swap(projValues.top, projValues.bottom);

		const float projWidth = projValues.right - projValues.left;
		const float projHeight = projValues.bottom - projValues.top;
		cullingRect_.Set(projValues.left, projValues.top, projWidth, projHeight);

		const bool scissorRectNonZeroArea = (scissorRect_.W > 0 && scissorRect_.H > 0);
		if (scissorRectNonZeroArea) {
			Rectf scissorRectFloat(float(scissorRect_.X), float(scissorRect_.Y), float(scissorRect_.W), float(scissorRect_.H));

			const bool viewportRectNonZeroArea = (viewportRect_.W > 0 && viewportRect_.H > 0);
			if (viewportRectNonZeroArea) {
				scissorRectFloat.X -= viewportRect_.X;
				scissorRectFloat.Y -= viewportRect_.Y;

				const float viewportWidthRatio = width / float(viewportRect_.W);
				const float viewportHeightRatio = height / float(viewportRect_.H);
				scissorRectFloat.X *= viewportWidthRatio;
				scissorRectFloat.Y *= viewportHeightRatio;
				scissorRectFloat.W *= viewportWidthRatio;
				scissorRectFloat.H *= viewportHeightRatio;
			}

			scissorRectFloat.X = (scissorRectFloat.X * projWidth / float(width)) + projValues.left;
			scissorRectFloat.Y = (scissorRectFloat.Y * projHeight / float(height)) + projValues.top;
			scissorRectFloat.W *= projWidth / float(width);
			scissorRectFloat.H *= projHeight / float(height);

			cullingRect_.Intersect(scissorRectFloat);
		}

		const Camera::ViewValues viewValues = vieportCamera->GetViewValues();
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
			const float sinRot = sinf(-viewValues.rotation);
			const float cosRot = cosf(-viewValues.rotation);
			const float rotatedWidth = fabsf(cullingRect_.W * cosRot) + fabsf(cullingRect_.H * sinRot);
			const float rotatedHeight = fabsf(cullingRect_.W * sinRot) + fabsf(cullingRect_.H * cosRot);

			const Vector2f center = cullingRect_.Center();
			// Using the inverse rotation angle
			const float rotatedX = cosRot * (center.X) + sinRot * (center.Y);
			const float rotatedY = -sinRot * (center.X) + cosRot * (center.Y);

			cullingRect_ = Rectf::FromCenterSize(rotatedX, rotatedY, rotatedWidth, rotatedHeight);
		}
	}

	void Viewport::Update()
	{
		RenderResources::SetCurrentViewport(this);
		RenderResources::SetCurrentCamera(camera_);

		if (rootNode_ != nullptr) {
			ZoneScopedC(0x81A861);
			if (rootNode_->lastFrameUpdated() < theApplication().GetFrameCount()) {
				rootNode_->OnUpdate(theApplication().GetTimeMult());
			}
			// AABBs should update after nodes have been transformed
			UpdateCulling(rootNode_);
		}

		stateBits_.set(StateBitPositions::UpdatedBit);
	}

	void Viewport::Visit()
	{
		RenderResources::SetCurrentViewport(this);

		CalculateCullingRect();

		if (rootNode_ != nullptr) {
			ZoneScopedC(0x81A861);
			std::uint32_t visitOrderIndex = 0;
			rootNode_->OnVisit(renderQueue_, visitOrderIndex);
		}

		stateBits_.set(StateBitPositions::VisitedBit);
	}

	void Viewport::SortAndCommitQueue()
	{
		RenderResources::SetCurrentViewport(this);

		if (!renderQueue_.IsEmpty()) {
			ZoneScopedC(0x81A861);
			renderQueue_.SortAndCommit();
		}

		stateBits_.set(StateBitPositions::CommittedBit);
	}

	void Viewport::Draw(std::uint32_t nextIndex)
	{
		Viewport* nextViewport = (nextIndex < chain_.size()) ? chain_[nextIndex] : nullptr;
		FATAL_ASSERT(nextViewport == nullptr || nextViewport->type_ != Type::Screen);

		if (nextViewport && nextViewport->type_ == Type::WithTexture) {
			nextViewport->Draw(nextIndex + 1);
		}

		ZoneScopedC(0x81A861);
#if defined(DEATH_DEBUG)
		// TODO: Rhi::Debug
		/*char debugString[128];
		std::size_t length;
		if (type_ == Type::Screen) {
			length = formatInto(debugString, "Draw screen viewport (0x{:x})", std::uintptr_t(this));
		} else if (type_ == Type::WithTexture && textures_[0]->name() != nullptr) {
			length = formatInto(debugString, "Draw viewport \"{}\" (0x{:x})", textures_[0]->name(), std::uintptr_t(this));
		} else {
			length = formatInto(debugString, "Draw viewport (0x{:x})", std::uintptr_t(this));
		}
		Rhi::Debug::ScopedGroup scoped({ debugString, length });*/
#endif

		RenderResources::SetCurrentViewport(this);
		{
			ZoneScopedNC("OnDrawViewport", 0x81A861);
			theApplication().appEventHandler_->OnDrawViewport(*this);
			//LOGD("IAppEventHandler::OnDrawViewport() invoked with viewport 0x{:x}", std::uintptr_t(this));
		}

		if (type_ == Type::WithTexture) {
			fbo_->BindDraw();
			fbo_->SetDrawBuffers(numColorAttachments_);
		}

		if (type_ == Type::Screen || type_ == Type::WithTexture) {
			const unsigned long int numFrames = theApplication().GetFrameCount();
			if ((lastFrameCleared_ < numFrames && (clearMode_ == ClearMode::EveryFrame || clearMode_ == ClearMode::ThisFrameOnly)) ||
				 clearMode_ == ClearMode::EveryDraw) {
				const Colorf previousClearColor = Rhi::Device::GetClearColor();
				Rhi::Device::SetClearColor(clearColor_);

				switch (depthStencilFormat_) {
					default:
					case DepthStencilFormat::Depth24_Stencil8:
						Rhi::Device::Clear(ClearFlags::Color | ClearFlags::Depth | ClearFlags::Stencil);
						break;
					case DepthStencilFormat::Depth24:
					case DepthStencilFormat::Depth16:
						Rhi::Device::Clear(ClearFlags::Color | ClearFlags::Depth);
						break;
					case DepthStencilFormat::None:
						Rhi::Device::Clear(ClearFlags::Color);
						break;
				}
				lastFrameCleared_ = numFrames;

				Rhi::Device::SetClearColor(previousClearColor);
			}
		}

		// This allows for sub-viewports that only change the camera and the OpenGL viewport
		if (nextViewport && nextViewport->type_ == Type::NoTexture) {
			const bool viewportRectNonZeroArea = (nextViewport->viewportRect_.W > 0 && nextViewport->viewportRect_.H > 0);
			if (!viewportRectNonZeroArea)
				nextViewport->viewportRect_ = viewportRect_;
			nextViewport->clearMode_ = ClearMode::Never;

			nextViewport->Draw(nextIndex + 1);
		}

		RenderResources::SetCurrentCamera(camera_);
		RenderResources::UpdateCameraUniforms();

		if (!renderQueue_.IsEmpty()) {
			const bool viewportRectNonZeroArea = (viewportRect_.W > 0 && viewportRect_.H > 0);
			const Recti previousViewport = Rhi::Device::GetViewport();
			if (viewportRectNonZeroArea) {
				Rhi::Device::SetViewport(viewportRect_);
			}

			const bool scissorRectNonZeroArea = (scissorRect_.W > 0 && scissorRect_.H > 0);
			Rhi::Device::ScissorState scissorState = Rhi::Device::GetScissorState();
			if (scissorRectNonZeroArea) {
				Rhi::Device::SetScissor(scissorRect_);
			}

			renderQueue_.Draw();

			if (scissorRectNonZeroArea) {
				Rhi::Device::SetScissorState(scissorState);
			}
			if (viewportRectNonZeroArea) {
				Rhi::Device::SetViewport(previousViewport);
			}
		}

		if (type_ == Type::WithTexture && depthStencilFormat_ != DepthStencilFormat::None &&
			!theApplication().GetAppConfiguration().withGlDebugContext) {
			fbo_->InvalidateDepthStencil(depthStencilFormat_);
		}

		if (clearMode_ == ClearMode::ThisFrameOnly) {
			clearMode_ = ClearMode::Never;
		} else if (clearMode_ == ClearMode::NextFrameOnly) {
			clearMode_ = ClearMode::ThisFrameOnly;
		}

		if (type_ == Type::WithTexture) {
#if defined(WITH_QT5)
			Qt5GfxDevice& gfxDevice = static_cast<Qt5GfxDevice&>(theApplication().gfxDevice());
			gfxDevice.bindDefaultDrawFramebufferObject();
#else
			Rhi::RenderTarget::UnbindDraw();
#endif
		}
	}

	void Viewport::UpdateCulling(SceneNode* node)
	{
		for (SceneNode* child : node->children()) {
			UpdateCulling(child);
		}

		if (node->type() != Object::ObjectType::SceneNode &&
			node->type() != Object::ObjectType::ParticleSystem) {
			DrawableNode* drawable = static_cast<DrawableNode*>(node);
			drawable->updateCulling();
		}
	}
}
