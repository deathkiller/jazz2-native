#include "AboutSection.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	define _i1 "WebGL"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	define _i1 "UWP"
#elif defined(WITH_OPENGLES)
#	define _i1 "OpenGL ES"
#else
#	define _i1 "OpenGL"
#endif

#if defined(WITH_GLEW)
#	define _i2 ", GLEW"
#else
#	define _i2 ""
#endif

#if defined(WITH_ANGLE)
#	define _i3 ", ANGLE"
#else
#	define _i3 ""
#endif

#if defined(WITH_GLFW)
#	define _i4 ", GLFW"
#else
#	define _i4 ""
#endif

#if defined(WITH_QT5)
#	define _i5 ", Qt"
#else
#	define _i5 ""
#endif

#if defined(WITH_SDL)
#	define _i6 ", SDL"
#else
#	define _i6 ""
#endif

#if defined(WITH_AUDIO)
#	if defined(DEATH_TARGET_ANDROID)
#		define _i7 ", OpenSL ES, OpenAL"
#	else
#		define _i7 ", OpenAL"
#	endif
#else
#	define _i7 ""
#endif

#if defined(WITH_VORBIS)
#	define _i8 ", Vorbis"
#else
#	define _i8 ""
#endif

#if defined(WITH_OPENMPT)
#	define _i9 ", libopenmpt"
#else
#	define _i9 ""
#endif

#if defined(WITH_WEBP)
#	define _i10 ", libwebp"
#else
#	define _i10 ""
#endif

#if defined(WITH_ZLIB)
#	define _i11 ", zlib"
#else
#	define _i11 ", libdeflate"
#endif

#if defined(WITH_ANGELSCRIPT)
#	define _i12 ", AngelScript"
#else
#	define _i12 ""
#endif

#if defined(WITH_TRACY)
#	define _i13 "\n\nTracy integration is enabled!"
#else
#	define _i13 ""
#endif

#define ADDITIONAL_INFO _i1 _i2 _i3 _i4 _i5 _i6 _i7 _i8 _i9 _i10 _i11 _i12 _i13

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

		_root->DrawElement("MenuDim"_s, pos.X, pos.Y + 24.0f - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::Black, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, 0.7f, 0.0f));

		pos.X = std::round(pos.X * 0.35f);

		int32_t charOffset = 0;

		// TRANSLATORS: Main information in About section
		_root->DrawStringShadow(_f("Reimplementation of the game \f[c:0x9e7056]Jazz Jackrabbit 2\f[c] released in 1998. Supports various\nversions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and\nChristmas Chronicles). Also, it partially supports some features of JJ2+ extension.\nFor more information, visit the official website: %s", "\f[c:0x707070]http://deat.tk/jazz2/\f[c]"),
			charOffset, viewSize.X * 0.5f, pos.Y - 22.0f, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		// TRANSLATORS: Header in About section
		_root->DrawStringShadow(_("Created By"), charOffset, pos.X, pos.Y + 42.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.85f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f);
		_root->DrawStringShadow("Dan R."_s, charOffset, pos.X + 25.0f, pos.Y + 45.0f + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 1.0f, 0.4f, 0.75f, 0.75f, 0.6f, 0.9f);

		_root->DrawStringShadow("&  Contributors: \f[c:0xd0705d]JJ2+ Team\f[c], \f[c:0x707070]Bioxxdevil\f[c], \f[c:0x707070]roox\f[c], \f[c:0x707070]tunip3\f[c], ..."_s, charOffset, pos.X + 22.0f + 70.0f, pos.Y + 45.0f + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.8f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f);

		_root->DrawStringShadow(I18n::Get().GetTranslationDescription(),
			charOffset, pos.X + 25.0f, pos.Y + 45.0f + 44.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.74f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		// TRANSLATORS: Bottom information in About section
		_root->DrawStringShadow(_f("This project uses modified \f[c:0x9e7056]nCine\f[c] game engine and following libraries:\n%s", ADDITIONAL_INFO), charOffset, viewSize.X * 0.5f, pos.Y + 54.0f + pos.Y * 0.4f, IMenuContainer::FontLayer,
			Alignment::Top, Font::DefaultColor, 0.76f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		_root->DrawElement("MenuLine"_s, 0, viewSize.X * 0.5f, pos.Y + 24.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		pos.Y = viewSize.Y - 100.0f;
	}

	void AboutSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
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