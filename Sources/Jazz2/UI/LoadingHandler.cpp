#include "LoadingHandler.h"

#include "../../nCine/Application.h"

#include <cmath>

namespace Jazz2::UI
{
	LoadingHandler::LoadingHandler(IRootController* root, bool darkMode)
		: _root(root), _transition(0.0f), _progress(-1.0f), _progressTransition(0.0f), _darkMode(darkMode)
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

		// Only a conversion of the original game files reports anything, and it can be well under way by the
		// time this handler appears, so the bar starts at wherever the conversion has got to. It's also never
		// taken back down: the conversion stops reporting as soon as it's done, and the last value it reported
		// is the one that should stay on screen for the frames until the main menu takes over.
		float reported = _root->GetInitializationProgress();
		if (reported >= 0.0f) {
			if (_progress < 0.0f) {
				_progress = reported;
			} else if (reported > _progress) {
				// Whole assets are converted at a time, so what is reported arrives in jumps of uneven size
				_progress = std::min(_progress + (reported - _progress) * 0.12f * timeMult, reported);
			}
		}

		if (_progress >= 0.0f && _progressTransition < 1.0f) {
			_progressTransition = std::min(_progressTransition + timeMult * 0.04f, 1.0f);
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
			// The indicator's whole cell, which is also what the progress bar below is placed against: the
			// frames are trimmed to the pixels they ink, so a single frame's extent moves as it animates
			Vector2f cellSize = Vector2f(base->FrameDimensions.X, base->FrameDimensions.Y);
			Vector2f cellPos = Vector2f(ViewSize.X - cellSize.X - 50.0f, ViewSize.Y - cellSize.Y - 40.0f);

			Vector2i texSize = base->TextureDiffuse->GetSize();
			Recti frameRect = base->GetFrameRect(frame);
			// A trimmed frame covers less than its cell, so it shifts into place instead of being stretched
			Vector2i frameOffset = base->GetFrameOffset(frame);
			Vector2f size = Vector2f((float)frameRect.W, (float)frameRect.H);
			Vector2f pos = cellPos + Vector2f((float)frameOffset.X, (float)frameOffset.Y);
			Vector4f texCoords = Vector4f(
				float(frameRect.W) / float(texSize.X),
				float(frameRect.X) / float(texSize.X),
				float(frameRect.H) / float(texSize.Y),
				float(frameRect.Y) / float(texSize.Y)
			);

			Colorf color = Colorf(1.0f, 1.0f, 1.0f, (_owner->_darkMode ? 0.8f : 1.0f) * _owner->_transition);
			std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? loadingRes->PaletteOffset : -1);
			DrawTexture(*base->TextureDiffuse.get(), pos, 960, size, texCoords, color, false, 0.0f, paletteOffset);

			if (_owner->_progress >= 0.0f) {
				constexpr float BarWidth = 240.0f;
				constexpr float BarHeight = 3.0f;
				constexpr float BarSpacing = 18.0f;
				constexpr float BarMarginLeft = 20.0f;

				// Ends where the indicator's cell begins and is centered on it, so the two read as one element.
				// A view too narrow for the whole bar gets a shorter one instead of one running off the edge.
				float right = std::round(cellPos.X - BarSpacing);
				float left = std::max(right - BarWidth, BarMarginLeft);
				float top = std::round(cellPos.Y + (cellSize.Y - BarHeight) * 0.5f);
				float width = right - left;

				if (width > 0.0f) {
					float alpha = _owner->_progressTransition;

					// The track is barely visible on purpose --- it's only there to show how much is still left
					DrawSolid(Vector2f(left, top), 955, Vector2f(width, BarHeight), _owner->_darkMode
						? Colorf(1.0f, 1.0f, 1.0f, 0.2f * alpha)
						: Colorf(0.0f, 0.0f, 0.0f, 0.12f * alpha));
					DrawSolid(Vector2f(left, top), 956, Vector2f(std::round(width * _owner->_progress), BarHeight), _owner->_darkMode
						? Colorf(1.0f, 1.0f, 1.0f, 0.8f * alpha)
						: Colorf(0.0f, 0.0f, 0.0f, 0.5f * alpha));
				}
			}
		}

		return true;
	}
}