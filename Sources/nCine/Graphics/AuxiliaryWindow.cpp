#include "AuxiliaryWindow.h"

#if defined(NCINE_HAS_AUXILIARY_WINDOWS)

#include "AuxiliaryWindowPresenter.h"

#include <Containers/SmallVector.h>

namespace nCine
{
	namespace
	{
		Death::Containers::SmallVector<AuxiliaryWindow*, 4>& GetWindows()
		{
			static Death::Containers::SmallVector<AuxiliaryWindow*, 4> windows;
			return windows;
		}
	}

	AuxiliaryWindow::AuxiliaryWindow()
	{
		GetWindows().push_back(this);
	}

	AuxiliaryWindow::~AuxiliaryWindow()
	{
		auto& windows = GetWindows();
		for (std::size_t i = 0; i < windows.size(); i++) {
			if (windows[i] == this) {
				windows.erase(windows.begin() + i);
				break;
			}
		}
	}

	void AuxiliaryWindow::Show(bool activate)
	{
		_shown = true;
		_activateWhenShown = activate;
		if (_visible && activate) {
			Raise();
		}
	}

	void AuxiliaryWindow::Hide()
	{
		_shown = false;
		if (_visible) {
			_visible = false;
			SetVisible(false, false);
		}
	}

	void AuxiliaryWindow::Present()
	{
		if (!_shown || _presenter == nullptr || _contents == nullptr) {
			return;
		}
		_presenter->Present(_contents, GetPixelSize());
		if (!_visible) {
			// Only now, what it shows was drawn for it
			_visible = true;
			SetVisible(true, _activateWhenShown);
		}
	}

	Death::Containers::ArrayView<AuxiliaryWindow* const> AuxiliaryWindow::GetAll()
	{
		auto& windows = GetWindows();
		return Death::Containers::ArrayView<AuxiliaryWindow* const>(windows.data(), windows.size());
	}

	AuxiliaryWindow* AuxiliaryWindow::FindById(std::uint32_t id)
	{
		if (id == 0) {
			return nullptr;
		}
		for (AuxiliaryWindow* window : GetWindows()) {
			if (window->_id == id) {
				return window;
			}
		}
		return nullptr;
	}

	bool AuxiliaryWindow::IsAnyFocused()
	{
		for (AuxiliaryWindow* window : GetWindows()) {
			if (window->IsFocused()) {
				return true;
			}
		}
		return false;
	}

	float AuxiliaryWindow::GetContentScale() const
	{
		Recti bounds = GetBounds();
		Vector2i pixels = GetPixelSize();
		return (bounds.W > 0 && pixels.X > 0 ? float(pixels.X) / float(bounds.W) : 1.0f);
	}
}

#endif
