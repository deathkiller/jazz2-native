#include "DetachedWindow.h"

#if defined(WITH_UI_WINDOWS) && defined(NCINE_HAS_AUXILIARY_WINDOWS)

#include "Theme.h"
#include "../../Rendering/PolylineRenderer.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/IGfxDevice.h"
#include "../../../nCine/Graphics/Texture.h"
#include "../../../nCine/Graphics/Viewport.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::UI::Windows
{
	class DetachedWindow::WindowCanvas : public UI::Canvas
	{
	public:
		explicit WindowCanvas(DetachedWindow& owner)
			: _owner(owner)
		{
			OrderedDraws = true;
		}

		bool OnDraw(RenderQueue& renderQueue) override
		{
			UI::Canvas::OnDraw(renderQueue);
			if (_owner._draw) {
				_owner._draw(*this, _owner);
			}
			return true;
		}

	private:
		DetachedWindow& _owner;
	};

	namespace
	{
		// The scales of displays come in steps of a few percent, the pixels and the units of windows they're told by are
		// whole numbers, so they're rounded, and the same scale is the same number every time
		float RoundContentScale(float scale)
		{
			return std::max(std::round(scale * 24.0f) / 24.0f, 1.0f / 24.0f);
		}
	}

	std::unique_ptr<DetachedWindow> DetachedWindow::Create(Element* element, const Rectf& bounds, Vector2i desktopPosition, float toDesktop,
		bool popup, bool passThrough, DrawFunc&& draw)
	{
		AuxiliaryWindowDesc desc;
		desc.Bounds = Recti(desktopPosition.X, desktopPosition.Y, std::max(std::int32_t(std::round(bounds.W * toDesktop)), 1),
			std::max(std::int32_t(std::round(bounds.H * toDesktop)), 1));
		desc.Popup = popup;
		desc.PassThrough = passThrough;
		std::unique_ptr<AuxiliaryWindow> window = theApplication().GetGfxDevice().CreateAuxiliaryWindow(desc);
		if (window == nullptr) {
			return nullptr;
		}

		std::unique_ptr<DetachedWindow> detached(new DetachedWindow(element, popup, Death::move(draw)));
		detached->_window = Death::move(window);
		detached->_lines = std::make_unique<Rendering::PolylineRenderer>(1 << 16);
		detached->_canvas = std::make_unique<WindowCanvas>(*detached);
		std::int32_t width = std::max(std::int32_t(bounds.W), 1), height = std::max(std::int32_t(bounds.H), 1);
		detached->_target = std::make_unique<Texture>(nullptr, Texture::Format::RGBA8, width, height);
		detached->_targetSize = Vector2i(width, height);
		detached->_target->SetMinFiltering(SamplerFilter::Nearest);
		detached->_target->SetMagFiltering(SamplerFilter::Nearest);
		detached->_view = std::make_unique<Viewport>(detached->_target.get(), Viewport::DepthStencilFormat::None);
		detached->_view->SetRootNode(detached->_canvas.get());
		detached->_view->SetCamera(&detached->_camera);
		detached->_view->SetClearMode(Viewport::ClearMode::EveryFrame);
		Colorf clear = Theme::Resolve(Theme::PanelFill);
		detached->_view->SetClearColor(clear.R, clear.G, clear.B, 1.0f);
		detached->_window->SetContents(detached->_target.get());
		detached->_bounds = Rectf(bounds.X, bounds.Y, float(width), float(height));
		detached->Place(bounds, desktopPosition, toDesktop);
		return detached;
	}

	DetachedWindow::DetachedWindow(Element* element, bool popup, DrawFunc&& draw)
		: _element(element), _popup(popup), _draw(Death::move(draw))
	{
	}

	DetachedWindow::~DetachedWindow()
	{
		// The viewport takes itself out of the chain, before the texture and the canvas it draws go
		_view = nullptr;
	}

	void DetachedWindow::Place(const Rectf& bounds, Vector2i desktopPosition, float toDesktop)
	{
		// On a display with another scale than the main window, it's drawn with more or fewer pixels, as large as it would
		// be in the main window there
		float mainScale = RoundContentScale(theApplication().GetGfxDevice().GetWindowContentScale());
		float raster = std::clamp(RoundContentScale(_window->GetContentScale()) / mainScale, 0.25f, 4.0f);
		std::int32_t width = std::max(std::int32_t(bounds.W), 1), height = std::max(std::int32_t(bounds.H), 1);
		Vector2i target(std::max(std::int32_t(std::round(float(width) * raster)), 1), std::max(std::int32_t(std::round(float(height) * raster)), 1));
		if (target != _targetSize) {
			_view->RemoveAllTextures();
			_target->Init(nullptr, Texture::Format::RGBA8, target.X, target.Y);
			_view->SetTexture(_target.get());
			_targetSize = target;
		}
		_raster = raster;
		_bounds = Rectf(bounds.X, bounds.Y, float(width), float(height));
		_desktopPosition = desktopPosition;

		// Units of the desktop a pixel of the window takes, as the system has it now, they may be scaled by the display
		Recti current = _window->GetBounds();
		Vector2i pixels = _window->GetPixelSize();
		_unitsPerPixel = (current.W > 0 && pixels.X > 0 ? float(current.W) / float(pixels.X) : toDesktop / raster);
		_window->SetInputScale(toDesktop / (_unitsPerPixel * raster));		_window->SetBounds(Recti(desktopPosition.X, desktopPosition.Y, std::max(std::int32_t(std::round(float(target.X) * _unitsPerPixel)), 1),
			std::max(std::int32_t(std::round(float(target.Y) * _unitsPerPixel)), 1)));

		// Drawn in positions relative to the main window, as everything else of the interface
		_camera.SetOrthoProjection(_bounds.X, _bounds.X + _bounds.W, _bounds.Y, _bounds.Y + _bounds.H);
		_camera.SetView(0.0f, 0.0f, 0.0f, 1.0f);

		// The chain is made anew when the level is laid out again, the window has to be in it every frame, it's drawn
		// last, nothing depends on it
		auto& chain = Viewport::GetChain();
		if (std::find(chain.begin(), chain.end(), _view.get()) == chain.end()) {
			chain.insert(chain.begin(), _view.get());
		}
	}

	Vector2i DetachedWindow::GetDesktopSize() const
	{
		Recti bounds = _window->GetBounds();
		return Vector2i(bounds.W, bounds.H);
	}

	Vector2i DetachedWindow::ToDesktop(Vector2f point) const
	{
		float toDesktop = _raster * _unitsPerPixel;
		return Vector2i(_desktopPosition.X + std::int32_t(std::round((point.X - _bounds.X) * toDesktop)),
			_desktopPosition.Y + std::int32_t(std::round((point.Y - _bounds.Y) * toDesktop)));
	}

	void DetachedWindow::Show(bool activate)
	{
		if (!_window->IsShown()) {
			_window->Show(activate);
		}
	}

	void DetachedWindow::Hide()
	{
		_window->Hide();
		// Not drawn while it's hidden, it's added again when it's placed
		auto& chain = Viewport::GetChain();
		auto it = std::find(chain.begin(), chain.end(), _view.get());
		if (it != chain.end()) {
			chain.erase(it);
		}
	}

	bool DetachedWindow::IsShown() const
	{
		return _window->IsShown();
	}

	void DetachedWindow::Raise()
	{
		_window->Raise();
	}

	void DetachedWindow::SetTitle(StringView title)
	{
		_window->SetTitle(title);
	}
}

#endif
