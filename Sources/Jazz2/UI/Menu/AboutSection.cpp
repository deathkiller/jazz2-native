#include "AboutSection.h"
#include "MenuResources.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	define _i1 "\nWebGL"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	define _i1 "\nUWP"
#elif defined(WITH_OPENGLES)
#	define _i1 "\nOpenGL│ES"
#else
#	define _i1 "\nOpenGL"
#endif

#if defined(WITH_AUDIO) && defined(DEATH_TARGET_ANDROID)
#	define _i2 " · OpenSL│ES"
#else
#	define _i2 ""
#endif

#if defined(DEATH_CPU_USE_RUNTIME_DISPATCH)
#	if defined(DEATH_CPU_USE_IFUNC)
#		define _i3 " · GNU IF\f[h:82]UNC\f[/h]"
#	else
#		define _i3 " · \f[h:86]CPU Runtime Dispatch\f[/h]"
#	endif
#else
#	define _i3 ""
#endif

#if defined(WITH_ANGLE)
#	define _i4 "\nANGLE \f[c:#707070]· \f[h:80]https://github.com/google/angle\f[/h]\f[/c]"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	define _i4 "\nMesa \f[c:#707070]· \f[h:80]https://gitlab.freedesktop.org/mesa/mesa\f[/h]\f[/c]"
#else
#	define _i4 ""
#endif

#if defined(WITH_GLFW)
#	if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
#		define _i5 "\nGLFW/contrib \f[c:#707070]· \f[h:80]https://github.com/pongasoft/emscripten-glfw\f[/h]\f[/c]"
#	else
#		define _i5 "\nGLFW \f[c:#707070]· \f[h:80]https://www.glfw.org/\f[/h]\f[/c]"
#	endif
#elif defined(WITH_QT5)
#	define _i5 "\nQt5 \f[c:#707070]· \f[h:80]https://www.qt.io/\f[/h]\f[/c]"
#elif defined(WITH_SDL)
#	define _i5 "\nSDL2 \f[c:#707070]· \f[h:80]https://www.libsdl.org/\f[/h]\f[/c]"
#else
#	define _i5 ""
#endif

#if defined(WITH_GLEW)
#	define _i6 "\nGLEW \f[c:#707070]· \f[h:80]https://glew.sourceforge.net/\f[/h]\f[/c]"
#else
#	define _i6 ""
#endif

#if defined(WITH_AUDIO)
#	if defined(DEATH_TARGET_ANDROID)
#		define _i7 "\nOpenSL│ES\nOpenAL \f[c:#707070]· \f[h:80]https://github.com/kcat/openal-soft\f[/h]\f[/c]"
#	else
#		define _i7 "\nOpenAL \f[c:#707070]· \f[h:80]https://github.com/kcat/openal-soft\f[/h]\f[/c]"
#	endif
#else
#	define _i7 ""
#endif

#if defined(WITH_VORBIS)
#	define _i8 "\nVorbis \f[c:#707070]· \f[h:80]https://github.com/xiph/vorbis\f[/h]\f[/c]"
#else
#	define _i8 ""
#endif

#if defined(WITH_OPENMPT)
#	define _i9 "\nlibopenmpt \f[c:#707070]· \f[h:80]https://lib.openmpt.org/libopenmpt/\f[/h]\f[/c]"
#else
#	define _i9 ""
#endif

#if defined(WITH_WEBP)
#	define _i10 "\nlibwebp \f[c:#707070]· \f[h:80]https://github.com/webmproject/libwebp\f[/h]\f[/c]"
#else
#	define _i10 ""
#endif

#if defined(WITH_ZLIB)
#	define _i11 "\nzlib \f[c:#707070]· \f[h:80]https://www.zlib.net/\f[/h]\f[/c]"
#else
#	define _i11 ""
#endif

#if defined(WITH_IMGUI)
#	define _i12 "\nImGui \f[c:#707070]· \f[h:80]https://github.com/ocornut/imgui\f[/h]\f[/c]"
#else
#	define _i12 ""
#endif

#if defined(WITH_ANGELSCRIPT)
#	define _i13 "\nAngelScript \f[c:#707070]· \f[h:80]https://www.angelcode.com/angelscript/\f[/h]\f[/c]"
#else
#	define _i13 ""
#endif

#if defined(WITH_MULTIPLAYER)
#	define _i14 "\nENet \f[c:#707070]· \f[h:80]https://github.com/lsalzman/enet\f[/h]\f[/c]"
#else
#	define _i14 ""
#endif

#define _i15 "\nConcurrentQueue \f[c:#707070]· \f[h:80]https://github.com/cameron314/concurrentqueue\f[/h]\f[/c]\nParallel Hashmap \f[c:#707070]· \f[h:80]https://github.com/greg7mdp/parallel-hashmap\f[/h]\f[/c]\nPattern-defeating quicksort \f[c:#707070]· \f[h:80]https://github.com/orlp/pdqsort\f[/h]\f[/c]\nsimdjson \f[c:#707070]· \f[h:80]https://github.com/simdjson/simdjson\f[/h]\f[/c]"

#if defined(WITH_TRACY)
#	define _i16 "\n\n\f[h:86]Tracy integration is enabled.\f[/h]"
#else
#	define _i16 ""
#endif

#define ADDITIONAL_INFO _i1 _i2 _i3 _i4 _i5 _i6 _i7 _i8 _i9 _i10 _i11 _i12 _i13 _i14 _i15 _i16

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	AboutSection::AboutSection()
		: _scrollOffset(0.0f), _scrollRate(0.0f), _autoScroll(true), _rewind(false)
	{
		auto& resolver = ContentResolver::Get();

		char text[8192];
		char* textPtr = text;
		std::size_t textSize = sizeof(text);

		// TRANSLATORS: Header text in About section
		auto headerText = _("Reimplementation of the game \f[c:#9e7056]Jazz Jackrabbit 2\f[/c] released in 1998. Supports various versions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and Christmas Chronicles). Also, it partially supports some features of JJ2+ extension.");
		// TRANSLATORS: Link to website under header text in About section
		auto websiteText = _f("For more information, visit the official website: %s", "\f[c:#707070]https://deat.tk/jazz2/\f[/c]");
		// TRANSLATORS: Label in About section
		auto developersText = _("Developers");
		// TRANSLATORS: Label in About section
		auto contributorsText = _("Contributors");
		// TRANSLATORS: Label in About section
		auto translatorsText = _("Translators");
		// TRANSLATORS: Footer text in About section
		auto footerText = _("This project uses modified \f[c:#9e7056]nCine\f[/c] game engine and following libraries:");

		std::int32_t headerLength = formatString(textPtr, textSize, 
			"%s\n%s\n\n\n"
			"\f[h:125]\f[j]%s\f[/j]\f[/h]\n\f[c:#d0705d]Dan R.\f[/c]\n\n\n"
			"\f[h:125]\f[j]%s\f[/j]\f[/h]\n\f[c:#707070]\f[w:80]JJ\f[h:86]2\f[/h]\f[/w]⁺\f[w:50] \f[/w]Team\f[/c]\n\f[c:#707070]arkamar\f[/c]  \f[h:86](Gentoo maintainer)\f[/h]\n\f[c:#707070]Bioxxdevil\f[/c]\n\f[c:#707070]JWP\f[/c]  \f[h:86](xtradeb maintainer)\f[/h]\n\f[c:#707070]Kreeblah\f[/c]  \f[h:86](Homebrew maintainer)\f[/h]\n\f[c:#707070]nat\f[/c]  \f[h:86](NixOS maintainer)\f[/h]\n\f[c:#707070]roox\f[/c]  \f[h:86](OpenSUSE maintainer)\f[/h]\n\f[c:#707070]tunip3\f[/c]\n\f[c:#707070]x_Dub_CZ\f[/c]\n\f[c:#707070]Xandu\f[/c]\n\n\n"
			"\f[h:125]\f[j]%s\f[/j]\f[/h]\n",
			headerText.data(), websiteText.data(), developersText.data(), contributorsText.data(), translatorsText.data());
		textPtr += headerLength;
		textSize -= headerLength;

		// Search both "Content/Translations/" and "Cache/Translations/" for translators
		for (auto item : fs::Directory(fs::CombinePath(resolver.GetContentPath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddTranslator(item, textPtr, textSize);
		}

		for (auto item : fs::Directory(fs::CombinePath(resolver.GetCachePath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddTranslator(item, textPtr, textSize);
		}

		formatString(textPtr, textSize, "\n\n%s%s", footerText.data(), ADDITIONAL_INFO);

		_textBlock.SetAlignment(Alignment::Center);
		_textBlock.SetScale(0.8f);
		_textBlock.SetMultiline(true);
		_textBlock.SetWrapping(true);
		_textBlock.SetFont(resolver.GetFont(FontType::Small));
		_textBlock.SetText(StringView(text));
	}

	Recti AboutSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
	}

	void AboutSection::OnUpdate(float timeMult)
	{
		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		} else if (_root->ActionPressed(PlayerActions::Up)) {
			if (_scrollRate < MaxScrollRate) {
				_scrollRate += 0.16f * timeMult;
			}
			_scrollOffset -= _scrollRate * timeMult;
			_autoScroll = false;
		} else if (_root->ActionPressed(PlayerActions::Down)) {
			if (_scrollRate < MaxScrollRate) {
				_scrollRate += 0.16f * timeMult;
			}
			_scrollOffset += _scrollRate * timeMult;
			_autoScroll = false;
		} else {
			_scrollRate = 0.0f;

			if (_autoScroll) {
				_scrollOffset += (_rewind ? (AutoScrollRate * -40.0f) : AutoScrollRate) * timeMult;
			}
		}
	}

	void AboutSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
	}

	void AboutSection::OnDrawClipped(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		float viewHeight = (bottomLine - topLine);
		float textBlockHeight = _textBlock.GetCachedHeight();
		float maxScrollOffset = textBlockHeight + viewHeight;
		if (_autoScroll && _rewind && _scrollOffset < viewHeight - 16.0f) {
			_scrollOffset = viewHeight - 16.0f;
			_autoScroll = false;
		} else if (_scrollOffset < 0.0f || textBlockHeight <= 0.0f) {
			_scrollOffset = 0.0f;
		} else if (_scrollOffset > maxScrollOffset) {
			_scrollOffset = maxScrollOffset;
			if (_autoScroll) {
				_rewind = true;
			}
		}

		int32_t charOffset = 0;
		_textBlock.Draw(canvas, Rectf(contentBounds.X + 60.0f, topLine + viewHeight - roundf(_scrollOffset), contentBounds.W - 120.0f, 1000000.0f),
			IMenuContainer::FontLayer, charOffset, 0.7f, 1.0f, 1.0f);

		if (_scrollOffset > viewHeight - 6.0f) {
			_root->DrawElement(MenuGlow, 0, centerX, topLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		if (_scrollOffset < maxScrollOffset - viewHeight) {
			_root->DrawElement(MenuGlow, 0, centerX, bottomLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	void AboutSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					std::int32_t y = (std::int32_t)(event.pointers[pointerIndex].y * viewSize.Y);
					if (y < 80) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}

					_touchLast = Vector2i((std::int32_t)(event.pointers[pointerIndex].x * viewSize.X), y);
					_autoScroll = false;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchLast != Vector2i::Zero) {
					std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2i touchMove = Vector2i((std::int32_t)(event.pointers[pointerIndex].x * viewSize.X), (std::int32_t)(event.pointers[pointerIndex].y * viewSize.Y));
						_scrollOffset += _touchLast.Y - touchMove.Y;
						_touchLast = touchMove;
					}
				}
				break;
			}
		}
	}

	void AboutSection::AddTranslator(const StringView languageFile, char*& result, std::size_t& resultLength)
	{
		if (fs::GetExtension(languageFile) != "mo"_s) {
			return;
		}

		auto language = fs::GetFileNameWithoutExtension(languageFile);
		if (language.empty() || language.size() >= sizeof(PreferencesCache::Language)) {
			return;
		}

		I18n i18n;
		if (!i18n.LoadFromFile(languageFile)) {
			return;
		}

		String desc = i18n.GetTranslationDescription();
		std::size_t descLength = desc.size();
		if (descLength == 0 || descLength >= resultLength) {
			return;
		}

		StringView langName = I18n::GetLanguageName(language);

		std::int32_t lineLength = formatString(result, resultLength, "\f[c:#707070]%s\f[/c]  \f[h:86](%s)\f[/h]\n", desc.data(), langName.data());
		result += lineLength;
		resultLength -= lineLength;
	}
}