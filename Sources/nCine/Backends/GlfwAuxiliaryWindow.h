#pragma once

#include "GlfwGfxDevice.h"

#if (defined(WITH_GLFW) && defined(NCINE_HAS_AUXILIARY_WINDOWS)) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace nCine::Backends
{
	/** @brief Window besides the main one made with GLFW, see @ref AuxiliaryWindow */
	class GlfwAuxiliaryWindow : public AuxiliaryWindow
	{
	public:
		/** @brief Returns `true` if windows besides the main one can be made with this build */
		static bool IsSupported();
		/** @brief Makes a hidden window, `nullptr` if it can't be made */
		static std::unique_ptr<GlfwAuxiliaryWindow> Create(const AuxiliaryWindowDesc& desc);
		/** @brief Returns the window of a GLFW window, `nullptr` if it's not one */
		static GlfwAuxiliaryWindow* FromHandle(GLFWwindow* window);

		~GlfwAuxiliaryWindow() override;

		void SetBounds(const Recti& bounds) override;
		Recti GetBounds() const override;
		Vector2i GetPixelSize() const override;
		float GetContentScale() const override;
		bool IsFocused() const override;
		void Raise() override;
		void SetTitle(StringView title) override;

		/** @brief Returns the GLFW window */
		GLFWwindow* GetHandle() const {
			return _window;
		}

	protected:
		void SetVisible(bool visible, bool activate) override;

	private:
		GLFWwindow* _window = nullptr;
		Vector2i _size;
		bool _popup = false;

		explicit GlfwAuxiliaryWindow(const AuxiliaryWindowDesc& desc);
	};
}

#endif
