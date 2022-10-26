#include "AboutSection.h"

#if defined(_DEBUG)
#	define INFO_BUILD_TYPE "\f[c:0x996a6a]debug\f[c]"
#else
#	define INFO_BUILD_TYPE "release"
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	define INFO1 "WebGL"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	define INFO1 "UWP"
#elif defined(WITH_OPENGLES)
#	define INFO1 "OpenGL ES"
#else
#	define INFO1 "OpenGL"
#endif

#if defined(WITH_GLEW)
#	define INFO2 ", GLEW"
#else
#	define INFO2 ""
#endif

#if defined(WITH_ANGLE)
#	define INFO3 ", Angle"
#else
#	define INFO3 ""
#endif

#if defined(WITH_GLFW)
#	define INFO4 ", GLFW"
#else
#	define INFO4 ""
#endif

#if defined(WITH_QT5)
#	define INFO5 ", Qt"
#else
#	define INFO5 ""
#endif

#if defined(WITH_SDL)
#	define INFO6 ", SDL"
#else
#	define INFO6 ""
#endif

#if defined(WITH_AUDIO)
#	if defined(DEATH_TARGET_ANDROID)
#		define INFO7 ", OpenSL ES, OpenAL"
#	else
#		define INFO7 ", OpenAL"
#	endif
#else
#	define INFO7 ""
#endif

#if defined(WITH_VORBIS)
#	define INFO8 ", Vorbis"
#else
#	define INFO8 ""
#endif

#if defined(WITH_OPENMPT)
#	define INFO9 ", libopenmpt"
#else
#	define INFO9 ""
#endif

#if defined(WITH_WEBP)
#	define INFO10 ", libwebp"
#else
#	define INFO10 ""
#endif

#if defined(WITH_ZLIB)
#	define INFO11 ", zlib"
#else
#	define INFO11 ", libdeflate"
#endif

#if defined(WITH_ANGELSCRIPT)
#	define INFO12 ", AngelScript"
#else
#	define INFO12 ""
#endif

#if defined(WITH_TRACY)
#	define INFO13 "\n\nTracy integration is enabled!"
#else
#	define INFO13 ""
#endif

#define ADDITIONAL_INFO "This project uses modified \f[c:0x9e7056]nCine\f[c] game engine.\n\nThis " INFO_BUILD_TYPE " build uses these additional libraries:\n" INFO1 INFO2 INFO3 INFO4 INFO5 INFO6 INFO7 INFO8 INFO9 INFO10 INFO11 INFO12 INFO13

namespace Jazz2::UI::Menu
{
	AboutSection::AboutSection()
	{
	}

	void AboutSection::OnUpdate(float timeMult)
	{
		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		}
	}

	void AboutSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		
		Vector2f pos = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);
		pos.Y = std::round(std::max(150.0f, pos.Y * 0.86f));

		_root->DrawElement("MenuDim"_s, pos.X, pos.Y + 70.0f - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::Black, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, 0.7f, 0.0f));

		pos.X = std::round(pos.X * 0.35f);

		int charOffset = 0;

		_root->DrawStringShadow("Reimplementation of the game \f[c:0x9e7056]Jazz Jackrabbit 2\f[c] released in 1998. Supports various\nversions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and\nChristmas Chronicles). Also, it partially supports some features of JJ2+ extension.\nFor more information, visit the official website: \f[c:0x707070]http://deat.tk/jazz2/\f[c]"_s,
			charOffset, viewSize.X * 0.5f, pos.Y - 22.0f, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		_root->DrawStringShadow("Created By"_s, charOffset, pos.X, pos.Y + 30.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.85f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f);
		_root->DrawStringShadow("Dan R."_s, charOffset, pos.X + 25.0f, pos.Y + 30.0f + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 1.0f, 0.4f, 0.75f, 0.75f, 0.6f, 0.9f);

		_root->DrawStringShadow("&  Contributors: \f[c:0x707070]Bioxxdevil\f[c], \f[c:0x707070]roox\f[c], \f[c:0x707070]tunip3\f[c]"_s, charOffset, pos.X + 25.0f + 70.0f, pos.Y + 30.0f + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f);

		_root->DrawStringShadow(ADDITIONAL_INFO, charOffset, viewSize.X * 0.5f, pos.Y + 34.0f + pos.Y * 0.34f, IMenuContainer::FontLayer,
			Alignment::Top, Font::DefaultColor, 0.8f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		_root->DrawElement("MenuLine"_s, 0, viewSize.X * 0.5f, pos.Y + 70.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		pos.Y = viewSize.Y - 100.0f;
	}

	void AboutSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
				}
			}
		}
	}
}