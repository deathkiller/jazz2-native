#include "ServerSelectSection.h"

#if defined(WITH_MULTIPLAYER)

#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "MenuResources.h"
#include "SimpleMessageSection.h"
#include "../../PreferencesCache.h"
#include "../../Multiplayer/NetworkManagerBase.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/FrameTimer.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

#include <Containers/StringConcatenable.h>

using namespace Death::IO;
using namespace Jazz2::UI::Menu::Resources;

/** @brief @ref Death::Containers::StringView from @ref NCINE_VERSION */
#define NCINE_VERSION_s DEATH_PASTE(NCINE_VERSION, _s)

namespace Jazz2::UI::Menu
{
	static constexpr std::uint64_t CurrentVersion = parseVersion(NCINE_VERSION_s);
	constexpr std::uint64_t VersionMask = ~0xFFFFFFFFULL;

	ServerSelectSection::ServerSelectSection()
		: _selectedIndex(0), _animation(0.0f), _y(0.0f), _height(0.0f), _availableHeight(0.0f), _pressedCount(0),
		_touchTime(0.0f), _touchSpeed(0.0f), _noiseCooldown(0.0f), _discovery(this),
		_touchDirection(0), _transitionTime(0.0f), _shouldStart(false), _isConnecting(false),
		_waitForIpInput(false), _keyboardVisible(false), _ipInput(64)
#if defined(DEATH_TARGET_ANDROID)
		, _recalcVisibleBoundsTimeLeft(30.0f)
#endif
	{
#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
		_initialVisibleSize.X = _currentVisibleBounds.W;
		_initialVisibleSize.Y = _currentVisibleBounds.H;
#endif
	}

	ServerSelectSection::~ServerSelectSection()
	{
	}

	Recti ServerSelectSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
	}

	void ServerSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	void ServerSelectSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (_touchSpeed > 0.0f) {
			if (_touchStart == Vector2f::Zero && _availableHeight < _height) {
				float y = _y + (_touchSpeed * (std::int32_t)_touchDirection * TouchKineticDivider * timeMult);
				if (y < (_availableHeight - _height) && _touchDirection < 0) {
					y = (_availableHeight - _height);
					_touchDirection = 1;
					_touchSpeed *= TouchKineticDamping;
				} else if (y > 0.0f && _touchDirection > 0) {
					y = 0.0f;
					_touchDirection = -1;
					_touchSpeed *= TouchKineticDamping;
				}
				_y = y;
			}

			_touchSpeed = std::max(_touchSpeed - TouchKineticFriction * TouchKineticDivider * timeMult, 0.0f);
		}

		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		}

		if (!_shouldStart) {
			if (_waitForIpInput) {
				_ipInput.Update(timeMult);

#if defined(DEATH_TARGET_ANDROID)
				_recalcVisibleBoundsTimeLeft -= timeMult;
				if (_recalcVisibleBoundsTimeLeft <= 0.0f) {
					_recalcVisibleBoundsTimeLeft = 60.0f;
					_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
				}
#endif

				if (_root->ActionHit(PlayerAction::ChangeWeapon) && theApplication().CanShowScreenKeyboard()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					if (_keyboardVisible) {
						theApplication().HideScreenKeyboard();
						_keyboardVisible = false;
					} else {
						theApplication().ShowScreenKeyboard();
						_keyboardVisible = true;
					}
					RecalcLayoutForScreenKeyboard();
				} else if (_root->ActionHit(PlayerAction::Menu)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					if (_keyboardVisible) {
						theApplication().HideScreenKeyboard();
						_keyboardVisible = false;
						RecalcLayoutForScreenKeyboard();
					} else {
						_waitForIpInput = false;
						_ipInput.Deactivate();
					}
				} else if (_root->ActionHit(PlayerAction::Fire)) {
					if (!_ipInput.GetText().empty()) {
						_root->PlaySfx("MenuSelect"_s, 0.6f);
						_selectedServer = {};
						_selectedServer.EndpointString = _ipInput.GetText();
						theApplication().HideScreenKeyboard();
						_keyboardVisible = false;
						RecalcLayoutForScreenKeyboard();
						_transitionTime = 1.0f;
					}
				}
				return;
			}

			if (_root->ActionHit(PlayerAction::ChangeWeapon)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForIpInput = true;
				_ipInput.Activate({});
				return;
			} else if (_root->ActionHit(PlayerAction::Menu)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->LeaveSection();
				return;
			} else if (!_items.empty()) {
				if (_root->ActionHit(PlayerAction::Fire)) {
					ExecuteSelected();
				} else if (_items.size() > 1) {
					if (_root->ActionPressed(PlayerAction::Up)) {
						if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerAction::Up)) {
							if (_noiseCooldown <= 0.0f) {
								_noiseCooldown = 10.0f;
								_root->PlaySfx("MenuSelect"_s, 0.5f);
							}
							_animation = 0.0f;

							std::int32_t offset;
							if (_selectedIndex > 0) {
								_selectedIndex--;
								offset = -ItemHeight / 2;
							} else {
								_selectedIndex = (std::int32_t)(_items.size() - 1);
								offset = 0;
							}
							EnsureVisibleSelected(offset);
							_pressedCount = std::min(_pressedCount + 6, 10);
						}
					} else if (_root->ActionPressed(PlayerAction::Down)) {
						if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerAction::Down)) {
							if (_noiseCooldown <= 0.0f) {
								_noiseCooldown = 10.0f;
								_root->PlaySfx("MenuSelect"_s, 0.5f);
							}
							_animation = 0.0f;

							std::int32_t offset;
							if (_selectedIndex < _items.size() - 1) {
								_selectedIndex++;
								offset = ItemHeight / 2;
							} else {
								_selectedIndex = 0;
								offset = 0;
							}
							EnsureVisibleSelected(offset);
							_pressedCount = std::min(_pressedCount + 6, 10);
						}
					} else {
						_pressedCount = 0;
					}
				}

				_touchTime += timeMult;
			}
		} else {
			_transitionTime -= 0.025f * timeMult;
			if (_transitionTime <= 0.0f) {
				OnAfterTransition();
			}
		}
	}

	void ServerSelectSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Connect To Server"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void ServerSelectSection::OnDrawClipped(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		std::int32_t charOffset = 0;

		if (_items.empty()) {
			_root->DrawStringShadow(_("No servers found, but still searchin'!"), charOffset, centerX, contentBounds.Y + contentBounds.H * 0.33f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);
			return;
		}

		_availableHeight = (bottomLine - topLine);
		constexpr float spacing = 20.0f;

		_y = (_availableHeight < _height ? std::clamp(_y, _availableHeight - _height, 0.0f) : 0.0f);

		Vector2f center = Vector2f(centerX, topLine + 12.0f + _y);
		float column1 = contentBounds.X + (contentBounds.W >= 690 ? (contentBounds.W * 0.2f) : (contentBounds.W >= 440 ? (contentBounds.W * 0.1f) : 20.0f));
		float column2 = contentBounds.X + (contentBounds.W >= 600 ? (contentBounds.W * 0.66f) : (contentBounds.W * 0.74f));

		Colorf defaultColor = (_waitForIpInput ? Font::TransparentDefaultColor : Font::DefaultColor);
		Colorf randomColor = (_waitForIpInput ? Font::TransparentRandomColor : Font::RandomColor);

		std::size_t itemsCount = _items.size();
		for (std::int32_t i = 0; i < itemsCount; i++) {
			_items[i].Y = center.Y;

			if (center.Y > topLine - ItemHeight && center.Y < bottomLine + ItemHeight) {
				if (_selectedIndex == i) {
					float easing = IMenuContainer::EaseOutElastic(_animation);
					float size = 0.7f + easing * 0.12f;
					float xMultiplier = _root->MeasureString(_items[i].Desc.Name, size, 0.9f).X * 0.125f;
					float x = column1 + xMultiplier - easing * xMultiplier;

					_root->DrawElement(MenuGlow, 0, centerX, center.Y, IMenuContainer::MainLayer - 200, Alignment::Center,
						Colorf(1.0f, 1.0f, 1.0f, 0.2f), 28.0f, 3.0f, true, true);

					_root->DrawStringShadow(_items[i].Desc.Name, charOffset, x, center.Y, IMenuContainer::FontLayer + 10,
						Alignment::Left, randomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					_root->DrawStringShadow(_items[i].Desc.Name, charOffset, column1, center.Y, IMenuContainer::FontLayer,
						Alignment::Left, defaultColor, 0.7f);
				}

				if (_items[i].Desc.MaxPlayerCount > 0) {
					char playerCount[32];
					std::size_t length = formatInto(playerCount, "{}/{}", _items[i].Desc.CurrentPlayerCount, _items[i].Desc.MaxPlayerCount);
					_root->DrawStringShadow({ playerCount, length }, charOffset, column2 - 90.0f, center.Y, IMenuContainer::FontLayer + 10 - 2,
						Alignment::Right, defaultColor, 0.7f);
				}

				if (!_items[i].Desc.Version.empty()) {
					_root->DrawStringShadow(StringView("v"_s + _items[i].Desc.Version), charOffset, column2 - 78.0f, center.Y, IMenuContainer::FontLayer + 10 - 2,
						Alignment::Left, _items[i].Desc.IsCompatible ? defaultColor : Colorf(0.7f, 0.44f, 0.44f, _waitForIpInput ? 0.36f : 0.5f), 0.7f);
				}

				StringView firstEndpoint = _items[i].Desc.EndpointString;
				firstEndpoint = firstEndpoint.prefix(firstEndpoint.findOr('|', firstEndpoint.end()).begin());

				String firstEndpointShort;
				if (firstEndpoint.size() > 23) {
					firstEndpointShort = "<"_s + firstEndpoint.slice(firstEndpoint.size() - 23, firstEndpoint.size());
					firstEndpoint = firstEndpointShort;
				}
#if defined(WITH_WEBSOCKET)
				if (_items[i].Desc.WsPort != 0) {
					firstEndpointShort = firstEndpoint + " ws"_s;
					firstEndpoint = firstEndpointShort;
				}
#endif
				if (_items[i].Desc.Flags & 0x80000000u /*Local*/) {
					firstEndpointShort = firstEndpoint + " ^"_s;
					firstEndpoint = firstEndpointShort;
				}

				_root->DrawStringShadow(firstEndpoint, charOffset, column2, center.Y, IMenuContainer::FontLayer + 10 - 2,
					Alignment::Left, defaultColor, 0.7f);
			}

			center.Y += spacing;
		}

		_height = center.Y - (topLine + _y);

		if (_items[0].Y < TopLine + ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, topLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		if (_items[itemsCount - 1].Y > bottomLine - ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, bottomLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	void ServerSelectSection::OnDrawOverlay(Canvas* canvas)
	{
		char stringBuffer[64];
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		std::int32_t charOffset = 0;

		if (_waitForIpInput) {
			float midY = (topLine + bottomLine) * 0.5f;

			// Semi-transparent overlay to dim the server list
			_root->DrawSolid(contentBounds.X, topLine - 1.0f, IMenuContainer::MainLayer + 100, Alignment::TopLeft,
				Vector2f(contentBounds.W, bottomLine - topLine + 2.0f), Colorf(1.0f, 1.0f, 1.0f, 0.2f));

			// Input box background
			_root->DrawElement(MenuDim, centerX, midY, IMenuContainer::MainLayer + 110,
				Alignment::Center, Colorf(0.1f, 0.1f, 0.4f, 0.9f), Vector2f(440.0f, 70.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));

			// Label
			// TRANSLATORS: Label in Connect To Server > IP Address input field
			_root->DrawStringShadow(_("IP Address:"), charOffset, centerX, midY - 16.0f, IMenuContainer::FontLayer + 110,
				Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);

			// Input text or placeholder
			StringView ipText = _ipInput.GetText();
			if (ipText.empty()) {
				// TRANSLATORS: Placeholder in Connect To Server > IP Address input field
				_root->DrawStringShadow(_("<ip> or <ip>:<port>"), charOffset, centerX, midY + 6.0f, IMenuContainer::FontLayer + 110,
					Alignment::Center, Colorf(0.5f, 0.5f, 0.5f, 0.3f), 0.9f);
			} else {
				Vector2f textSize = _root->MeasureString(ipText, 0.9f);
				_root->DrawStringShadow(ipText, charOffset, centerX, midY + 6.0f, IMenuContainer::FontLayer + 110,
					Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f);

				// Cursor
				Vector2f textToCursorSize = _root->MeasureString(ipText.prefix(_ipInput.GetCursor()), 0.9f);
				_root->DrawSolid(centerX - textSize.X * 0.5f + textToCursorSize.X + 1.0f, midY + 6.0f - 1.0f,
					IMenuContainer::FontLayer + 120, Alignment::Center, Vector2f(1.0f, 14.0f),
					Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_ipInput.GetCaretAnim() * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
			}

			// Bottom hints
			float hintY = contentBounds.Y + contentBounds.H - 18.0f;

			/*if (theApplication().CanShowScreenKeyboard()) {
				_root->DrawElement(ShowKeyboard, -1, centerX - 100.0f, hintY + 2.0f, IMenuContainer::MainLayer + 110, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.2f));
				_root->DrawElement(ShowKeyboard, -1, centerX - 100.0f, hintY, IMenuContainer::MainLayer + 120, Alignment::Center, Colorf::White);
				// TRANSLATORS: Bottom hint in Connect To Server > IP Address input field
				_root->DrawStringShadow(_("Keyboard"), charOffset, centerX - 84.0f, hintY, IMenuContainer::FontLayer + 110,
					Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);
			}*/

			std::size_t length = formatInto(stringBuffer, "\f[c:#d0705d]{}\f[/c] │", _("Change Weapon"));
			_root->DrawStringShadow({ stringBuffer, length }, charOffset, centerX + 4.0f, hintY, IMenuContainer::FontLayer + 110,
				Alignment::Right, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);

			_root->DrawElement(GetResourceForButtonName(ButtonName::Y), 0, centerX + 18.0f, hintY + 2.0f, IMenuContainer::MainLayer + 110, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.16f), 0.8f, 0.8f);
			_root->DrawElement(GetResourceForButtonName(ButtonName::Y), 0, centerX + 18.0f, hintY, IMenuContainer::MainLayer + 120, Alignment::Center, Colorf::White, 0.8f, 0.8f);

			// TRANSLATORS: Bottom hint in Connect To Server > IP Address input field, prefixed with key/button to press
			_root->DrawStringShadow(_("to show keyboard"), charOffset, centerX + 32.0f, hintY, IMenuContainer::FontLayer + 110,
				Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);
		} else {
			std::size_t length = formatInto(stringBuffer, "\f[c:#d0705d]{}\f[/c] │", _("Change Weapon"));

			_root->DrawStringShadow({ stringBuffer, length }, charOffset, centerX - 15.0f, contentBounds.Y + contentBounds.H - 18.0f, IMenuContainer::FontLayer,
				Alignment::Right, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);

			_root->DrawElement(GetResourceForButtonName(ButtonName::Y), 0, centerX - 2.0f, contentBounds.Y + contentBounds.H - 18.0f + 2.0f,
				IMenuContainer::ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.16f), 0.8f, 0.8f);
			_root->DrawElement(GetResourceForButtonName(ButtonName::Y), 0, centerX - 2.0f, contentBounds.Y + contentBounds.H - 18.0f,
				IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 0.8f, 0.8f);

			// TRANSLATORS: Bottom hint in Connect To Server section, prefixed with key/button to press
			_root->DrawStringShadow(_("to connect to IP address"), charOffset, centerX + 8.0f, contentBounds.Y + contentBounds.H - 18.0f, IMenuContainer::FontLayer,
				Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);
		}

#if !defined(DEATH_TARGET_ANDROID)
		if (_waitForIpInput && _keyboardVisible) {
			auto contentBounds2 = _root->GetContentBounds();
			float titleY = contentBounds2.Y - (canvas->ViewSize.Y >= 300 ? 30.0f : 12.0f) - 2.0f;

			// Dark overlay covering the whole screen
			_root->DrawSolid(0.0f, 0.0f, IMenuContainer::MainLayer - 10, Alignment::TopLeft,
				Vector2f(canvas->ViewSize.X, canvas->ViewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

			// Show input text at the top of the screen, above the keyboard
			StringView ipText2 = _ipInput.GetText();
			_root->DrawStringShadow(ipText2.empty() ? StringView("<ip> or <ip>:<port>"_s) : ipText2, charOffset, 120.0f, titleY, IMenuContainer::MainLayer,
				Alignment::Left, ipText2.empty() ? Colorf(0.5f, 0.5f, 0.5f, 0.3f) : Colorf(0.62f, 0.44f, 0.34f, 0.5f), 1.0f);

			if (!ipText2.empty()) {
				Vector2f textToCursorSize = _root->MeasureString(ipText2.prefix(_ipInput.GetCursor()), 1.0f);
				_root->DrawSolid(120.0f + textToCursorSize.X + 1.0f, titleY - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Left, Vector2f(1.0f, 14.0f),
					Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_ipInput.GetCaretAnim() * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
			}
		}
#endif

#if defined(DEATH_TARGET_ANDROID)
		if (_waitForIpInput && (_currentVisibleBounds.W < _initialVisibleSize.X || _currentVisibleBounds.H < _initialVisibleSize.Y)) {
			Vector2i viewSizeLocal = _root->GetViewSize();
			if (_currentVisibleBounds.Y * viewSizeLocal.Y / _initialVisibleSize.Y < 32.0f) {
				float titleY = contentBounds.Y - (canvas->ViewSize.Y >= 300 ? 30.0f : 12.0f) - 2.0f;

				_root->DrawSolid(0.0f, 0.0f, IMenuContainer::MainLayer - 10, Alignment::TopLeft,
					Vector2f(canvas->ViewSize.X, canvas->ViewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

				StringView ipText2 = _ipInput.GetText();
				_root->DrawStringShadow(ipText2.empty() ? StringView("<ip> or <ip>:<port>"_s) : ipText2, charOffset, 120.0f, titleY, IMenuContainer::MainLayer,
					Alignment::Left, ipText2.empty() ? Colorf(0.5f, 0.5f, 0.5f, 0.3f) : Colorf(0.62f, 0.44f, 0.34f, 0.5f), 1.0f);

				if (!ipText2.empty()) {
					Vector2f textToCursorSize = _root->MeasureString(ipText2.prefix(_ipInput.GetCursor()), 1.0f);
					_root->DrawSolid(120.0f + textToCursorSize.X + 1.0f, titleY - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Left, Vector2f(1.0f, 14.0f),
						Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_ipInput.GetCaretAnim() * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
				}
			}
		}
#endif

		if (_shouldStart) {
			auto command = canvas->RentRenderCommand();
			if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::Transition))) {
				command->GetMaterial().ReserveUniformsDataMemory();
				command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			}

			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(Vector4f(1.0f, 0.0f, 1.0f, 0.0f).Data());
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(Vector2f(static_cast<float>(canvas->ViewSize.X), static_cast<float>(canvas->ViewSize.Y)).Data());
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(0.0f, 0.0f, 0.0f, _transitionTime).Data());

			command->SetTransformation(Matrix4x4f::Identity);
			command->SetLayer(999);

			canvas->DrawRenderCommand(command);
		}
	}

	NavigationFlags ServerSelectSection::GetNavigationFlags() const
	{
		return (_waitForIpInput ? NavigationFlags::AllowGamepads : NavigationFlags::AllowAll);
	}

	void ServerSelectSection::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_waitForIpInput) {
			switch (event.sym) {
				case Keys::Escape: {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					if (_keyboardVisible) {
						theApplication().HideScreenKeyboard();
						_keyboardVisible = false;
						RecalcLayoutForScreenKeyboard();
					} else {
						_waitForIpInput = false;
						_ipInput.Deactivate();
					}
					break;
				}
				case Keys::Return: {
					if (!_ipInput.GetText().empty()) {
						_root->PlaySfx("MenuSelect"_s, 0.6f);
						_selectedServer = {};
						_selectedServer.EndpointString = _ipInput.GetText();
						theApplication().HideScreenKeyboard();
						_keyboardVisible = false;
						RecalcLayoutForScreenKeyboard();
						_waitForIpInput = false;
						_ipInput.Deactivate();
						_shouldStart = true;
						_transitionTime = 1.0f;
					}
					break;
				}
				default: {
					_ipInput.OnKeyPressed(event);
					break;
				}
			}
		}
	}

	void ServerSelectSection::OnTextInput(const TextInputEvent& event)
	{
		if (_waitForIpInput) {
			_ipInput.OnTextInput(event);
		}
	}

	void ServerSelectSection::OnTouchEvent(const TouchEvent& event, Vector2i viewSize)
	{
		if (_shouldStart) {
			return;
		}

		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
					if (y < 80.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						if (_waitForIpInput) {
							if (_keyboardVisible) {
								theApplication().HideScreenKeyboard();
								_keyboardVisible = false;
								RecalcLayoutForScreenKeyboard();
							} else {
								_waitForIpInput = false;
								_ipInput.Deactivate();
							}
						} else {
							_root->LeaveSection();
						}
						return;
					}

					_touchStart = Vector2f(event.pointers[pointerIndex].x * viewSize.X, y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * viewSize.X, event.pointers[pointerIndex].y * viewSize.Y);
						if (!_waitForIpInput && _availableHeight < _height) {
							float delta = touchMove.Y - _touchLast.Y;
							if (delta != 0.0f) {
								_y += delta;
								if (delta < -0.1f && _touchDirection >= 0) {
									_touchDirection = -1;
									_touchSpeed = 0.0f;
								} else if (delta > 0.1f && _touchDirection <= 0) {
									_touchDirection = 1;
									_touchSpeed = 0.0f;
								}
								_touchSpeed = (0.8f * _touchSpeed) + (0.2f * std::abs(delta) / TouchKineticDivider);
							}
						}
						_touchLast = touchMove;
					}
				}
				break;
			}
			case TouchEventType::Up: {
				bool alreadyMoved = (_touchStart == Vector2f::Zero || (_touchStart - _touchLast).Length() > 10.0f || _touchTime > FrameTimer::FramesPerSecond);
				_touchStart = Vector2f::Zero;
				if (alreadyMoved) {
					return;
				}

				Recti contentBounds = _root->GetContentBounds();
				float hintY = contentBounds.Y + contentBounds.H - 18.0f;

				if (_waitForIpInput) {
					// Keyboard toggle hint tap
					if (theApplication().CanShowScreenKeyboard() && std::abs(_touchLast.Y - hintY) < 20.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						if (_keyboardVisible) {
							theApplication().HideScreenKeyboard();
							_keyboardVisible = false;
						} else {
							theApplication().ShowScreenKeyboard();
							_keyboardVisible = true;
						}
						RecalcLayoutForScreenKeyboard();
					}
					return;
				}

				// Connect to IP hint tap
				if (std::abs(_touchLast.Y - hintY) < 20.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForIpInput = true;
					_ipInput.Activate({});
					return;
				}

				float halfW = viewSize.X * 0.5f;
				std::size_t itemsCount = _items.size();
				for (std::int32_t i = 0; i < itemsCount; i++) {
					if (std::abs(_touchLast.X - halfW) < 200.0f && std::abs(_touchLast.Y - _items[i].Y) < 22.0f) {
						if (_selectedIndex == i) {
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
							EnsureVisibleSelected();
						}
						break;
					}
				}
				break;
			}
		}
	}

	void ServerSelectSection::OnServerFound(Jazz2::Multiplayer::ServerDescription&& desc)
	{
		std::uint64_t serverVersion = parseVersion(desc.Version);
		desc.IsCompatible = ((serverVersion & VersionMask) == (CurrentVersion & VersionMask));

#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		// On Emscripten (WS-only client), mark servers without WebSocket support as unavailable
		if (desc.WsPort == 0) {
			desc.IsCompatible = false;
		}
#endif

		for (auto& item : _items) {
			if (item.Desc.EndpointString == desc.EndpointString) {
				std::uint32_t prevFlags = (item.Desc.Flags & 0x80000000u /*Local*/);
				item.Desc = std::move(desc);
				item.Desc.Flags |= prevFlags;
				return;
			}
		}

		_items.push_back(std::move(desc));
	}

	void ServerSelectSection::ExecuteSelected()
	{
		if (_items.empty()) {
			return;
		}

#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		// On Emscripten, only WebSocket-capable servers can be joined
		if (_items[_selectedIndex].Desc.WsPort == 0) {
			return;
		}
#endif

		_root->PlaySfx("MenuSelect"_s, 0.6f);

		auto& selectedItem = _items[_selectedIndex];
		_selectedServer = selectedItem.Desc;
		_shouldStart = true;
		_transitionTime = 1.0f;
	}

	void ServerSelectSection::OnAfterTransition()
	{
		if (_isConnecting) {
			return;
		}

		_isConnecting = true;

#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		// On Emscripten, always connect via WebSocket
		StringView firstEndpoint = _selectedServer.EndpointString;
		firstEndpoint = firstEndpoint.prefix(firstEndpoint.findOr('|', firstEndpoint.end()).begin());

		String wsUrl;
		if (_selectedServer.WsPort != 0) {
			// Server from list: construct ws[s]://host:WsPort from the ENet endpoint
			StringView host; std::uint16_t enetPort;
			if (Jazz2::Multiplayer::NetworkManagerBase::TrySplitAddressAndPort(firstEndpoint, host, enetPort)) {
				StringView protocol = (_selectedServer.WsSecure ? "wss://"_s : "ws://"_s);
				char portBuf[8];
				std::size_t portLen = formatInto(portBuf, "{}", (int)_selectedServer.WsPort);
				wsUrl = protocol + host + ":"_s + StringView(portBuf, portLen);
			}
		}
		if (wsUrl.empty()) {
			// Manual IP entry or fallback: wrap in ws:// if not already a WebSocket URL
			if (!firstEndpoint.hasPrefix("ws://"_s) && !firstEndpoint.hasPrefix("wss://"_s)) {
				wsUrl = "wss://"_s + firstEndpoint;
			} else {
				wsUrl = String(firstEndpoint);
			}
		}
		_root->ConnectToServer(wsUrl, 0);
#else
		_root->ConnectToServer(_selectedServer.EndpointString, 0);
#endif
	}

	void ServerSelectSection::EnsureVisibleSelected(std::int32_t offset)
	{
		Recti contentBounds = _root->GetContentBounds();
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		std::int32_t y = _items[_selectedIndex].Y + offset;
		if (y < topLine + ItemHeight * 0.5f) {
			_y += (topLine + ItemHeight * 0.5f - y);
		} else if (y > bottomLine - ItemHeight * 0.5f) {
			_y += (bottomLine - ItemHeight * 0.5f - y);
		}
	}

	ServerSelectSection::ItemData::ItemData(Jazz2::Multiplayer::ServerDescription&& desc)
		: Desc(std::move(desc))
	{
	}

	void ServerSelectSection::RecalcLayoutForScreenKeyboard()
	{
#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();

		if (_recalcVisibleBoundsTimeLeft > 30.0f) {
			_recalcVisibleBoundsTimeLeft = 30.0f;
		}
#endif
	}
}

#endif