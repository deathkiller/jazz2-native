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
		ASSERT_MSG(_metadata != nullptr, "Cannot load required metadata");
	}

	LoadingHandler::LoadingHandler(IRootController* root, Function<bool(IRootController*)>&& callback, bool darkMode)
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
		constexpr float defaultRatio = (float)DefaultWidth / DefaultHeight;
		float currentRatio = (float)width / height;

		std::int32_t w, h;
		if (currentRatio > defaultRatio) {
			w = std::min(DefaultWidth, width);
			h = (std::int32_t)(w / currentRatio);
		} else if (currentRatio < defaultRatio) {
			h = std::min(DefaultHeight, height);
			w = (std::int32_t)(h * currentRatio);
		} else {
			w = std::min(DefaultWidth, width);
			h = std::min(DefaultHeight, height);
		}

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
			std::int32_t frame = loadingRes->FrameOffset + ((std::int32_t)(AnimTime * loadingRes->FrameCount / loadingRes->AnimDuration) % loadingRes->FrameCount);

			GenericGraphicResource* base = loadingRes->Base;
			Vector2f size = Vector2f(base->FrameDimensions.X, base->FrameDimensions.Y);
			Vector2f pos = Vector2f(ViewSize.X - size.X - 50.0f, ViewSize.Y - size.Y - 40.0f);

			Vector2i texSize = base->TextureDiffuse->size();
			std::int32_t col = frame % base->FrameConfiguration.X;
			std::int32_t row = frame / base->FrameConfiguration.X;
			Vector4f texCoords = Vector4f(
				float(base->FrameDimensions.X) / float(texSize.X),
				float(base->FrameDimensions.X * col) / float(texSize.X),
				float(base->FrameDimensions.Y) / float(texSize.Y),
				float(base->FrameDimensions.Y * row) / float(texSize.Y)
			);

			Colorf color = Colorf(1.0f, 1.0f, 1.0f, (_owner->_darkMode ? 0.8f : 1.0f) * _owner->_transition);
			DrawTexture(*base->TextureDiffuse.get(), pos, 960, size, texCoords, color, false);
		}

		return true;
	}
}