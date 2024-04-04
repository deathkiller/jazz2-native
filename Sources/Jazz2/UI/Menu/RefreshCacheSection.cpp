#include "RefreshCacheSection.h"

#if !defined(DEATH_TARGET_EMSCRIPTEN)

#include "MenuResources.h"
#include "MainMenu.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/Graphics/BinaryShaderCache.h"
#include "../../../nCine/Graphics/RenderResources.h"

using namespace Jazz2::UI::Menu::Resources;
using namespace nCine;

namespace Jazz2::UI::Menu
{
	RefreshCacheSection::RefreshCacheSection()
		: _animation(0.0f), _done(false)
	{
	}

	void RefreshCacheSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

#if defined(WITH_THREADS)
		_thread.Run([](void* arg) {
			auto _this = static_cast<RefreshCacheSection*>(arg);
			if (auto mainMenu = dynamic_cast<MainMenu*>(_this->_root)) {
				mainMenu->_root->RefreshCacheLevels();
			}

			std::uint32_t filesRemoved = RenderResources::binaryShaderCache().prune();
			LOGI("Pruning binary shader cache (removed %u files)...", filesRemoved);

			_this->_done = true;
		}, this);
#else
		if (auto mainMenu = dynamic_cast<MainMenu*>(_root)) {
			mainMenu->_root->RefreshCacheLevels();
		}

		std::uint32_t filesRemoved = RenderResources::binaryShaderCache().prune();
		LOGI("Pruning binary shader cache (removed %u files)...", filesRemoved);

		_done = true;
#endif
	}

	void RefreshCacheSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_done) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		}
	}

	void RefreshCacheSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		Vector2f center = Vector2f(contentBounds.X + contentBounds.W * 0.5f, contentBounds.Y + contentBounds.H * 0.5f);
		float topLine = contentBounds.Y + 31.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - 42.0f;

		_root->DrawElement(MenuDim, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		center.Y = topLine + (bottomLine - topLine) * 0.4f;
		int32_t charOffset = 0;

		_root->DrawStringShadow(_("Refresh Cache"), charOffset, center.X, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		_root->DrawStringShadow(_("Processing of files in \f[c:0x9e7056]\"Source\"\f[c] directory..."), charOffset, center.X, center.Y, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		_root->DrawStringShadow(_("Newly added levels and episodes will be available soon."), charOffset, center.X, center.Y + 24.0f, IMenuContainer::FontLayer,
			Alignment::Top, Font::DefaultColor, 0.8f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void RefreshCacheSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		// No actions are allowed
	}
}

#endif