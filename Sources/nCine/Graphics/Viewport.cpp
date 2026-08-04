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
	SmallVector<Viewport*> Viewport::_chain;

	Viewport::Viewport(const char* name, Texture* texture, DepthStencilFormat depthStencilFormat)
		: _type(Type::NoTexture), _width(0), _height(0), _viewportRect(0, 0, 0, 0), _scissorRect(0, 0, 0, 0),
			_depthStencilFormat(DepthStencilFormat::None), _lastFrameCleared(0), _clearMode(ClearMode::EveryFrame),
			_clearColor(Colorf::Black), _fbo(nullptr), _rootNode(nullptr),
			_camera(nullptr), _stateBits(0), _numColorAttachments(0)
	{
		for (std::uint32_t i = 0; i < MaxNumTextures; i++) {
			_textures[i] = nullptr;
		}

		if (texture != nullptr) {
			const bool texAdded = SetTexture(texture);
			if (texAdded) {
				_fbo->SetObjectLabel(name);
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
		if (_type == Type::Screen) {
			return false;
		}

		if (_type != Type::NoTexture) {
			static const std::int32_t MaxColorAttachments = theServiceLocator().GetRhiCapabilities().GetValue(RHI::IRhiCapabilities::IntValues::MAX_COLOR_ATTACHMENTS);
			const bool indexOutOfRange = (index >= std::uint32_t(MaxColorAttachments) || index >= MaxNumTextures);
			const bool widthDiffers = texture != nullptr && (_width > 0 && texture->GetWidth() != _width);
			const bool heightDiffers = texture != nullptr && (_height > 0 && texture->GetHeight() != _height);
			if (indexOutOfRange || _textures[index] == texture || widthDiffers || heightDiffers)
				return false;
		}

		bool result = false;
		if (texture != nullptr) {
			// Adding a new texture
			if (_fbo == nullptr) {
				_fbo = std::make_unique<RHI::RenderTarget>();
			}

			_fbo->AttachColorTexture(*texture->_rhiTexture, index);
			const bool isStatusComplete = _fbo->IsStatusComplete();
			if (isStatusComplete) {
				_type = Type::WithTexture;
				_textures[index] = texture;
				_numColorAttachments++;

				if (_width == 0 || _height == 0) {
					_width = texture->GetWidth();
					_height = texture->GetHeight();
					_viewportRect.Set(0, 0, _width, _height);
				}
			}
			result = isStatusComplete;
		} else {
			// Remove an existing texture
			if (_fbo != nullptr) {
				_fbo->DetachColorTexture(index);
				_textures[index] = nullptr;
				_numColorAttachments--;

				if (_numColorAttachments == 0) {
					// Removing the depth/stencil render target
					if (_depthStencilFormat != DepthStencilFormat::None) {
						_fbo->DetachDepthStencil(_depthStencilFormat);
						_depthStencilFormat = Viewport::DepthStencilFormat::None;
					}

					_type = Type::NoTexture;
					_width = 0;
					_height = 0;
				}
			}
			result = true;
		}

		return result;
	}

	/** @note Specifying `DepthStencilFormat::None` removes the depth and stencil renderbuffer of the viewport's FBO */
	bool Viewport::SetDepthStencilFormat(DepthStencilFormat depthStencilFormat)
	{
		if (_depthStencilFormat == depthStencilFormat || _type == Type::NoTexture)
			return false;

		bool result = false;
		if (depthStencilFormat != Viewport::DepthStencilFormat::None) {
			// Adding a depth/stencil render target
			if (_fbo == nullptr) {
				_fbo = std::make_unique<RHI::RenderTarget>();
			}
			if (_depthStencilFormat != Viewport::DepthStencilFormat::None) {
				_fbo->DetachDepthStencil(_depthStencilFormat);
			}
			_fbo->AttachDepthStencil(depthStencilFormat, _width, _height);

			const bool isStatusComplete = _fbo->IsStatusComplete();
			if (isStatusComplete) {
				_depthStencilFormat = depthStencilFormat;
			}
			result = isStatusComplete;
		} else {
			// Removing the depth/stencil render target
			if (_fbo != nullptr) {
				_fbo->DetachDepthStencil(_depthStencilFormat);
				_depthStencilFormat = Viewport::DepthStencilFormat::None;
			}

			result = true;
		}

		return result;
	}

	bool Viewport::RemoveAllTextures()
	{
		if (_type == Type::Screen) {
			return false;
		}

		if (_fbo != nullptr) {
			for (std::uint32_t i = 0; i < MaxNumTextures; i++) {
				if (_textures[i] != nullptr) {
					_fbo->DetachColorTexture(i);
					_textures[i] = nullptr;
				}
			}
			_numColorAttachments = 0;

			if (_depthStencilFormat != DepthStencilFormat::None) {
				_fbo->DetachDepthStencil(_depthStencilFormat);
				_depthStencilFormat = DepthStencilFormat::None;
			}
		}

		_type = Type::NoTexture;
		_width = 0;
		_height = 0;
		return true;
	}

	Texture* Viewport::GetTexture(std::uint32_t index)
	{
		DEATH_ASSERT(index < MaxNumTextures);

		Texture* texture = nullptr;
		if (index < MaxNumTextures) {
			texture = _textures[index];
		}

		return texture;
	}

	void Viewport::SetRenderTargetLabel(const char* label)
	{
		if (_fbo != nullptr) {
			_fbo->SetObjectLabel(label);
		}
	}

	void Viewport::CalculateCullingRect()
	{
		ZoneScopedC(0x81A861);

		const std::int32_t width = (_width != 0 ? _width : _viewportRect.W);
		const std::int32_t height = (_height != 0 ? _height : _viewportRect.H);

		const Camera* vieportCamera = (_camera != nullptr ? _camera : RenderResources::GetCurrentCamera());
		Camera::ProjectionValues projValues = vieportCamera->GetProjectionValues();
		if (projValues.top > projValues.bottom) std::swap(projValues.top, projValues.bottom);

		const float projWidth = projValues.right - projValues.left;
		const float projHeight = projValues.bottom - projValues.top;
		_cullingRect.Set(projValues.left, projValues.top, projWidth, projHeight);

		const bool scissorRectNonZeroArea = (_scissorRect.W > 0 && _scissorRect.H > 0);
		if (scissorRectNonZeroArea) {
			Rectf scissorRectFloat(float(_scissorRect.X), float(_scissorRect.Y), float(_scissorRect.W), float(_scissorRect.H));

			const bool viewportRectNonZeroArea = (_viewportRect.W > 0 && _viewportRect.H > 0);
			if (viewportRectNonZeroArea) {
				scissorRectFloat.X -= _viewportRect.X;
				scissorRectFloat.Y -= _viewportRect.Y;

				const float viewportWidthRatio = width / float(_viewportRect.W);
				const float viewportHeightRatio = height / float(_viewportRect.H);
				scissorRectFloat.X *= viewportWidthRatio;
				scissorRectFloat.Y *= viewportHeightRatio;
				scissorRectFloat.W *= viewportWidthRatio;
				scissorRectFloat.H *= viewportHeightRatio;
			}

			scissorRectFloat.X = (scissorRectFloat.X * projWidth / float(width)) + projValues.left;
			scissorRectFloat.Y = (scissorRectFloat.Y * projHeight / float(height)) + projValues.top;
			scissorRectFloat.W *= projWidth / float(width);
			scissorRectFloat.H *= projHeight / float(height);

			_cullingRect.Intersect(scissorRectFloat);
		}

		const Camera::ViewValues viewValues = vieportCamera->GetViewValues();
		if (viewValues.scale != 0.0f && viewValues.scale != 1.0f) {
			const float invScale = 1.0f / viewValues.scale;
			_cullingRect.X = (_cullingRect.X + viewValues.position.X) * invScale;
			_cullingRect.Y = (_cullingRect.Y + viewValues.position.Y) * invScale;
			_cullingRect.W *= invScale;
			_cullingRect.H *= invScale;
		} else {
			_cullingRect.X += viewValues.position.X;
			_cullingRect.Y += viewValues.position.Y;
		}

		if (viewValues.rotation > SceneNode::MinRotation || viewValues.rotation < -SceneNode::MinRotation) {
			const float sinRot = sinf(-viewValues.rotation);
			const float cosRot = cosf(-viewValues.rotation);
			const float rotatedWidth = fabsf(_cullingRect.W * cosRot) + fabsf(_cullingRect.H * sinRot);
			const float rotatedHeight = fabsf(_cullingRect.W * sinRot) + fabsf(_cullingRect.H * cosRot);

			const Vector2f center = _cullingRect.Center();
			// Using the inverse rotation angle
			const float rotatedX = cosRot * (center.X) + sinRot * (center.Y);
			const float rotatedY = -sinRot * (center.X) + cosRot * (center.Y);

			_cullingRect = Rectf::FromCenterSize(rotatedX, rotatedY, rotatedWidth, rotatedHeight);
		}
	}

	void Viewport::Update()
	{
		RenderResources::SetCurrentViewport(this);
		RenderResources::SetCurrentCamera(_camera);

		if (_rootNode != nullptr) {
			ZoneScopedC(0x81A861);
			if (_rootNode->lastFrameUpdated() < theApplication().GetFrameCount()) {
				_rootNode->OnUpdate(theApplication().GetTimeMult());
			}
			// AABBs should update after nodes have been transformed
			UpdateCulling(_rootNode);
		}

		_stateBits.set(StateBitPositions::UpdatedBit);
	}

	void Viewport::Visit()
	{
		RenderResources::SetCurrentViewport(this);

		CalculateCullingRect();

		if (_rootNode != nullptr) {
			ZoneScopedC(0x81A861);
			std::uint32_t visitOrderIndex = 0;
			_rootNode->OnVisit(_renderQueue, visitOrderIndex);
		}

		_stateBits.set(StateBitPositions::VisitedBit);
	}

	void Viewport::SortAndCommitQueue()
	{
		RenderResources::SetCurrentViewport(this);

		if (!_renderQueue.IsEmpty()) {
			ZoneScopedC(0x81A861);
			_renderQueue.SortAndCommit();
		}

		_stateBits.set(StateBitPositions::CommittedBit);
	}

	void Viewport::Draw(std::uint32_t nextIndex)
	{
		Viewport* nextViewport = (nextIndex < _chain.size()) ? _chain[nextIndex] : nullptr;
		FATAL_ASSERT(nextViewport == nullptr || nextViewport->_type != Type::Screen);

		if (nextViewport && nextViewport->_type == Type::WithTexture) {
			nextViewport->Draw(nextIndex + 1);
		}

		ZoneScopedC(0x81A861);
#if defined(DEATH_DEBUG)
		// TODO: RHI::Debug
		/*char debugString[128];
		std::size_t length;
		if (_type == Type::Screen) {
			length = formatInto(debugString, "Draw screen viewport (0x{:x})", std::uintptr_t(this));
		} else if (_type == Type::WithTexture && _textures[0]->name() != nullptr) {
			length = formatInto(debugString, "Draw viewport \"{}\" (0x{:x})", _textures[0]->name(), std::uintptr_t(this));
		} else {
			length = formatInto(debugString, "Draw viewport (0x{:x})", std::uintptr_t(this));
		}
		RHI::Debug::ScopedGroup scoped({ debugString, length });*/
#endif

		RenderResources::SetCurrentViewport(this);
		{
			ZoneScopedNC("OnDrawViewport", 0x81A861);
			theApplication()._appEventHandler->OnDrawViewport(*this);
			//LOGD("IAppEventHandler::OnDrawViewport() invoked with viewport 0x{:x}", std::uintptr_t(this));
		}

		if (_type == Type::WithTexture) {
			_fbo->BindDraw();
			_fbo->SetDrawBuffers(_numColorAttachments);
		}

		if (_type == Type::Screen || _type == Type::WithTexture) {
			const unsigned long int numFrames = theApplication().GetFrameCount();
			if ((_lastFrameCleared < numFrames && (_clearMode == ClearMode::EveryFrame || _clearMode == ClearMode::ThisFrameOnly)) ||
				 _clearMode == ClearMode::EveryDraw) {
				const Colorf previousClearColor = RHI::Device::GetClearColor();
				RHI::Device::SetClearColor(_clearColor);

				switch (_depthStencilFormat) {
					default:
					case DepthStencilFormat::Depth24_Stencil8:
						RHI::Device::Clear(ClearFlags::Color | ClearFlags::Depth | ClearFlags::Stencil);
						break;
					case DepthStencilFormat::Depth24:
					case DepthStencilFormat::Depth16:
						RHI::Device::Clear(ClearFlags::Color | ClearFlags::Depth);
						break;
					case DepthStencilFormat::None:
						RHI::Device::Clear(ClearFlags::Color);
						break;
				}
				_lastFrameCleared = numFrames;

				RHI::Device::SetClearColor(previousClearColor);
			}
		}

		// This allows for sub-viewports that only change the camera and the OpenGL viewport
		if (nextViewport && nextViewport->_type == Type::NoTexture) {
			const bool viewportRectNonZeroArea = (nextViewport->_viewportRect.W > 0 && nextViewport->_viewportRect.H > 0);
			if (!viewportRectNonZeroArea)
				nextViewport->_viewportRect = _viewportRect;
			nextViewport->_clearMode = ClearMode::Never;

			nextViewport->Draw(nextIndex + 1);
		}

		RenderResources::SetCurrentCamera(_camera);
		RenderResources::UpdateCameraUniforms();

		if (!_renderQueue.IsEmpty()) {
			const bool viewportRectNonZeroArea = (_viewportRect.W > 0 && _viewportRect.H > 0);
			const Recti previousViewport = RHI::Device::GetViewport();
			if (viewportRectNonZeroArea) {
				RHI::Device::SetViewport(_viewportRect);
			}

			const bool scissorRectNonZeroArea = (_scissorRect.W > 0 && _scissorRect.H > 0);
			RHI::Device::ScissorState scissorState = RHI::Device::GetScissorState();
			if (scissorRectNonZeroArea) {
				RHI::Device::SetScissor(_scissorRect);
			}

			_renderQueue.Draw();

			if (scissorRectNonZeroArea) {
				RHI::Device::SetScissorState(scissorState);
			}
			if (viewportRectNonZeroArea) {
				RHI::Device::SetViewport(previousViewport);
			}
		}

		if (_type == Type::WithTexture && _depthStencilFormat != DepthStencilFormat::None &&
			!theApplication().GetAppConfiguration().withGlDebugContext) {
			_fbo->InvalidateDepthStencil(_depthStencilFormat);
		}

		if (_clearMode == ClearMode::ThisFrameOnly) {
			_clearMode = ClearMode::Never;
		} else if (_clearMode == ClearMode::NextFrameOnly) {
			_clearMode = ClearMode::ThisFrameOnly;
		}

		if (_type == Type::WithTexture) {
#if defined(WITH_QT5)
			Qt5GfxDevice& gfxDevice = static_cast<Qt5GfxDevice&>(theApplication().gfxDevice());
			gfxDevice.bindDefaultDrawFramebufferObject();
#else
			RHI::RenderTarget::UnbindDraw();
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
