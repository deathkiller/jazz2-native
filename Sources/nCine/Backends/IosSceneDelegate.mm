// UIScene life cycle support for SDL2 on iOS: SDL2 has no scene delegate of its own (SDLUIKitSceneDelegate is SDL3),
// and a UIWindow without a windowScene is never shown. The window and the scene arrive in either order, so whichever
// comes second does the attaching. Compiled without ARC (the project default), hence the explicit retain/release.

#import <UIKit/UIKit.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "IosSceneDelegate.h"

static UIWindowScene* _connectedScene = nil;
static UIWindow* _pendingWindow = nil;

static void TryAttach()
{
	if (_connectedScene == nil || _pendingWindow == nil) {
		return;
	}
	_pendingWindow.windowScene = _connectedScene;
	// SDL already called makeKeyAndVisible, but without a scene it had no effect
	[_pendingWindow makeKeyAndVisible];
	[_pendingWindow release];
	_pendingWindow = nil;
}

@interface NCineSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

@implementation NCineSceneDelegate

- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)connectionOptions
{
	if (![scene isKindOfClass:[UIWindowScene class]]) {
		return;
	}
	[_connectedScene release];
	_connectedScene = [(UIWindowScene*)scene retain];
	TryAttach();
}

@end

namespace nCine::Backends
{
	void IosAttachToWindowScene(SDL_Window* window)
	{
		SDL_SysWMinfo wmInfo;
		SDL_VERSION(&wmInfo.version);
		if (!SDL_GetWindowWMInfo(window, &wmInfo) || wmInfo.subsystem != SDL_SYSWM_UIKIT) {
			return;
		}
		[_pendingWindow release];
		_pendingWindow = [wmInfo.info.uikit.window retain];
		TryAttach();
	}
}
