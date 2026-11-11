#pragma once

struct SDL_Window;

namespace nCine::Backends
{
	/// Attaches the SDL window to the connected UIWindowScene (now or once it connects)
	/**
	 * SDL2 creates its UIWindow without a scene, which stays invisible under the UIScene life cycle that iOS 27
	 * requires; NCineSceneDelegate (named in Info.plist) hands the scene over.
	 */
	void IosAttachToWindowScene(SDL_Window* window);
}
