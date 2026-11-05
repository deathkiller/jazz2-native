#pragma once

#include "SdlGfxDevice.h"

#if ((defined(WITH_SDL2) || defined(WITH_SDL3)) && defined(NCINE_HAS_AUXILIARY_WINDOWS)) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace nCine::Backends
{
	/** @brief Window besides the main one made with SDL, see @ref AuxiliaryWindow */
	class SdlAuxiliaryWindow : public AuxiliaryWindow
	{
	public:
		/** @brief Returns `true` if windows besides the main one can be made with this build and video driver */
		static bool IsSupported();
		/** @brief Makes a hidden window, `nullptr` if it can't be made */
		static std::unique_ptr<SdlAuxiliaryWindow> Create(const AuxiliaryWindowDesc& desc);

		~SdlAuxiliaryWindow() override;

		void SetBounds(const Recti& bounds) override;
		Recti GetBounds() const override;
		Vector2i GetPixelSize() const override;
		float GetContentScale() const override;
		bool IsFocused() const override;
		void Raise() override;
		void SetTitle(StringView title) override;

	protected:
		void SetVisible(bool visible, bool activate) override;

	private:
		SDL_Window* _window = nullptr;
		Vector2i _size;
		bool _popup = false;
#if defined(WITH_RHI_GL)
		SDL_GLContext _glContext = nullptr;
#endif

		explicit SdlAuxiliaryWindow(const AuxiliaryWindowDesc& desc);
	};
}

#endif
