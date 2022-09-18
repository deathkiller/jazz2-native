#include "AboutSection.h"

namespace Jazz2::UI::Menu
{
	AboutSection::AboutSection()
	{
		_additionalInfo[0] = '\0';

#if defined(_DEBUG)
#	define BUILD_TYPE "debug"
#else
#	define BUILD_TYPE "release"
#endif

		strcat_s(_additionalInfo, "This project uses modified nCine game engine.\n\nThis " BUILD_TYPE " build uses these additional libraries:\n");
#if defined(WITH_OPENGLES)
		strcat_s(_additionalInfo, "OpenGL ES");
#else
		strcat_s(_additionalInfo, "OpenGL");
#endif
#if defined(WITH_GLEW)
		strcat_s(_additionalInfo, ", GLEW");
#endif
#if defined(WITH_ANGLE)
		strcat_s(_additionalInfo, ", Angle");
#endif
#if defined(WITH_GLFW)
		strcat_s(_additionalInfo, ", GLFW");
#endif
#if defined(WITH_QT5)
		strcat_s(_additionalInfo, ", Qt");
#endif
#if defined(WITH_SDL)
		strcat_s(_additionalInfo, ", SDL");
#endif
#if defined(WITH_AUDIO)
		strcat_s(_additionalInfo, ", OpenAL");
#endif
#if defined(WITH_VORBIS)
		strcat_s(_additionalInfo, ", Vorbis");
#endif
#if defined(WITH_OPENMPT)
		strcat_s(_additionalInfo, ", libopenmpt");
#endif
#if defined(WITH_WEBP)
		strcat_s(_additionalInfo, ", libwebp");
#endif
#if defined(WITH_ZLIB)
		strcat_s(_additionalInfo, ", zlib");
#else
		strcat_s(_additionalInfo, ", libdeflate");
#endif
#if defined(WITH_ANGELSCRIPT)
		strcat_s(_additionalInfo, ", AngelScript");
#endif
#if defined(WITH_TRACY)
		strcat_s(_additionalInfo, "\n\nTracy integration is enabled!");
#endif
		int s = (int)strlen(_additionalInfo);
		LOGI_X("SIZE: %i", s);
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
		pos.Y = std::round(std::max(130.0f, pos.Y * 0.86f));

		_root->DrawElement("MenuDim"_s, pos.X, pos.Y + 60.0f - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::White, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, 0.7f, 0.0f));

		pos.X = std::round(pos.X * 0.35f);

		int charOffset = 0;

		_root->DrawStringShadow("Reimplementation of the game Jazz Jackrabbit 2 released in 1998. Supports various\nversions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and\nChristmas Chronicles). Also, it partially supports some features of JJ2+ extension."_s,
			charOffset, viewSize.X * 0.5f, pos.Y - 22.0f, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		_root->DrawStringShadow("Created By"_s, charOffset, pos.X, pos.Y + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.85f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f);
		_root->DrawStringShadow("Dan R."_s, charOffset, pos.X + 25.0f, pos.Y + 20.0f + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 1.0f, 0.4f, 0.75f, 0.75f, 0.6f, 0.9f);

		_root->DrawStringShadow("<http://deat.tk/jazz2/>"_s, charOffset, pos.X + 25.0f + 70.0f, pos.Y + 20.0f + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f);

		_root->DrawStringShadow(_additionalInfo, charOffset, viewSize.X * 0.5f, pos.Y + 24.0f + pos.Y * 0.34f, IMenuContainer::FontLayer,
			Alignment::Top, Font::DefaultColor, 0.8f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		_root->DrawElement("MenuLine"_s, 0, viewSize.X * 0.5f, pos.Y + 60.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

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