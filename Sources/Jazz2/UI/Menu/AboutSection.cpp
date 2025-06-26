#include "AboutSection.h"
#include "MenuResources.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	define _i1 "\nWebGL"
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	define _i1 "\nUWP"
#elif defined(WITH_OPENGLES)
#	define _i1 "\nOpenGL│ES"
#else
#	define _i1 "\nOpenGL"
#endif

// Reserved for future use
#define _i2 ""

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
#	define _i7 "\nOpenAL \f[c:#707070]· \f[h:80]https://github.com/kcat/openal-soft\f[/h]\f[/c]"
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

#if defined(WITH_CURL)
#	define _i11 "\nlibcurl \f[c:#707070]· \f[h:80]https://curl.se/libcurl/\f[/h]\f[/c]"
#else
#	define _i11 ""
#endif

#if defined(WITH_ZLIB)
#	define _i12 "\nzlib \f[c:#707070]· \f[h:80]https://www.zlib.net/\f[/h]\f[/c]"
#else
#	define _i12 ""
#endif

#if defined(WITH_IMGUI)
#	define _i13 "\nImGui \f[c:#707070]· \f[h:80]https://github.com/ocornut/imgui\f[/h]\f[/c]"
#else
#	define _i13 ""
#endif

#if defined(WITH_ANGELSCRIPT)
#	define _i14 "\nAngelScript \f[c:#707070]· \f[h:80]https://www.angelcode.com/angelscript/\f[/h]\f[/c]"
#else
#	define _i14 ""
#endif

#if defined(WITH_BACKWARD)
#	define _i15 "\nBackward \f[c:#707070]· \f[h:80]https://github.com/bombela/backward-cpp\f[/h]\f[/c]"
#else
#	define _i15 ""
#endif

#if defined(WITH_MULTIPLAYER)
#	define _i16 "\nENet \f[c:#707070]· \f[h:80]https://github.com/lsalzman/enet\f[/h]\f[/c]"
#else
#	define _i16 ""
#endif

#define _i17 "\njsoncpp \f[c:#707070]· \f[h:80]https://github.com/open-source-parsers/jsoncpp\f[/h]\f[/c]\nParallel Hashmap \f[c:#707070]· \f[h:80]https://github.com/greg7mdp/parallel-hashmap\f[/h]\f[/c]\n\f[h:80]Parts of \f[/h]Corrade \f[c:#707070]· \f[h:80]https://github.com/mosra/corrade\f[/h]\f[/c]\nPattern-defeating quicksort \f[c:#707070]· \f[h:80]https://github.com/orlp/pdqsort\f[/h]\f[/c]"

#if defined(WITH_TRACY)
#	define _i18 "\n\n\f[h:86]Tracy integration is enabled.\f[/h]"
#else
#	define _i18 ""
#endif

#define ADDITIONAL_INFO _i1 _i2 _i3 _i4 _i5 _i6 _i7 _i8 _i9 _i10 _i11 _i12 _i13 _i14 _i15 _i16 _i17 _i18

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	AboutSection::AboutSection()
		: _maxScrollOffset(0.0f), _touchTime(0.0f), _touchSpeed(0.0f), _scrollOffset(0.0f), _scrollRate(0.0f),
			_touchDirection(0), _autoScroll(true), _rewind(false)
	{
		auto& resolver = ContentResolver::Get();

		char text[8192];
		char* textPtr = text;
		std::size_t textSize = sizeof(text);

		// TRANSLATORS: Header text in About section
		auto headerText = _("Reimplementation of the game \f[c:#9e7056]Jazz Jackrabbit 2\f[/c] released in 1998. Supports various versions of the game (Shareware Demo, Holiday Hare '98, The Secret Files and Christmas Chronicles). Also, it partially supports some features of JJ2+ extension.");
		// TRANSLATORS: Link to website under header text in About section
		auto websiteText = _("For more information, visit the official website:");
		// TRANSLATORS: Label in About section
		auto developersText = _("Developers");
		// TRANSLATORS: Label in About section
		auto contributorsText = _("Contributors");
		// TRANSLATORS: Label in About section
		auto translatorsText = _("Translators");
		// TRANSLATORS: Footer text in About section
		auto footerText = _("This project uses modified \f[c:#9e7056]nCine\f[/c] game engine and following libraries:");

		std::int32_t headerLength = formatString(textPtr, textSize, 
			"%s\n%s\n\f[w:80]\f[c:#707070]https://deat.tk/jazz2/\f[/c]\f[/w]\n\n\n"
			"\f[h:125]\f[j]%s\f[/j]\f[/h]\n\f[c:#d0705d]Dan R.\f[/c]\n\n\n"
			"\f[h:125]\f[j]%s\f[/j]\f[/h]\n\f[c:#707070]\f[w:80]JJ\f[h:86]2\f[/h]\f[/w]⁺\f[w:50] \f[/w]Team\f[/c]\n\f[c:#707070]arkamar\f[/c]  \f[h:86](Gentoo maintainer)\f[/h]\n\f[c:#707070]Bioxxdevil\f[/c]\n\f[c:#707070]Chewi\f[/c]  \f[h:86](Gentoo maintainer)\f[/h]\n\f[c:#707070]JWP\f[/c]  \f[h:86](xtradeb maintainer)\f[/h]\n\f[c:#707070]Kreeblah\f[/c]  \f[h:86](Homebrew maintainer)\f[/h]\n\f[c:#707070]nat\f[/c]  \f[h:86](NixOS maintainer)\f[/h]\n\f[c:#707070]roox\f[/c]  \f[h:86](OpenSUSE maintainer)\f[/h]\n\f[c:#707070]tunip3\f[/c]\n\f[c:#707070]x_Dub_CZ\f[/c]\n\f[c:#707070]Xandu\f[/c]\n\n\n"
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
		_textBlock.SetText(StringView{text});

		_textBlockHeaderOnly.SetAlignment(Alignment::Center);
		_textBlockHeaderOnly.SetScale(0.8f);
		_textBlockHeaderOnly.SetMultiline(true);
		_textBlockHeaderOnly.SetWrapping(true);
		_textBlockHeaderOnly.SetFont(resolver.GetFont(FontType::Small));
		_textBlockHeaderOnly.SetText(headerText);
	}

	Recti AboutSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
	}

	void AboutSection::OnUpdate(float timeMult)
	{
		if (_root->ActionHit(PlayerAction::Fire)) {
			// Approximation of scroll offsets, needs to be changed when header is changed
			if (_scrollOffset > 40.0f && _scrollOffset < 400.0f) {
				if (theApplication().OpenUrl("https://deat.tk/jazz2/"_s)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
				}
			}
		} else if (_root->ActionHit(PlayerAction::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (_root->ActionPressed(PlayerAction::Up)) {
			if (_scrollRate < MaxScrollRate) {
				_scrollRate += 0.16f * timeMult;
			}
			_scrollOffset -= _scrollRate * timeMult;
			_autoScroll = false;
		} else if (_root->ActionPressed(PlayerAction::Down)) {
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

		if (_touchSpeed > 0.0f) {
			if (_touchStart == Vector2f::Zero) {
				float scrollOffset = _scrollOffset + (_touchSpeed * (std::int32_t)_touchDirection * TouchKineticDivider * timeMult);
				if (scrollOffset < 0.0f && _touchDirection < 0) {
					scrollOffset = 0.0f;
					_touchDirection = 1;
					_touchSpeed *= TouchKineticDamping;
				} else if (scrollOffset > _maxScrollOffset && _touchDirection > 0) {
					scrollOffset = _maxScrollOffset;
					_touchDirection = -1;
					_touchSpeed *= TouchKineticDamping;
				}
				_scrollOffset = scrollOffset;
			}

			_touchSpeed = std::max(_touchSpeed - TouchKineticFriction * TouchKineticDivider * timeMult, 0.0f);
		}

		_touchTime += timeMult;
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
		_maxScrollOffset = textBlockHeight + viewHeight;
		if (_autoScroll && _rewind && _scrollOffset < viewHeight - 16.0f) {
			_scrollOffset = viewHeight - 16.0f;
			_autoScroll = false;
		} else if (_scrollOffset < 0.0f || textBlockHeight <= 0.0f) {
			_scrollOffset = 0.0f;
		} else if (_scrollOffset > _maxScrollOffset) {
			_scrollOffset = _maxScrollOffset;
			if (_autoScroll) {
				_rewind = true;
			}
		}

		float padding = (contentBounds.W > 450.0f ? 60.0f : 20.0f);

		Vector2f headerSize = _textBlockHeaderOnly.MeasureSize(Vector2f(contentBounds.W - padding * 2.0f, 1000000.0f));
		_root->DrawElement(MenuGlow, 0, centerX, topLine + viewHeight + headerSize.Y + 14.0f + 2.0f - roundf(_scrollOffset),
			IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f), 10.0f, 5.0f, true, true);

		int32_t charOffset = 0;
		_textBlock.Draw(canvas, Rectf(contentBounds.X + padding, topLine + viewHeight - roundf(_scrollOffset), contentBounds.W - padding * 2.0f, 1000000.0f),
			IMenuContainer::FontLayer, charOffset, 0.7f, 1.0f, 1.0f);

		if (_scrollOffset > viewHeight - 6.0f) {
			_root->DrawElement(MenuGlow, 0, centerX, topLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		if (_scrollOffset < _maxScrollOffset - viewHeight) {
			_root->DrawElement(MenuGlow, 0, centerX, bottomLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	void AboutSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
					if (y < 80.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}

					_touchStart = Vector2f(event.pointers[pointerIndex].x * viewSize.X, y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
					_autoScroll = false;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * viewSize.X, event.pointers[pointerIndex].y * viewSize.Y);
						float delta = _touchLast.Y - touchMove.Y;
						if (delta != 0.0f) {
							_scrollOffset += delta;
							if (delta < -0.1f && _touchDirection >= 0) {
								_touchDirection = -1;
								_touchSpeed = 0.0f;
							} else if (delta > 0.1f && _touchDirection <= 0) {
								_touchDirection = 1;
								_touchSpeed = 0.0f;
							}
							_touchSpeed = (0.8f * _touchSpeed) + (0.2f * std::abs(delta) / TouchKineticDivider);
						}
						_touchLast = touchMove;
					}
				}
				break;
			}
			case TouchEventType::Up: {
				bool alreadyMoved = (_touchStart == Vector2f::Zero || (_touchStart - _touchLast).SqrLength() > 100 || _touchTime > FrameTimer::FramesPerSecond);
				_touchStart = Vector2f::Zero;
				if (alreadyMoved) {
					return;
				}

				if (_touchLast.Y > 120.0f) {
					// Approximation of scroll offsets, needs to be changed when header is changed
					if (_scrollOffset > 40.0f && _scrollOffset < 400.0f) {
						if (theApplication().OpenUrl("https://deat.tk/jazz2/"_s)) {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
						}
					}
				}
				break;
			}
		}
	}

	void AboutSection::AddTranslator(StringView languageFile, char*& result, std::size_t& resultLength)
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

		std::int32_t lineLength;
		if (langName) {
			lineLength = formatString(result, resultLength, "\f[c:#707070]%s\f[/c]  \f[h:86](%s)\f[/h]\n", desc.data(), langName.data());
		} else {
			lineLength = formatString(result, resultLength, "\f[c:#707070]%s\f[/c]\n", desc.data());
		}
		result += lineLength;
		resultLength -= lineLength;
	}
}