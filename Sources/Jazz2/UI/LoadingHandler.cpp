#include "LoadingHandler.h"

#include "../../nCine/Application.h"

namespace Jazz2::UI
{
	LoadingHandler::LoadingHandler(IRootController* root, bool darkMode)
		: _root(root), _transition(0.0f), _darkMode(darkMode)
	{
		_canvasBackground = std::make_unique<BackgroundCanvas>(this);

		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/Loading"_s);
		DEATH_ASSERT(_metadata != nullptr, "Cannot load required metadata", );
	}

	LoadingHandler::LoadingHandler(IRootController* root, bool darkMode, Function<bool(IRootController*)>&& callback)
		: LoadingHandler(root, darkMode)
	{
		_callback = std::move(callback);
	}

	LoadingHandler::~LoadingHandler()
	{
		_canvasBackground->setParent(nullptr);
	}

	Vector2i LoadingHandler::GetViewSize() const
	{
		return _upscalePass.GetViewSize();
	}

	void LoadingHandler::OnBeginFrame()
	{
		float timeMult = theApplication().GetTimeMult();

		if (_callback && _callback(_root)) {
			_callback = nullptr;
		}

		if (_transition < 1.0f) {
			_transition = std::min(_transition + timeMult * 0.04f, 1.0f);
		}
	}

	void LoadingHandler::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		Vector2i viewSize = Rendering::UpscaleRenderPass::CalculateViewSize(width, height, DefaultWidth, DefaultHeight);
		std::int32_t w = viewSize.X;
		std::int32_t h = viewSize.Y;

		_upscalePass.Initialize(w, h, width, height);

		// Viewports must be registered in reverse order
		_upscalePass.Register();

		_canvasBackground->setParent(_upscalePass.GetNode());
	}

	bool LoadingHandler::BackgroundCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_upscalePass.GetViewSize();

		DrawSolid(Vector2f::Zero, 950, Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y)), _owner->_darkMode ? Colorf::Black : Colorf::White);

		auto* loadingRes = _owner->_metadata->FindAnimation(AnimState::Idle);
		if (loadingRes != nullptr) {
			std::int32_t frame = loadingRes->GetFrameForTime(AnimTime);

			GenericGraphicResource* base = loadingRes->Base;
			Vector2f size = Vector2f(base->FrameDimensions.X, base->FrameDimensions.Y);
			Vector2f pos = Vector2f(ViewSize.X - size.X - 50.0f, ViewSize.Y - size.Y - 40.0f);

			Vector2i texSize = base->TextureDiffuse->GetSize();
			Recti frameRect = base->GetFrameRect(frame);
			// A trimmed frame covers less than its cell, so it shifts into place instead of being stretched
			Vector2i frameOffset = base->GetFrameOffset(frame);
			size = Vector2f((float)frameRect.W, (float)frameRect.H);
			pos += Vector2f((float)frameOffset.X, (float)frameOffset.Y);
			Vector4f texCoords = Vector4f(
				float(frameRect.W) / float(texSize.X),
				float(frameRect.X) / float(texSize.X),
				float(frameRect.H) / float(texSize.Y),
				float(frameRect.Y) / float(texSize.Y)
			);

			Colorf color = Colorf(1.0f, 1.0f, 1.0f, (_owner->_darkMode ? 0.8f : 1.0f) * _owner->_transition);
			std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? loadingRes->PaletteOffset : -1);
			DrawTexture(*base->TextureDiffuse.get(), pos, 960, size, texCoords, color, false, 0.0f, paletteOffset);
		}

		return true;
	}
}