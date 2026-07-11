#include "HUD.h"
#include "InGameConsole.h"
#include "Menu/Tweening.h"
#include "../ContentResolver.h"
#include "../PreferencesCache.h"
#include "../Actors/Enemies/Bosses/BossBase.h"

#if defined(WITH_ANGELSCRIPT)
#	include "../Scripting/LevelScriptLoader.h"
#endif

#include "../../nCine/Application.h"
#include "../../nCine/I18n.h"
#include "../../nCine/Graphics/RenderQueue.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../../nCine/Backends/Android/AndroidApplication.h"
#endif

// Position of key in 22x6 grid
static const std::uint8_t KeyLayout[] = {
	0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17,
	22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 38, 39, 40, 41, 42, 43,
	44, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
	66, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 84, 85, 86,
	88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 101, 104, 106, 107, 108, 109,
	110, 111, 112, 116, 120, 121, 122, 123, 125, 126, 127, 128, 130
};

namespace Jazz2::UI
{
	namespace Resources
	{
		static constexpr AnimState WeaponBlasterJazz = (AnimState)0;
		static constexpr AnimState WeaponBlasterSpaz = (AnimState)1;
		static constexpr AnimState WeaponBlasterLori = (AnimState)2;
		static constexpr AnimState WeaponBouncer = (AnimState)3;
		static constexpr AnimState WeaponFreezer = (AnimState)4;
		static constexpr AnimState WeaponSeeker = (AnimState)5;
		static constexpr AnimState WeaponRF = (AnimState)6;
		static constexpr AnimState WeaponToaster = (AnimState)7;
		static constexpr AnimState WeaponTNT = (AnimState)8;
		static constexpr AnimState WeaponPepper = (AnimState)9;
		static constexpr AnimState WeaponElectro = (AnimState)10;
		static constexpr AnimState WeaponThunderbolt = (AnimState)11;
		static constexpr AnimState WeaponPowerUpBlasterJazz = (AnimState)20;
		static constexpr AnimState WeaponPowerUpBlasterSpaz = (AnimState)21;
		static constexpr AnimState WeaponPowerUpBlasterLori = (AnimState)22;
		static constexpr AnimState WeaponPowerUpBouncer = (AnimState)23;
		static constexpr AnimState WeaponPowerUpFreezer = (AnimState)24;
		static constexpr AnimState WeaponPowerUpSeeker = (AnimState)25;
		static constexpr AnimState WeaponPowerUpRF = (AnimState)26;
		static constexpr AnimState WeaponPowerUpToaster = (AnimState)27;
		static constexpr AnimState WeaponPowerUpTNT = (AnimState)28;
		static constexpr AnimState WeaponPowerUpPepper = (AnimState)29;
		static constexpr AnimState WeaponPowerUpElectro = (AnimState)30;
		static constexpr AnimState WeaponPowerUpThunderbolt = (AnimState)31;
		static constexpr AnimState WeaponToasterDisabled = (AnimState)47;
		static constexpr AnimState CharacterJazz = (AnimState)60;
		static constexpr AnimState CharacterSpaz = (AnimState)61;
		static constexpr AnimState CharacterLori = (AnimState)62;
		static constexpr AnimState CharacterFrog = (AnimState)63;
		static constexpr AnimState Heart = (AnimState)70;
		static constexpr AnimState PickupGemRed = (AnimState)71;
		static constexpr AnimState PickupGemGreen = (AnimState)72;
		static constexpr AnimState PickupGemBlue = (AnimState)73;
		static constexpr AnimState PickupGemPurple = (AnimState)74;
		static constexpr AnimState PickupCoin = (AnimState)75;
		static constexpr AnimState PickupFood = (AnimState)76;
		static constexpr AnimState PickupCarrot = (AnimState)77;
		static constexpr AnimState PickupStopwatch = (AnimState)78;
		static constexpr AnimState BossHealthBar = (AnimState)79;
		static constexpr AnimState WeaponWheel = (AnimState)80;
		static constexpr AnimState WeaponWheelInner = (AnimState)81;
		static constexpr AnimState WeaponWheelDim = (AnimState)82;
		static constexpr AnimState TouchDpad = (AnimState)100;
		static constexpr AnimState TouchFire = (AnimState)101;
		static constexpr AnimState TouchJump = (AnimState)102;
		static constexpr AnimState TouchRun = (AnimState)103;
		static constexpr AnimState TouchChange = (AnimState)104;
		static constexpr AnimState TouchPause = (AnimState)105;
		static constexpr AnimState TouchClose = (AnimState)106;
	}

	using namespace Jazz2::UI::Resources;

	HUD::HUD(LevelHandler* levelHandler)
		: _levelHandler(levelHandler), _metadata(nullptr), _metadataIndexed(nullptr), _levelTextTime(-1.0f), _coins(0), _gems(0), _coinsTime(-1.0f), _gemsTime(-1.0f),
			_activeBossTime(0.0f), _touchButtonsTimer(0.0f), _rgbAmbientLight(0.0f), _rgbHealthLast(0.0f), _rgbLightsAnim(0.0f),
			_rgbLightsTime(0.0f), _transitionState(TransitionState::WaitingForFadeIn), _transitionTime(1.0f),
			_joystickActive(false), _joystickOrigin(0.0f, 0.0f), _joystickCurrent(0.0f, 0.0f),
			_joystickPointerId(-1), _joystickMaxRadius(60.0f)
	{
		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/HUD"_s);
		_smallFont = resolver.GetFont(FontType::Small);

		RefreshTouchButtons();
	}

	HUD::~HUD()
	{
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (PreferencesCache::EnableRgbLights) {
			RgbLights::Get().Clear();
		}
#endif
	}

	void HUD::OnUpdate(float timeMult)
	{
		Canvas::OnUpdate(timeMult);

		if (_levelTextTime >= 0.0f) {
			_levelTextTime += timeMult;
		}
		if (_touchButtonsTimer > 0.0f) {
			_touchButtonsTimer -= timeMult;
		}

		switch (_transitionState) {
			case TransitionState::FadeIn:
				_transitionTime += 0.025f * timeMult;
				if (_transitionTime >= 1.0f) {
					_transitionState = TransitionState::None;
				}
				break;
			case TransitionState::FadeOut:
				if (_transitionTime > 0.0f) {
					_transitionTime -= 0.025f * timeMult;
					if (_transitionTime < 0.0f) {
						_transitionTime = 0.0f;
					}
				}
				break;
			case TransitionState::WaitingForFadeOut:
				_transitionTime -= timeMult;
				if (_transitionTime <= 0.0f) {
					_transitionState = TransitionState::FadeOut;
					_transitionTime = 1.0f;
				}
				break;
		}

		if (!_levelHandler->_assignedViewports.empty()) {
			if (_coinsTime >= 0.0f) {
				_coinsTime += timeMult;
			}
			if (_gemsTime >= 0.0f) {
				_gemsTime += timeMult;
			}
			if (_levelHandler->_activeBoss != nullptr) {
				_activeBossTime += timeMult;

				constexpr float TransitionTime = 60.0f;
				if (_activeBossTime > TransitionTime) {
					_activeBossTime = TransitionTime;
				}
			} else {
				_activeBossTime = 0.0f;
			}

			UpdateWeaponWheel(timeMult);
			UpdateRgbLights(timeMult, _levelHandler->_assignedViewports[0].get());
		}
	}

	bool HUD::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		if (_metadata == nullptr) {
			return false;
		}

		ViewSize = _levelHandler->GetViewSize();

		Rectf view = Rectf(0.0f, 0.0f, static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y));
		Rectf adjustedView = view;
#if defined(DEATH_TARGET_ANDROID)
		if (static_cast<AndroidApplication&>(theApplication()).IsScreenRound()) {
			float newWidth = roundf(adjustedView.W * 0.5f);
			adjustedView.X += roundf((adjustedView.W - newWidth) * 0.5f);
			adjustedView.W = newWidth;
			adjustedView.H = roundf(adjustedView.H * 0.95f);
		} else
#endif
		if (_touchButtonsTimer > 0.0f) {
			const auto& dpadLayout = PreferencesCache::TouchButtons[(std::size_t)TouchButtonSlot::Dpad];
			float leftMargin = dpadLayout.EdgeOffset.X + DpadSize * DefaultRef * dpadLayout.Scale + 8.0f;
			float rightMargin = 0.0f;
			for (TouchButtonSlot slot : { TouchButtonSlot::Fire, TouchButtonSlot::Jump, TouchButtonSlot::Run }) {
				const auto& sl = PreferencesCache::TouchButtons[(std::size_t)slot];
				if (sl.Anchor == TouchButtonAnchor::BottomRight || sl.Anchor == TouchButtonAnchor::TopRight) {
					rightMargin = std::max(rightMargin, sl.EdgeOffset.X + ButtonSize * DefaultRef * sl.Scale + 8.0f);
				}
			}
			adjustedView.X = leftMargin;
			adjustedView.W = adjustedView.W - adjustedView.X - rightMargin;
		}

		std::int32_t charOffset = 0;
		char stringBuffer[32];

		auto players = _levelHandler->GetPlayers();
		
		for (std::size_t i = 0; i < _levelHandler->_assignedViewports.size(); i++) {
			auto& viewport = _levelHandler->_assignedViewports[i];
			if (auto* player = runtime_cast<Actors::Player>(viewport->GetTargetActor())) {
				Rectf scopedView = viewport->GetBounds();
				Rectf adjustedScopedView = scopedView;
				float left = std::max(adjustedScopedView.X, adjustedView.X);
				float right = std::min(adjustedScopedView.X + adjustedScopedView.W, adjustedView.X + adjustedView.W);
				adjustedScopedView.X = left;
				adjustedScopedView.W = right - left;

#if defined(DEATH_TARGET_ANDROID)
				if (static_cast<AndroidApplication&>(theApplication()).IsScreenRound() && _levelHandler->_assignedViewports.size() == 1) {
					adjustedScopedView.H = roundf(adjustedScopedView.H * 0.95f);
				}
#endif

				OnDrawOverview(scopedView, adjustedScopedView, player);
				
				if DEATH_UNLIKELY(player->_playerType != PlayerType::Spectate) {
					OnDrawHealth(scopedView, adjustedScopedView, player);
					OnDrawScore(scopedView, player);
					OnDrawWeaponAmmo(adjustedScopedView, player);

					DrawWeaponWheel(scopedView, player);
				}
			}
		}

		DrawViewportSeparators();
		OnDrawCoins(view, charOffset);
		OnDrawGems(view, charOffset);
		OnDrawActiveBoss(adjustedView);
		OnDrawLevelText(charOffset);

#if defined(DEATH_DEBUG)
		/*if (PreferencesCache::ShowPerformanceMetrics && !_levelHandler->_assignedViewports.empty()) {
			auto& viewport = _levelHandler->_assignedViewports[0];
			for (const auto& actor : _levelHandler->_actors) {
				DrawSolid(actor->GetPos() - actor->AABB.GetExtents() - viewport->_cameraPos + viewport->GetBounds().GetSize() * 0.5f, 10,
					actor->AABB.GetExtents() * 2.0f, Colorf(0.0f, 0.0f, 0.0f, 0.3f), false);
				DrawSolid(actor->GetPos() - actor->AABBInner.GetExtents() - viewport->_cameraPos + viewport->GetBounds().GetSize() * 0.5f, 12,
					actor->AABBInner.GetExtents() * 2.0f, Colorf(1.0f, 1.0f, 1.0f, 0.3f), true);
			}
		}*/
#endif

		// Touch Controls
		if (_touchButtonsTimer > 0.0f) {
			auto* controlledPlayer = (!players.empty() ? players[0] : nullptr);
			OnDrawTouchButtons(controlledPlayer);
		}

		// Performance Metrics
		if (PreferencesCache::ShowPerformanceMetrics) {
			i32tos((std::int32_t)std::round(theApplication().GetFrameTimer().GetAverageFps()), stringBuffer);
#if defined(DEATH_TARGET_ANDROID)
			if (static_cast<AndroidApplication&>(theApplication()).IsScreenRound()) {
				_smallFont->DrawString(this, stringBuffer, charOffset, view.W / 2 + 40.0f, view.Y + 6.0f, FontLayer,
					Alignment::TopRight, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);
			} else
#endif
			_smallFont->DrawString(this, stringBuffer, charOffset, view.W - 4.0f, view.Y + 1.0f, FontLayer,
				Alignment::TopRight, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);
		}

		if (_transitionState >= TransitionState::WaitingForFadeIn && _transitionState <= TransitionState::FadeOut) {
			auto command = RentRenderCommand();
			if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::Transition))) {
				command->GetMaterial().ReserveUniformsDataMemory();
				command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			}

			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

			auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(Vector4f(1.0f, 0.0f, 1.0f, 0.0f).Data());
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y)).Data());
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(0.0f, 0.0f, 0.0f, _transitionTime).Data());

			command->SetTransformation(Matrix4x4f::Identity);
			command->SetLayer(600);

			renderQueue.AddCommand(command);
		}

		return true;
	}

	void HUD::OnTouchEvent(const TouchEvent& event, uint32_t& overrideActions, Vector2f& overrideMovement)
	{
		_touchButtonsTimer = 1200.0f;

		constexpr std::uint32_t DpadMask = (1 << (std::int32_t)PlayerAction::Left)
			| (1 << (std::int32_t)PlayerAction::Right)
			| (1 << (std::int32_t)PlayerAction::Up)
			| (1 << (std::int32_t)PlayerAction::Down)
			| (1 << (std::int32_t)PlayerAction::Buttstomp);

		if (event.type == TouchEventType::Down || event.type == TouchEventType::PointerDown) {
			int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x * (float)ViewSize.X;
				float y = event.pointers[pointerIndex].y * (float)ViewSize.Y;

				if (PreferencesCache::EnableTouchJoystick && _joystickPointerId == -1) {
					// Floating joystick: activate anywhere in the left portion / D-pad zone
					if (x < ViewSize.X * 0.5f && y > ViewSize.Y * 0.2f) {
						_joystickActive = true;
						_joystickOrigin = Vector2f(x, y);
						_joystickCurrent = Vector2f(x, y);
						_joystickPointerId = event.actionIndex;
					}
				}

				for (std::uint32_t i = 0; i < TouchButtonsCount; i++) {
					auto& button = _touchButtons[i];
					if (PreferencesCache::EnableTouchJoystick && i <= 4) {
						continue;  // D-pad managed by joystick
					} 
					if (button.Action != PlayerAction::None) {
						if (button.CurrentPointerId == -1 && IsOnButton(button, x, y)) {
							button.CurrentPointerId = event.actionIndex;
							overrideActions |= (1 << (std::int32_t)button.Action);
							if (button.Action == PlayerAction::Down) {
								overrideActions |= (1 << (std::int32_t)PlayerAction::Buttstomp);
							}
#if defined(NCINE_HAS_VIBRATIONS)
							if (PreferencesCache::EnableTouchVibration) {
								theApplication().Vibrate(12);
							}
#endif
						}
					}
				}
			}
		} else if (event.type == TouchEventType::Move) {
			if (PreferencesCache::EnableTouchJoystick && _joystickActive) {
				std::int32_t pointerIndex = event.findPointerIndex(_joystickPointerId);
				if (pointerIndex != -1) {
					_joystickCurrent = Vector2f(
						event.pointers[pointerIndex].x * (float)ViewSize.X,
						event.pointers[pointerIndex].y * (float)ViewSize.Y);

					Vector2f diff = _joystickCurrent - _joystickOrigin;
					float dist = diff.Length();
					Vector2f dir = (dist > 1.0f ? diff / dist : Vector2f(0.0f, 0.0f));
					float magnitude = std::min(dist / _joystickMaxRadius, 1.0f);
					Vector2f movement = dir * magnitude;

					overrideMovement = movement;
					overrideActions &= ~DpadMask;

					if (movement.X < -0.2f) overrideActions |= (1 << (std::int32_t)PlayerAction::Left);
					else if (movement.X > 0.2f) overrideActions |= (1 << (std::int32_t)PlayerAction::Right);
					
					if (movement.Y < -0.6f) overrideActions |= (1 << (std::int32_t)PlayerAction::Up);
					else if (movement.Y > 0.6f) {
						overrideActions |= (1 << (std::int32_t)PlayerAction::Down);
						if (movement.Y > 0.75f) {
							overrideActions |= (1 << (std::int32_t)PlayerAction::Buttstomp);
						}
					}
				}
			}

			for (std::uint32_t i = 0; i < TouchButtonsCount; i++) {
				auto& button = _touchButtons[i];
				if (PreferencesCache::EnableTouchJoystick && i <= 4) {
					continue;
				}
				if (button.Action != PlayerAction::None) {
					if (button.CurrentPointerId != -1) {
						bool isPressed = false;
						std::int32_t pointerIndex = event.findPointerIndex(button.CurrentPointerId);
						if (pointerIndex != -1) {
							float x = event.pointers[pointerIndex].x * (float)ViewSize.X;
							float y = event.pointers[pointerIndex].y * (float)ViewSize.Y;
							isPressed = IsOnButton(button, x, y);
						}

						if (!isPressed) {
							button.CurrentPointerId = -1;
							overrideActions &= ~(1 << (std::int32_t)button.Action);
							if (button.Action == PlayerAction::Down) {
								overrideActions &= ~(1 << (std::int32_t)PlayerAction::Buttstomp);
							}
						}
					} else {
						auto players = _levelHandler->GetPlayers();
						bool canPlayerMoveVertically = (!players.empty() && players[0]->CanMoveVertically());
						if ((button.Align & AllowRollover) != AllowRollover && !canPlayerMoveVertically) continue;

						for (std::uint32_t j = 0; j < event.count; j++) {
							float x = event.pointers[j].x * (float)ViewSize.X;
							float y = event.pointers[j].y * (float)ViewSize.Y;
							if (IsOnButton(button, x, y)) {
								button.CurrentPointerId = event.pointers[j].id;
								overrideActions |= (1 << (std::int32_t)button.Action);
								if (button.Action == PlayerAction::Down) {
									overrideActions |= (1 << (std::int32_t)PlayerAction::Buttstomp);
								}
								break;
							}
						}
					}
				}
			}
		} else if (event.type == TouchEventType::Up) {
			if (PreferencesCache::EnableTouchJoystick && _joystickActive) {
				_joystickActive = false;
				_joystickPointerId = -1;
				overrideMovement = Vector2f(0.0f, 0.0f);
				overrideActions &= ~DpadMask;
			}

			for (std::uint32_t i = 0; i < TouchButtonsCount; i++) {
				auto& button = _touchButtons[i];
				if (PreferencesCache::EnableTouchJoystick && i <= 4) {
					continue;
				}
				if (button.CurrentPointerId != -1) {
					button.CurrentPointerId = -1;
					overrideActions &= ~(1 << (std::int32_t)button.Action);
					if (button.Action == PlayerAction::Down) {
						overrideActions &= ~(1 << (std::int32_t)PlayerAction::Buttstomp);
					}
				}
			}

		} else if (event.type == TouchEventType::PointerUp) {
			if (PreferencesCache::EnableTouchJoystick && _joystickActive && event.actionIndex == _joystickPointerId) {
				_joystickActive = false;
				_joystickPointerId = -1;
				overrideMovement = Vector2f(0.0f, 0.0f);
				overrideActions &= ~DpadMask;
			}

			for (std::uint32_t i = 0; i < TouchButtonsCount; i++) {
				auto& button = _touchButtons[i];
				if (PreferencesCache::EnableTouchJoystick && i <= 4) {
					continue;
				}
				if (button.CurrentPointerId == event.actionIndex) {
					button.CurrentPointerId = -1;
					overrideActions &= ~(1 << (std::int32_t)button.Action);
					if (button.Action == PlayerAction::Down) {
						overrideActions &= ~(1 << (std::int32_t)PlayerAction::Buttstomp);
					}
				}
			}
		}
	}

	void HUD::ShowLevelText(StringView text)
	{
		if (_levelText == text || text.empty()) {
			return;
		}

		_levelText = text;
		_levelTextTime = 0.0f;
	}

	void HUD::ShowCoins(std::int32_t count)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;

		_coins = count;

		if (_coinsTime < 0.0f) {
			_coinsTime = 0.0f;
		} else if (_coinsTime > TransitionTime) {
			_coinsTime = TransitionTime;
		}

		if (_gemsTime >= 0.0f) {
			if (_gemsTime <= TransitionTime + StillTime) {
				_gemsTime = TransitionTime + StillTime;
			} else {
				_gemsTime = -1.0f;
			}
		}
	}

	void HUD::ShowGems(std::uint8_t gemType, std::int32_t count)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;

		_gems = count;
		_gemsLastType = gemType;

		if (_gemsTime < 0.0f) {
			_gemsTime = 0.0f;
		} else if (_gemsTime > TransitionTime) {
			_gemsTime = TransitionTime;
		}

		if (_coinsTime >= 0.0f) {
			if (_coinsTime <= TransitionTime + StillTime) {
				_coinsTime = TransitionTime + StillTime;
			} else {
				_coinsTime = -1.0f;
			}
		}
	}

	void HUD::BeginFadeIn(bool skip)
	{
		_transitionState = TransitionState::FadeIn;
		_transitionTime = 0.0f;
	}

	void HUD::BeginFadeOut(float delay)
	{
		if (delay <= 0.0f) {
			_transitionState = TransitionState::FadeOut;
			_transitionTime = 1.0f;
		} else {
			_transitionState = TransitionState::WaitingForFadeOut;
			_transitionTime = delay;
		}
	}

	bool HUD::IsWeaponWheelVisible(std::int32_t playerIndex) const
	{
		return (_weaponWheel[playerIndex].Anim > 0.0f);
	}

	void HUD::OnDrawOverview(const Rectf& view, const Rectf& adjustedView, Actors::Player* player)
	{
		std::int32_t charOffset = 0, charOffsetShadow = 0;

		if DEATH_UNLIKELY(player->_playerType == PlayerType::Spectate) {
			auto spectateText = _("Spectating");
			_smallFont->DrawString(this, spectateText, charOffsetShadow, view.X + 10.0f, view.Y + 6.0f + 2.0f, FontShadowLayer,
							Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.7f, 0.7f, 0.7f, 0.3f, 0.9f);
			_smallFont->DrawString(this, spectateText, charOffset, view.X + 10.0f, view.Y + 6.0f, FontLayer,
							Alignment::TopLeft, Colorf(0.45f, 0.45f, 0.45f), 0.8f, 0.7f, 0.7f, 0.7f, 0.3f, 0.9f);
		}
	}

	void HUD::OnDrawHealth(const Rectf& view, const Rectf& adjustedView, Actors::Player* player)
	{
		float bottom = adjustedView.Y + adjustedView.H;

		AnimState playerIcon;
		switch (player->_playerType) {
			default:
			case PlayerType::Jazz: playerIcon = CharacterJazz; break;
			case PlayerType::Spaz: playerIcon = CharacterSpaz; break;
			case PlayerType::Lori: playerIcon = CharacterLori; break;
			case PlayerType::Frog: playerIcon = CharacterFrog; break;
		}

		std::int32_t health = player->GetHealth();
		std::int32_t lives = player->GetLives();

#if defined(WITH_ANGELSCRIPT)
		bool shouldDrawHealth = (health < 32 && (_levelHandler->_scripts == nullptr || !_levelHandler->_scripts->OnDraw(this, player, view, Scripting::DrawType::Health)));
		bool shouldDrawLives = (_levelHandler->_scripts == nullptr || !_levelHandler->_scripts->OnDraw(this, player, view, Scripting::DrawType::Lives));
#else
		bool shouldDrawHealth = (health < 32);
		constexpr bool shouldDrawLives = true;
#endif
		if (shouldDrawLives) {
			DrawElement(playerIcon, -1, adjustedView.X + 38.0f, bottom - 1.0f + 1.6f, ShadowLayer, Alignment::BottomRight, Colorf(0.0f, 0.0f, 0.0f, 0.4f));

			// Recolor the character icon to match the player when it's being recolored; fall back to the plain icon
			std::int32_t paletteOffset = player->GetPaletteOffset();
			bool drawn = false;
			if (paletteOffset >= 0) {
				if (_metadataIndexed == nullptr) {
					_metadataIndexed = ContentResolver::Get().RequestMetadata("UI/HUD"_s, true);
				}
				drawn = DrawElementWithPalette(playerIcon, -1, adjustedView.X + 38.0f, bottom - 1.0f, MainLayer, Alignment::BottomRight, Colorf::White, (float)paletteOffset);
			}
			if (!drawn) {
				DrawElement(playerIcon, -1, adjustedView.X + 38.0f, bottom - 1.0f, MainLayer, Alignment::BottomRight, Colorf::White);
			}
		}

		char stringBuffer[32];
		std::int32_t charOffset = 0;
		std::int32_t charOffsetShadow = 0;

		// If drawing of lives if overriden, fallback to legacy HUD
		if (PreferencesCache::EnableReforgedHUD && shouldDrawLives) {
			if (lives > 0) {
				if (shouldDrawHealth) {
					DrawHealthCarrots(adjustedView.X + 24.0f, bottom - 30.0f, health);
				}

				if (shouldDrawLives) {
					if (lives < UINT8_MAX) {
						stringBuffer[0] = 'x';
						i32tos(lives, stringBuffer + 1);

						_smallFont->DrawString(this, stringBuffer, charOffsetShadow, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f + 1.0f, FontShadowLayer,
							Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f));
						_smallFont->DrawString(this, stringBuffer, charOffset, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f, FontLayer,
							Alignment::BottomLeft, Font::DefaultColor);
					} else {
						_smallFont->DrawString(this, "x\u221E", charOffsetShadow, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f + 1.0f, FontShadowLayer,
							Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f));
						_smallFont->DrawString(this, "x\u221E", charOffset, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f, FontLayer,
							Alignment::BottomLeft, Font::DefaultColor);
					}
				}
			} else {
				if (shouldDrawHealth) {
					DrawHealthCarrots(adjustedView.X + 24.0f, bottom - 20.0f, health);
				}
			}

			if (player->_activeShield != ShieldType::None) {
				i32tos((std::int32_t)ceilf(player->_activeShieldTime * FrameTimer::SecondsPerFrame), stringBuffer);

				DrawElement(PickupStopwatch, -1, adjustedView.X + 68.0f, bottom - 8.0f + 1.6f, ShadowLayer, Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.6f, 0.6f);
				DrawElement(PickupStopwatch, -1, adjustedView.X + 68.0f, bottom - 8.0f, MainLayer, Alignment::BottomLeft, Colorf::White, 0.6f, 0.6f);

				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, adjustedView.X + 84.0f, bottom - 8.0f + 1.0f, FontShadowLayer,
					Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.92f);
				_smallFont->DrawString(this, stringBuffer, charOffset, adjustedView.X + 84.0f, bottom - 8.0f, FontLayer,
					Alignment::BottomLeft, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.92f);
			}
		} else {
			if (shouldDrawHealth) {
				for (std::int32_t i = 0; i < health; i++) {
					DrawElement(Heart, -1, view.X + view.W - 4.0f - (i * 16.0f), view.Y + 4.0f + 1.6f, ShadowLayer, Alignment::TopRight, Colorf(0.0f, 0.0f, 0.0f, 0.3f));
					DrawElement(Heart, -1, view.X + view.W - 4.0f - (i * 16.0f), view.Y + 4.0f, MainLayer, Alignment::TopRight, Colorf::White);
				}
			}

			if (shouldDrawLives) {
				if (lives > 0) {
					if (lives < UINT8_MAX) {
						stringBuffer[0] = 'x';
						i32tos(lives, stringBuffer + 1);
						_smallFont->DrawString(this, stringBuffer, charOffsetShadow, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f + 1.0f, FontShadowLayer,
							Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f));
						_smallFont->DrawString(this, stringBuffer, charOffset, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f, FontLayer,
							Alignment::BottomLeft, Font::DefaultColor);
					} else {
						_smallFont->DrawString(this, "x\u221E", charOffsetShadow, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f + 1.0f, FontShadowLayer,
							Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f));
						_smallFont->DrawString(this, "x\u221E", charOffset, adjustedView.X + 36.0f - 4.0f, bottom - 1.0f, FontLayer,
							Alignment::BottomLeft, Font::DefaultColor);
					}
				}
			}

			if (player->_activeShield != ShieldType::None) {
				DrawElement(PickupStopwatch, -1, view.X + view.W * 0.5f - 30.0f, view.Y + 1.0f + 1.6f, ShadowLayer, Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.3f));
				DrawElement(PickupStopwatch, -1, view.X + view.W * 0.5f - 30.0f, view.Y + 1.0f, MainLayer, Alignment::TopLeft, Colorf::White);

				i32tos((std::int32_t)ceilf(player->_activeShieldTime * FrameTimer::SecondsPerFrame), stringBuffer);
				_smallFont->DrawString(this, stringBuffer, charOffsetShadow, view.X + view.W * 0.5f - 6.0f, view.Y + 6.0f + 1.0f,
					FontShadowLayer, Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f));
				_smallFont->DrawString(this, stringBuffer, charOffset, view.X + view.W * 0.5f - 6.0f, view.Y + 6.0f,
					FontLayer, Alignment::TopLeft, Font::DefaultColor);
			}
		}
	}

	void HUD::OnDrawScore(const Rectf& view, Actors::Player* player)
	{
#if defined(WITH_ANGELSCRIPT)
		if (_levelHandler->_scripts != nullptr && _levelHandler->_scripts->OnDraw(this, player, view, Scripting::DrawType::Score)) {
			return;
		}
#endif

		char stringBuffer[32];
		std::int32_t charOffset = 0;
		std::int32_t charOffsetShadow = 0;

		if (PreferencesCache::EnableReforgedHUD) {
			DrawElement(PickupFood, -1, view.X + 3.0f, view.Y + 3.0f + 1.6f, ShadowLayer, Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.4f));
			DrawElement(PickupFood, -1, view.X + 3.0f, view.Y + 3.0f, MainLayer, Alignment::TopLeft, Colorf::White);

			std::size_t length = formatInto(stringBuffer, "{:.8}", player->GetScore());
			_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 14.0f, view.Y + 5.0f + 1.0f, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
			_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 14.0f, view.Y + 5.0f, FontLayer,
				Alignment::TopLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
		} else {
			std::size_t length = formatInto(stringBuffer, "{:.8}", player->GetScore());
			_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + 4.0f, view.Y + 1.0f + 1.0f, FontShadowLayer,
				Alignment::TopLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
			_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + 4.0f, view.Y + 1.0f, FontLayer,
				Alignment::TopLeft, Font::DefaultColor, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.88f);
		}
	}

	void HUD::OnDrawWeaponAmmo(const Rectf& adjustedView, Actors::Player* player)
	{
#if defined(WITH_ANGELSCRIPT)
		if (_levelHandler->_scripts != nullptr && _levelHandler->_scripts->OnDraw(this, player, adjustedView, Scripting::DrawType::WeaponAmmo)) {
			return;
		}
#endif

		if (!player->_weaponAllowed || player->_playerType == PlayerType::Frog) {
			return;
		}

		float right = adjustedView.X + adjustedView.W;
		float bottom = adjustedView.Y + adjustedView.H;

		WeaponType weapon = player->_currentWeapon;
		Vector2f pos = Vector2f(right - 40.0f, bottom - 2.0f);
		AnimState currentWeaponAnim = GetCurrentWeapon(player, weapon, pos);

		char stringBuffer[32];
		StringView ammoCount;
		if (player->_inventory.WeaponAmmo[(int32_t)weapon] == UINT16_MAX) {
			ammoCount = "x\u221E"_s;
		} else {
			stringBuffer[0] = 'x';
			i32tos(player->_inventory.WeaponAmmo[(int32_t)weapon] / 256, stringBuffer + 1);
			ammoCount = stringBuffer;
		}

		std::int32_t charOffset = 0;
		std::int32_t charOffsetShadow = 0;
		_smallFont->DrawString(this, ammoCount, charOffsetShadow, right - 40.0f, bottom - 2.0f + 1.0f, FontShadowLayer,
			Alignment::BottomLeft, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);
		_smallFont->DrawString(this, ammoCount, charOffset, right - 40.0f, bottom - 2.0f, FontLayer,
			Alignment::BottomLeft, Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);

		auto* res = _metadata->FindAnimation(currentWeaponAnim);
		if (res != nullptr) {
			if (res->Base->FrameDimensions.Y < 20) {
				pos.Y -= std::round((20 - res->Base->FrameDimensions.Y) * 0.5f);
			}

			DrawElement(currentWeaponAnim, -1, pos.X, pos.Y + 1.6f, ShadowLayer, Alignment::BottomRight, Colorf(0.0f, 0.0f, 0.0f, 0.4f));

			// The Blaster icon is tinted with the player's colors (like the character icon), so recolor it to match
			// when the player is being recolored; every other weapon has fixed colors and uses the plain (baked) icon.
			bool drawn = false;
			if (weapon == WeaponType::Blaster) {
				std::int32_t paletteOffset = player->GetPaletteOffset();
				if (paletteOffset >= 0) {
					if (_metadataIndexed == nullptr) {
						_metadataIndexed = ContentResolver::Get().RequestMetadata("UI/HUD"_s, true);
					}
					drawn = DrawElementWithPalette(currentWeaponAnim, -1, pos.X, pos.Y, MainLayer, Alignment::BottomRight, Colorf::White, (float)paletteOffset);
				}
			}
			if (!drawn) {
				DrawElement(currentWeaponAnim, -1, pos.X, pos.Y, MainLayer, Alignment::BottomRight, Colorf::White);
			}
		}
	}

	void HUD::OnDrawActiveBoss(const Rectf& adjustedView)
	{
		if (_levelHandler->_activeBoss == nullptr || _levelHandler->_activeBoss->GetMaxHealth() == INT32_MAX) {
			return;
		}

		float bottom = adjustedView.Y + adjustedView.H;

		constexpr float TransitionTime = 60.0f;
		float y, alpha;
		if (_activeBossTime < TransitionTime) {
			y = (TransitionTime - _activeBossTime) / 8.0f;
			y = bottom * 0.1f - (y * y);
			alpha = std::max(_activeBossTime / TransitionTime, 0.0f);
		} else {
			y = bottom * 0.1f;
			alpha = 1.0f;
		}

		float perc = 0.08f + 0.84f * _levelHandler->_activeBoss->GetHealth() / _levelHandler->_activeBoss->GetMaxHealth();

		DrawElement(BossHealthBar, 0, ViewSize.X * 0.5f, y + 2.0f, ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.1f * alpha));
		DrawElement(BossHealthBar, 0, ViewSize.X * 0.5f, y + 1.0f, ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.2f * alpha));

		DrawElement(BossHealthBar, 0, ViewSize.X * 0.5f, y, MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, alpha));
		DrawElementClipped(BossHealthBar, 1, ViewSize.X * 0.5f, y, MainLayer + 2, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, alpha), perc, 1.0f);
	}

	void HUD::OnDrawLevelText(int32_t& charOffset)
	{
		constexpr float StillTime = 350.0f;
		constexpr float TransitionTime = 100.0f;
		constexpr float TotalTime = StillTime + TransitionTime * 2.0f;

		if (_levelTextTime < 0.0f) {
			return;
		}

		float offset;
		if (_levelTextTime < TransitionTime) {
			offset = powf((TransitionTime - _levelTextTime) / 12.0f, 3);
		} else if (_levelTextTime > TransitionTime + StillTime) {
			offset = -powf((_levelTextTime - TransitionTime - StillTime) / 12.0f, 3);
		} else {
			offset = 0;
		}

		float textScale = (ViewSize.X >= 360 ? 1.0f : 0.8f);

		std::int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(this, _levelText, charOffsetShadow, ViewSize.X * 0.5f + offset, ViewSize.Y * 0.04f + 2.5f, 50,
			Alignment::Top, Colorf(0.0f, 0.0f, 0.0f, 0.3f), textScale, 0.72f, 0.8f, 0.8f);
		_smallFont->DrawString(this, _levelText, charOffset, ViewSize.X * 0.5f + offset, ViewSize.Y * 0.04f, 60,
			Alignment::Top, Font::DefaultColor, textScale, 0.72f, 0.8f, 0.8f);

		if (_levelTextTime > TotalTime) {
			_levelTextTime = -1.0f;
			_levelText = {};
		}
	}

	void HUD::OnDrawCoins(const Rectf& view, std::int32_t& charOffset)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;
		constexpr float TotalTime = StillTime + TransitionTime * 2.0f;

		if (_coinsTime < 0.0f) {
			return;
		}

		float offset, alpha;
		if (_coinsTime < TransitionTime) {
			offset = (TransitionTime - _coinsTime) / 10.0f;
			offset = -(offset * offset);
			alpha = std::max(_coinsTime / TransitionTime, 0.1f);
		} else if (_coinsTime > TransitionTime + StillTime) {
			offset = (_coinsTime - TransitionTime - StillTime) / 10.0f;
			offset = (offset * offset);
			alpha = (TotalTime - _coinsTime) / TransitionTime;
		} else {
			offset = 0.0f;
			alpha = 1.0f;
		}

		float alpha2 = alpha * alpha;
		DrawElement(PickupCoin, -1, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + 2.5f + offset, ShadowLayer,
			Alignment::Right, Colorf(0.0f, 0.0f, 0.0f, 0.2f * alpha), 0.8f, 0.8f);
		DrawElement(PickupCoin, -1, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + offset, MainLayer,
			Alignment::Right, Colorf(1.0f, 1.0f, 1.0f, alpha2), 0.8f, 0.8f);

		char stringBuffer[32];
		std::size_t length = formatInto(stringBuffer, "x{}", _coins);

		std::int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + 2.5f + offset, FontShadowLayer,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f * alpha), 1.0f, 0.0f, 0.0f, 0.0f);

		Colorf fontColor = Font::DefaultColor;
		fontColor.SetAlpha(alpha2);
		_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + offset, FontLayer,
			Alignment::Left, fontColor, 1.0f, 0.0f, 0.0f, 0.0f);

		if (_coinsTime > TotalTime) {
			_coinsTime = -1.0f;
		}
	}

	void HUD::OnDrawGems(const Rectf& view, std::int32_t& charOffset)
	{
		constexpr float StillTime = 120.0f;
		constexpr float TransitionTime = 60.0f;
		constexpr float TotalTime = StillTime + TransitionTime * 2.0f;

		if (_gemsTime < 0.0f) {
			return;
		}

		float offset, alpha;
		if (_gemsTime < TransitionTime) {
			offset = (TransitionTime - _gemsTime) / 10.0f;
			offset = -(offset * offset);
			alpha = std::max(_gemsTime / TransitionTime, 0.1f);
		} else if (_gemsTime > TransitionTime + StillTime) {
			offset = (_gemsTime - TransitionTime - StillTime) / 10.0f;
			offset = (offset * offset);
			alpha = (TotalTime - _gemsTime) / TransitionTime;
		} else {
			offset = 0.0f;
			alpha = 1.0f;
		}

		AnimState animState = (AnimState)((std::uint32_t)PickupGemRed + _gemsLastType);
		float alpha2 = alpha * alpha;
		DrawElement(animState, -1, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + 2.5f + offset, ShadowLayer, Alignment::Right,
			Colorf(0.0f, 0.0f, 0.0f, 0.4f * alpha2), 0.8f, 0.8f);
		DrawElement(animState, -1, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + offset, MainLayer, Alignment::Right,
			Colorf(1.0f, 1.0f, 1.0f, 0.8f * alpha2), 0.8f, 0.8f);

		char stringBuffer[32];
		std::size_t length = formatInto(stringBuffer, "x{}", _gems);

		std::int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(this, { stringBuffer, length }, charOffsetShadow, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + 2.5f + offset, FontShadowLayer,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f * alpha), 1.0f, 0.0f, 0.0f, 0.0f);

		Colorf fontColor = Font::DefaultColor;
		fontColor.SetAlpha(alpha2);
		_smallFont->DrawString(this, { stringBuffer, length }, charOffset, view.X + view.W * 0.5f, view.Y + view.H * 0.92f + offset, FontLayer,
			Alignment::Left, fontColor, 1.0f, 0.0f, 0.0f, 0.0f);

		if (_gemsTime > TotalTime) {
			_gemsTime = -1.0f;
		}
	}

	void HUD::OnDrawTouchButtons(Actors::Player* player)
	{
		bool isConsoleVisible = _levelHandler->_console->IsVisible();
		for (std::uint32_t bi = 0; bi < TouchButtonsCount; bi++) {
			auto& button = _touchButtons[bi];

			// In joystick mode, skip the D-pad visual (indices 0-4); joystick is drawn separately
			if (PreferencesCache::EnableTouchJoystick && bi <= 4) {
				continue;
			}

			if (player == nullptr && button.Action != PlayerAction::Menu && button.Action != PlayerAction::Console) {
				continue;
			}

			Texture* texture = nullptr;
			if (isConsoleVisible) {
				// Show only Close button if console is open
				if (button.Action != PlayerAction::Menu) {
					continue;
				}
				if (auto anim = _metadata->FindAnimation(TouchClose)) {
					texture = anim->Base->TextureDiffuse.get();
				}
			} else {
#if defined(NCINE_HAS_NATIVE_BACK_BUTTON)
				if (button.Action == PlayerAction::Menu && PreferencesCache::UseNativeBackButton) {
					continue;
				}
#endif
				if (button.Graphics != nullptr) {
					texture = button.Graphics->Base->TextureDiffuse.get();
				}
			}

			if (texture == nullptr) {
				continue;
			}

			float x = button.Left;
			float y = button.Top;
			if ((button.Align & Alignment::Right) == Alignment::Right) {
				x = ViewSize.X - button.Width * 0.5f - x;
			} else if ((button.Align & Alignment::Left) == Alignment::Left) {
				x = x + button.Width * 0.5f;
			} else {
				x = ViewSize.X / 2;
			}
			if ((button.Align & Alignment::Bottom) == Alignment::Bottom) {
				y = ViewSize.Y - button.Height * 0.5f - y;
			} else {
				y = y + button.Height * 0.5f;
			}

			Colorf color = (button.Action == PlayerAction::Run && player != nullptr && player->_isRunPressed ? Colorf(0.6f, 0.6f, 0.6f) : Colorf::White);

			DrawTexture(*texture, Vector2f(std::round(x - button.Width * 0.5f), std::round(y - button.Height * 0.5f)),
				TouchButtonsLayer, Vector2f(button.Width, button.Height), Vector4f(1.0f, 0.0f, 1.0f, 0.0f), color);
		}

		// Floating analog joystick overlay (replaces D-pad in joystick mode)
		if (PreferencesCache::EnableTouchJoystick && _joystickActive) {
			Vector2f diff = _joystickCurrent - _joystickOrigin;
			float dist = diff.Length();
			Vector2f thumbPos = _joystickOrigin + (dist > _joystickMaxRadius ? diff / dist * _joystickMaxRadius : diff);
			DrawJoystick(_joystickOrigin.X, _joystickOrigin.Y, _joystickMaxRadius, _joystickMaxRadius * 0.75f,
				thumbPos.X, thumbPos.Y);
		}
	}

	bool HUD::GetTouchPauseButtonRect(Rectf& bounds) const
	{
		if (_touchButtonsTimer <= 0.0f) {
			return false;
		}

		// The pause/menu button is touch button 9 (see RefreshTouchButtons())
		const auto& button = _touchButtons[9];
		if (button.Action != PlayerAction::Menu || button.Graphics == nullptr) {
			return false;
		}
#if defined(NCINE_HAS_NATIVE_BACK_BUTTON)
		if (PreferencesCache::UseNativeBackButton) {
			// Handled by the platform's native back button, so nothing is drawn here
			return false;
		}
#endif

		// Resolve the on-screen center from the alignment, exactly as OnDrawTouchButtons() does
		float x;
		if ((button.Align & Alignment::Right) == Alignment::Right) {
			x = ViewSize.X - button.Width * 0.5f - button.Left;
		} else if ((button.Align & Alignment::Left) == Alignment::Left) {
			x = button.Left + button.Width * 0.5f;
		} else {
			x = ViewSize.X / 2;
		}
		float y;
		if ((button.Align & Alignment::Bottom) == Alignment::Bottom) {
			y = ViewSize.Y - button.Height * 0.5f - button.Top;
		} else {
			y = button.Top + button.Height * 0.5f;
		}

		bounds = Rectf(x - button.Width * 0.5f, y - button.Height * 0.5f, button.Width, button.Height);
		return true;
	}

	void HUD::DrawHealthCarrots(float x, float y, std::int32_t health)
	{
		constexpr Colorf CarrotShadowColor = Colorf(0.0f, 0.0f, 0.0f, 0.5f);

		std::int32_t lastCarrotIdx = 0;
		float lastCarrotOffset = 0.0f;
		float scale = 0.5f;
		float angleBase1 = sinf(AnimTime * 10.0f) * fDegToRad;
		float angleBase2 = sinf(AnimTime * 12.0f + 3.0f) * fDegToRad;
		float angleBase3 = sinf(AnimTime * 11.0f + 7.0f) * fDegToRad;

		// Limit frame rate of carrot movement
		angleBase1 = std::round(angleBase1 * 3.0f * fRadToDeg) / (3.0f * fRadToDeg);
		angleBase2 = std::round(angleBase2 * 3.0f * fRadToDeg) / (3.0f * fRadToDeg);
		angleBase3 = std::round(angleBase3 * 3.0f * fRadToDeg) / (3.0f * fRadToDeg);

		if (health >= 1) {
			float angle = angleBase1 * (health > 1 ? -6.0f : -14.0f) + 0.2f;
			DrawElement(PickupCarrot, 1, x + 1.0f - 1.0f, y + 2.0f + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 1, x + 1.0f + 1.0f, y + 2.0f + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 1, x + 1.0f, y + 2.0f, FontLayer + lastCarrotIdx * 2 + 1, Alignment::Left, Colorf::White, scale, scale, false, angle);
			lastCarrotIdx++;
			lastCarrotOffset = 7.0f;
		}
		if (health >= 3) {
			float angle = angleBase3 * 10.0f;
			lastCarrotOffset -= 1.0f;
			DrawElement(PickupCarrot, 2, x + lastCarrotOffset - 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 2, x + lastCarrotOffset + 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 2, x + lastCarrotOffset, y, FontLayer + lastCarrotIdx * 2 + 1, Alignment::Left, Colorf::White, scale, scale, false, angle);
			lastCarrotIdx++;
			lastCarrotOffset += 6.0f;
		}
		if (health >= 2) {
			float angle = angleBase2 * -6.0f + 0.2f;
			DrawElement(PickupCarrot, 2, x + lastCarrotOffset - 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 2, x + lastCarrotOffset + 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 2, x + lastCarrotOffset, y, FontLayer + lastCarrotIdx * 2 + 1, Alignment::Left, Colorf::White, scale, scale, false, angle);
			lastCarrotIdx++;
			lastCarrotOffset = 17.0f;
		}
		if (health >= 6) {
			for (std::int32_t i = 0; i < health - 5; i++) {
				float angle = ((i % 3) == 1 ? angleBase2 : angleBase3) * (4.0f + ((i * 7) % 6));
				if ((i % 2) == 1) {
					angle = -angle;
				}

				DrawElement(PickupCarrot, 2, x + lastCarrotOffset - 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
				DrawElement(PickupCarrot, 2, x + lastCarrotOffset + 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
				DrawElement(PickupCarrot, 2, x + lastCarrotOffset, y, FontLayer + lastCarrotIdx * 2 + 1, Alignment::Left, Colorf::White, scale, scale, false, angle);
				lastCarrotIdx++;
				lastCarrotOffset += 5.0f;
			}
		}
		if (health >= 5) {
			float angle = angleBase1 * 10.0f;
			lastCarrotOffset -= 1.0f;
			DrawElement(PickupCarrot, 3, x + lastCarrotOffset - 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 3, x + lastCarrotOffset + 1.0f, y + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 3, x + lastCarrotOffset, y, FontLayer + lastCarrotIdx * 2 + 1, Alignment::Left, Colorf::White, scale, scale, false, angle);
			lastCarrotIdx++;
			lastCarrotOffset += 5.0f;
		}
		if (health >= 4) {
			float angle = angleBase2 * -6.0f - 0.4f;
			lastCarrotOffset -= 2.0f;
			DrawElement(PickupCarrot, 5, x + lastCarrotOffset - 1.0f, y + 2.0f + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 5, x + lastCarrotOffset + 1.0f, y + 2.0f + 1.0f, FontLayer + lastCarrotIdx * 2, Alignment::Left, CarrotShadowColor, scale, scale, false, angle);
			DrawElement(PickupCarrot, 5, x + lastCarrotOffset, y + 2.0f, FontLayer + lastCarrotIdx * 2 + 1, Alignment::Left, Colorf::White, scale, scale, false, angle);
			lastCarrotIdx++;
		}
	}

	void HUD::DrawViewportSeparators()
	{
		switch (_levelHandler->_assignedViewports.size()) {
			case 2: {
				if (PreferencesCache::PreferVerticalSplitscreen) {
					std::int32_t halfW = ViewSize.X / 2;
					DrawSolid(Vector2f(halfW - 1.0f, 0.0f), 20, Vector2f(1.0f, ViewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.4f));
					DrawSolid(Vector2f(halfW, 0.0f), 20, Vector2f(1.0f, ViewSize.Y), Colorf(1.0f, 1.0f, 1.0f, 0.1f), true);
				} else {
					std::int32_t halfH = ViewSize.Y / 2;
					DrawSolid(Vector2f(0.0f, halfH - 1.0f), 20, Vector2f(ViewSize.X, 1.0f), Colorf(1.0f, 1.0f, 1.0f, 0.1f), true);
					DrawSolid(Vector2f(0.0f, halfH), 20, Vector2f(ViewSize.X, 1.0f), Colorf(0.0f, 0.0f, 0.0f, 0.4f));
				}
				break;
			}
			case 3: {
				std::int32_t halfW = (ViewSize.X + 1) / 2;
				std::int32_t halfH = (ViewSize.Y + 1) / 2;
				DrawSolid(Vector2f(halfW, halfH), 20, Vector2f(halfW, halfH), Colorf::Black);
				DEATH_FALLTHROUGH
			}
			case 4: {
				std::int32_t halfW = (ViewSize.X + 1) / 2;
				std::int32_t halfH = (ViewSize.Y + 1) / 2;
				DrawSolid(Vector2f(halfW - 1.0f, 0.0f), 20, Vector2f(1.0f, ViewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.4f));
				DrawSolid(Vector2f(halfW, 0.0f), 20, Vector2f(1.0f, ViewSize.Y), Colorf(1.0f, 1.0f, 1.0f, 0.1f), true);
				DrawSolid(Vector2f(0.0f, halfH - 1.0f), 20, Vector2f(ViewSize.X, 1.0f), Colorf(1.0f, 1.0f, 1.0f, 0.1f), true);
				DrawSolid(Vector2f(0.0f, halfH), 20, Vector2f(ViewSize.X, 1.0f), Colorf(0.0f, 0.0f, 0.0f, 0.4f));
				break;
			}
		}
	}

	void HUD::DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scaleX, float scaleY, bool additiveBlending, float angle)
	{
		auto* res = _metadata->FindAnimation(state);
		if (res == nullptr) {
			return;
		}

		if (frame < 0) {
			frame = res->FrameOffset + ((std::int32_t)(AnimTime * res->FrameCount / res->AnimDuration) % res->FrameCount);
		}

		GenericGraphicResource* base = res->Base;
		Vector2f size = Vector2f(base->FrameDimensions.X * scaleX, base->FrameDimensions.Y * scaleY);
		Vector2f adjustedPos = ApplyAlignment(align, Vector2f(x, y), size);

		Vector2i texSize = base->TextureDiffuse->GetSize();
		std::int32_t col = frame % base->FrameConfiguration.X;
		std::int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? res->PaletteOffset : -1);
		DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, additiveBlending, angle, paletteOffset);
	}

	bool HUD::DrawElementWithPalette(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float paletteOffset, float scaleX, float scaleY)
	{
		if (_metadataIndexed == nullptr) {
			return false;
		}
		auto* res = _metadataIndexed->FindAnimation(state);
		if (res == nullptr || res->Base == nullptr || res->Base->TextureDiffuse == nullptr) {
			return false;
		}

		GenericGraphicResource* base = res->Base;
		// Only the PaletteRemap shader makes sense here - the diffuse must hold palette indices, not baked colors
		if ((base->Flags & GenericGraphicResourceFlags::Indexed) != GenericGraphicResourceFlags::Indexed) {
			return false;
		}

		auto* shader = ContentResolver::Get().GetShader(PrecompiledShader::PaletteRemap);
		if (shader == nullptr) {
			return false;
		}
		Texture* palette = ContentResolver::Get().GetPaletteTexture();
		if (palette == nullptr) {
			return false;
		}

		if (frame < 0) {
			frame = res->FrameOffset + ((std::int32_t)(AnimTime * res->FrameCount / res->AnimDuration) % res->FrameCount);
		}

		Vector2f size = Vector2f(base->FrameDimensions.X * scaleX, base->FrameDimensions.Y * scaleY);
		Vector2f adjustedPos = ApplyAlignment(align, Vector2f(x, y), size);

		Vector2i texSize = base->TextureDiffuse->GetSize();
		std::int32_t col = frame % base->FrameConfiguration.X;
		std::int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		auto command = RentRenderCommand();
		if (command->GetMaterial().SetShader(shader)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
		}
		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

		auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
		if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
			textureUniform->SetIntValue(0); // GL_TEXTURE0
		}
		auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
		if (paletteUniform != nullptr) {
			paletteUniform->SetIntValue(1); // GL_TEXTURE1
		}

		auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(texCoords.Data());
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(size.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());
		auto* palOffsetUniform = instanceBlock->GetUniform(Material::PaletteOffsetUniformName);
		if (palOffsetUniform != nullptr) {
			palOffsetUniform->SetFloatValue(paletteOffset);
		}

		command->SetTransformation(Matrix4x4f::Translation(adjustedPos.X, adjustedPos.Y, 0.0f));
		command->SetLayer(z);
		command->GetMaterial().SetTexture(0, *base->TextureDiffuse.get());
		command->GetMaterial().SetTexture(1, *palette);

		DrawRenderCommand(command);
		return true;
	}

	void HUD::DrawElementClipped(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float clipX, float clipY)
	{
		auto* res = _metadata->FindAnimation(state);
		if (res == nullptr) {
			return;
		}

		if (frame < 0) {
			frame = res->FrameOffset + ((std::int32_t)(AnimTime * res->FrameCount / res->AnimDuration) % res->FrameCount);
		}

		GenericGraphicResource* base = res->Base;
		Vector2f size = Vector2f(base->FrameDimensions.X * clipX, base->FrameDimensions.Y * clipY);
		Vector2f adjustedPos = ApplyAlignment(align, Vector2f(x, y), base->FrameDimensions.As<float>());

		Vector2i texSize = base->TextureDiffuse->GetSize();
		std::int32_t col = frame % base->FrameConfiguration.X;
		std::int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			std::floor(float(base->FrameDimensions.X) * clipX) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			std::floor(float(base->FrameDimensions.Y) * clipY) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? res->PaletteOffset : -1);
		DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, false, 0.0f, paletteOffset);
	}

	AnimState HUD::GetCurrentWeapon(Actors::Player* player, WeaponType weapon, Vector2f& offset)
	{
		if (weapon == WeaponType::Toaster && player->_inWater) {
			offset.Y += 2;
			return WeaponToasterDisabled;
		} else if (weapon == WeaponType::Seeker) {
			offset.X += 2;
		} else if (weapon == WeaponType::TNT) {
			offset.X += 2;
		} else if (weapon == WeaponType::Electro) {
			offset.X += 6;
		}

		if ((player->_inventory.WeaponUpgrades[(std::int32_t)weapon] & 0x01) != 0) {
			switch (weapon) {
				default:
				case WeaponType::Blaster:
					if (player->_playerType == PlayerType::Spaz) {
						return WeaponPowerUpBlasterSpaz;
					} else if (player->_playerType == PlayerType::Lori) {
						return WeaponPowerUpBlasterLori;
					} else {
						return WeaponPowerUpBlasterJazz;
					}

				case WeaponType::Bouncer: return WeaponPowerUpBouncer;
				case WeaponType::Freezer: return WeaponPowerUpFreezer;
				case WeaponType::Seeker: return WeaponPowerUpSeeker;
				case WeaponType::RF: return WeaponPowerUpRF;
				case WeaponType::Toaster: return WeaponPowerUpToaster;
				case WeaponType::TNT: return WeaponPowerUpTNT;
				case WeaponType::Pepper: return WeaponPowerUpPepper;
				case WeaponType::Electro: return WeaponPowerUpElectro;
				case WeaponType::Thunderbolt: return WeaponPowerUpThunderbolt;
			}
		} else {
			switch (weapon) {
				default:
				case WeaponType::Blaster:
					if (player->_playerType == PlayerType::Spaz) {
						return WeaponBlasterSpaz;
					} else if (player->_playerType == PlayerType::Lori) {
						return WeaponBlasterLori;
					} else {
						return WeaponBlasterJazz;
					}

				case WeaponType::Bouncer: return WeaponBouncer;
				case WeaponType::Freezer: return WeaponFreezer;
				case WeaponType::Seeker: return WeaponSeeker;
				case WeaponType::RF: return WeaponRF;
				case WeaponType::Toaster: return WeaponToaster;
				case WeaponType::TNT: return WeaponTNT;
				case WeaponType::Pepper: return WeaponPepper;
				case WeaponType::Electro: return WeaponElectro;
				case WeaponType::Thunderbolt: return WeaponThunderbolt;
			}
		}
	}

	void HUD::DrawWeaponWheel(const Rectf& view, Actors::Player* player)
	{
		auto& state = _weaponWheel[player->_playerIndex];
		if (state.Anim <= 0.0f) {
			return;
		}

		auto* res = _metadata->FindAnimation(WeaponWheel);
		if (res == nullptr) {
			return;
		}

		Texture& lineTexture = *res->Base->TextureDiffuse.get();

		auto& input = _levelHandler->_playerInputs[player->_playerIndex];
		if (!input.Frozen) {
			input.Frozen = true;
			input.FrozenMovement = input.RequiredMovement;
		}

		if (player->_weaponWheelState == Actors::Player::WeaponWheelState::Hidden && player->_sugarRushLeft <= 0.0f && state.Anim >= WeaponWheelAnimDuration * 0.1f) {
			player->_weaponWheelState = Actors::Player::WeaponWheelState::Opening;
		}

		Vector2f center = view.Center();
		float angleStep = fTwoPi / state.WeaponCount;

		float h = input.RequiredMovement.X;
		float v = input.RequiredMovement.Y;
		if (std::abs(h) + std::abs(v) < 0.5f) {
			h = 0.0f;
			v = 0.0f;
		}

		if (state.Vertices == nullptr) {
			state.Vertices = std::make_unique<Vertex[]>(WeaponWheelMaxVertices);
		}
		state.VerticesCount = 0;
		state.RenderCommandsCount = 0;

		float requestedAngle;
		std::int32_t requestedIndex;
		if (h == 0.0f && v == 0.0f) {
			requestedAngle = NAN;
			requestedIndex = -1;
		} else {
			requestedAngle = atan2f(v, h);
			if (requestedAngle < 0) {
				requestedAngle += fTwoPi;
			}

			float adjustedAngle = requestedAngle + fPiOver2 + angleStep * 0.5f;
			if (adjustedAngle >= fTwoPi) {
				adjustedAngle -= fTwoPi;
			}

			requestedIndex = (std::int32_t)(state.WeaponCount * adjustedAngle / fTwoPi);
		}

		float scale = std::min(std::min(view.W, view.H) * 0.0034f, 1.0f);
		float alpha = state.Anim / WeaponWheelAnimDuration;
		float easing = Menu::Easing::OutCubic(alpha);
		float distance = scale * (20.0f  + (70.0f * easing));
		float distance2 = scale * (10.0f  + (50.0f * easing));
		float distance3 = distance2 * 2.0f;

		float alphaInner = std::min(Vector2f(h, v).Length() * easing * 1.5f - 0.6f, 1.0f);
		if (alphaInner > 0.0f) {
			DrawElement(WeaponWheelInner, -1, center.X, center.Y, MainLayer + 5, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, alphaInner), scale * easing, scale * easing, true, requestedAngle);
		}

		float angle = -fPiOver2;
		for (std::int32_t i = 0, j = 0; i < std::int32_t(arraySize(player->_inventory.WeaponAmmo)); i++) {
			if (player->_inventory.WeaponAmmo[i] != 0) {
				float x = cosf(angle) * distance;
				float y = sinf(angle) * distance;

				Vector2f pos = Vector2f(center.X + x, center.Y + y);
				AnimState weapon = GetCurrentWeapon(player, (WeaponType)i, pos);
				Colorf color2;
				float scale;
				bool isSelected = (j == requestedIndex);
				if (isSelected) {
					state.LastIndex = i;
					color2 = Colorf(1.0f, 0.8f, 0.5f, alpha);
					scale = 1.0f;
				} else {
					color2 = Colorf(1.0f, 1.0f, 1.0f, alpha * 0.9f);
					scale = 0.9f;
				}

				DrawElement(WeaponWheelDim, -1, pos.X, pos.Y, ShadowLayer - 10, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, alpha * 0.6f), 5.0f, 5.0f);

				// The Blaster icon is tinted with the player's colors (like the character icon), so recolor it to match
				// when the player is being recolored; every other weapon has fixed colors and uses the plain (baked) icon.
				Colorf weaponColor = Colorf(1.0f, 1.0f, 1.0f, isSelected ? alpha : alpha * 0.7f);
				bool drawn = false;
				if ((WeaponType)i == WeaponType::Blaster) {
					std::int32_t paletteOffset = player->GetPaletteOffset();
					if (paletteOffset >= 0) {
						if (_metadataIndexed == nullptr) {
							_metadataIndexed = ContentResolver::Get().RequestMetadata("UI/HUD"_s, true);
						}
						drawn = DrawElementWithPalette(weapon, -1, roundf(pos.X), roundf(pos.Y), MainLayer + 10, Alignment::Center, weaponColor, (float)paletteOffset, scale, scale);
					}
				}
				if (!drawn) {
					DrawElement(weapon, -1, roundf(pos.X), roundf(pos.Y), MainLayer + 10, Alignment::Center, weaponColor, scale, scale);
				}

				if (PreferencesCache::WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount) {
					char stringBuffer[32];
					StringView ammoCount;
					if (player->_inventory.WeaponAmmo[i] == UINT16_MAX) {
						ammoCount = "x\u221E"_s;
					} else {
						stringBuffer[0] = 'x';
						i32tos(player->_inventory.WeaponAmmo[i] / 256, stringBuffer + 1);
						ammoCount = stringBuffer;
					}

					std::int32_t charOffset = 0;
					_smallFont->DrawString(this, ammoCount, charOffset, center.X + cosf(angle) * distance * 1.4f, center.Y + sinf(angle) * distance * 1.4f, FontLayer,
						Alignment::Center, isSelected ? Colorf(0.62f, 0.44f, 0.34f, 0.5f * alpha) : Colorf(0.45f, 0.45f, 0.45f, 0.48f * alpha), 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
				}

				float angleFrom = angle - angleStep * 0.4f;
				float angleTo = angle + angleStep * 0.4f;

				Colorf color1 = Colorf(0.0f, 0.0f, 0.0f, alpha * 0.2f);

				DrawWeaponWheelSegment(state, center.X - distance2 - 1.0f, center.Y - distance2 - 1.0f, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);
				DrawWeaponWheelSegment(state, center.X - distance2 - 1.0f, center.Y - distance2 + 1.0f, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);
				DrawWeaponWheelSegment(state, center.X - distance2 + 1.0f, center.Y - distance2 - 1.0f, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);
				DrawWeaponWheelSegment(state, center.X - distance2 + 1.0f, center.Y - distance2 + 1.0f, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);

				DrawWeaponWheelSegment(state, center.X - distance2 - 0.5f, center.Y - distance2, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);
				DrawWeaponWheelSegment(state, center.X - distance2 + 0.5f, center.Y - distance2, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);
				DrawWeaponWheelSegment(state, center.X - distance2, center.Y - distance2 - 0.5f, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);
				DrawWeaponWheelSegment(state, center.X - distance2, center.Y - distance2 + 0.5f, distance3, distance3, ShadowLayer, angleFrom, angleTo, lineTexture, color1);

				DrawWeaponWheelSegment(state, center.X - distance2, center.Y - distance2, distance3, distance3, MainLayer, angleFrom, angleTo, lineTexture, color2);
				if (isSelected) {
					DrawWeaponWheelSegment(state, center.X - distance2 - 1.0f, center.Y - distance2 - 1.0f, distance3 + 2.0f, distance3 + 2.0f, MainLayer + 1, angleFrom + fRadAngle1, angleTo - fRadAngle1, lineTexture, Colorf(1.0f, 0.8f, 0.5f, alpha * 0.3f));
				}

				angle += angleStep;
				j++;
			}
		}
	}

	void HUD::UpdateWeaponWheel(float timeMult)
	{
		for (auto& viewport : _levelHandler->_assignedViewports) {
			if (Actors::Player* player = runtime_cast<Actors::Player>(viewport->GetTargetActor())) {
				auto& state = _weaponWheel[player->_playerIndex];

				if (PrepareWeaponWheel(player, state.WeaponCount)) {
					if (state.Anim < WeaponWheelAnimDuration) {
						state.Anim += timeMult;
						if (state.Anim > WeaponWheelAnimDuration) {
							state.Anim = WeaponWheelAnimDuration;
						}
					}
				} else {
					if (state.Anim > 0.0f) {
						state.Anim -= timeMult * 2.0f;
						if (state.Anim <= 0.0f) {
							state.Anim = 0.0f;
							_levelHandler->_playerInputs[player->_playerIndex].Frozen = false;

							if (player->_weaponWheelState == Actors::Player::WeaponWheelState::Visible) {
								player->_weaponWheelState = Actors::Player::WeaponWheelState::Closing;
							}
						}
					}
				}
			}
		}
	}

	bool HUD::PrepareWeaponWheel(Actors::Player* player, std::int32_t& weaponCount)
	{
		weaponCount = 0;

		auto& state = _weaponWheel[player->_playerIndex];

		if (PreferencesCache::WeaponWheel == WeaponWheelStyle::Disabled || player == nullptr ||
			!((player->_controllable && player->_controllableExternal) || !_levelHandler->IsReforged()) || player->_playerType == PlayerType::Frog) {
			if (state.Anim > 0.0f) {
				state.Shown = false;
				state.LastIndex = -1;
			}
			return false;
		}

		bool isGamepad;
		if (!_levelHandler->PlayerActionPressed(player, PlayerAction::ChangeWeapon, true, isGamepad) || !isGamepad) {
			if (state.Anim > 0.0f) {
				if (state.Anim < WeaponWheelAnimDuration * 0.5f) {
					// Switch to the next weapon on short press
					if (state.Shown) {
						player->SwitchToNextWeapon();
					}
				} else if (state.LastIndex != -1) {
					player->SwitchToWeaponByIndex((std::uint32_t)state.LastIndex);
				}
				state.Shown = false;
				state.LastIndex = -1;
				weaponCount = GetWeaponCount(player);
			}
			return false;
		}

		state.Shown = true;
		weaponCount = GetWeaponCount(player);
		return (weaponCount > 0);
	}

	std::int32_t HUD::GetWeaponCount(Actors::Player* player)
	{
		std::int32_t weaponCount = 0;

		for (std::int32_t i = 0; i < std::int32_t(arraySize(player->_inventory.WeaponAmmo)); i++) {
			if (player->_inventory.WeaponAmmo[i] != 0) {
				weaponCount++;
			}
		}

		// Player must have at least 2 weapons
		if (weaponCount < 2) {
			weaponCount = 0;
		}

		return weaponCount;
	}

	void HUD::DrawWeaponWheelSegment(WeaponWheelState& state, float x, float y, float width, float height, std::uint16_t z, float minAngle, float maxAngle, const Texture& texture, const Colorf& color)
	{
		width *= 0.5f; x += width;
		height *= 0.5f; y += height;

		float angleRange = std::min(maxAngle - minAngle, fRadAngle360);
		std::int32_t segmentNum = std::clamp((std::int32_t)std::round(powf(std::max(width, height), 0.65f) * 3.5f * angleRange / fRadAngle360), 4, 128);
		float angleStep = angleRange / (segmentNum - 1);
		std::int32_t vertexCount = segmentNum + 2;
		float angle = minAngle;

		Vertex* vertices = &state.Vertices[state.VerticesCount];
		state.VerticesCount += vertexCount;

		if (state.VerticesCount > WeaponWheelMaxVertices) {
			// This shouldn't happen, 768 vertices should be enough
			DEATH_DEBUG_ASSERT(state.VerticesCount <= WeaponWheelMaxVertices);
			return;
		}

		constexpr float Mult = 2.2f;

		{
			std::int32_t j = 0;
			vertices[j].X = x + cosf(angle) * (width * Mult - 0.5f);
			vertices[j].Y = y + sinf(angle) * (height * Mult - 0.5f);
			vertices[j].U = 0.0f;
			vertices[j].V = 0.0f;
		}

		for (std::int32_t i = 1; i < vertexCount - 1; i++) {
			vertices[i].X = x + cosf(angle) * (width - 0.5f);
			vertices[i].Y = y + sinf(angle) * (height - 0.5f);
			vertices[i].U = 0.15f + (0.7f * (float)(i - 1) / (vertexCount - 3));
			vertices[i].V = 0.0f;

			angle += angleStep;
		}

		{
			angle -= angleStep;

			std::int32_t j = vertexCount - 1;
			vertices[j].X = x + cosf(angle) * (width * Mult - 0.5f);
			vertices[j].Y = y + sinf(angle) * (height * Mult - 0.5f);
			vertices[j].U = 1.0f;
			vertices[j].V = 0.0f;
		}

		// Create render command
		RenderCommand* command;
		if (state.RenderCommandsCount < state.RenderCommands.size()) {
			command = state.RenderCommands[state.RenderCommandsCount].get();
			state.RenderCommandsCount++;
		} else {
			command = state.RenderCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::MeshSprite)).get();
			command->GetMaterial().SetBlendingEnabled(true);
		}

		if (command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::MeshSprite)) {
			command->GetMaterial().ReserveUniformsDataMemory();

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}

		command->GetGeometry().SetDrawParameters(PrimitiveType::LineStrip, 0, vertexCount);
		command->GetGeometry().SetElementsPerVertex(VertexFloats);
		command->GetGeometry().SetHostVertexPointer((const float*)vertices);

		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

		auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(1.0f, 1.0f);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

		command->SetTransformation(Matrix4x4f::Identity);
		command->SetLayer(z);
		command->GetMaterial().SetTexture(texture);

		DrawRenderCommand(command);
	}

	void HUD::RefreshTouchButtons()
	{
		using PC = PreferencesCache;
		using Slot = Jazz2::TouchButtonSlot;

		auto toAlign = [](Jazz2::TouchButtonAnchor anchor) -> Alignment {
			switch (anchor) {
				case Jazz2::TouchButtonAnchor::BottomLeft:  return Alignment::BottomLeft;
				case Jazz2::TouchButtonAnchor::BottomRight: return Alignment::BottomRight;
				case Jazz2::TouchButtonAnchor::TopLeft:     return Alignment::TopLeft;
				case Jazz2::TouchButtonAnchor::TopRight:    return Alignment::TopRight;
				case Jazz2::TouchButtonAnchor::TopCenter:   return Alignment::Top;
				default:                                    return Alignment::BottomLeft;
			}
		};

		// D-pad / Joystick (buttons 0-4)
		{
			const auto& layout = PC::TouchButtons[(std::size_t)Slot::Dpad];
			float scale = layout.Scale;
			float ex = layout.EdgeOffset.X;
			float ey = layout.EdgeOffset.Y;
			Alignment align = toAlign(layout.Anchor);
			float s = std::round(DpadSize * DefaultRef * scale);
			float t = std::round(DpadThreshold * DefaultRef * scale);

			_touchButtons[0] = MakeTouchButton(PlayerAction::None, TouchDpad, align, ex, ey, s, s);
			_touchButtons[1] = MakeTouchButton(PlayerAction::Up, AnimState::Default, align, ex, ey + s * 2.0f / 3.0f, s, s / 3.0f + t);
			_touchButtons[2] = MakeTouchButton(PlayerAction::Down, AnimState::Default, align, ex, ey - t, s, s / 3.0f + t);
			_touchButtons[3] = MakeTouchButton(PlayerAction::Left, AnimState::Default, align | AllowRollover, ex - t, ey, s / 3.0f + t, s);
			_touchButtons[4] = MakeTouchButton(PlayerAction::Right, AnimState::Default, align | AllowRollover, ex + s * 2.0f / 3.0f, ey, s / 3.0f + t, s);

			_joystickMaxRadius = s * 0.5f;
		}

		// Fire, Jump, Run, ChangeWeapon (buttons 5-8)
		const AnimState actionStates[] = { TouchFire, TouchJump, TouchRun, TouchChange };
		const float actionDefaultSizes[] = { ButtonSize, ButtonSize, ButtonSize, SmallButtonSize };
		const Slot actionSlots[] = { Slot::Fire, Slot::Jump, Slot::Run, Slot::ChangeWeapon };
		for (std::int32_t i = 0; i < 4; i++) {
			const auto& layout = PC::TouchButtons[(std::size_t)actionSlots[i]];
			float sz = std::round(actionDefaultSizes[i] * DefaultRef * layout.Scale);
			_touchButtons[5 + i] = MakeTouchButton(
				static_cast<PlayerAction>((std::int32_t)PlayerAction::Fire + i),
				actionStates[i], toAlign(layout.Anchor),
				layout.EdgeOffset.X, layout.EdgeOffset.Y, sz, sz);
		}

		// Menu and Console (buttons 9-10)
#if defined(DEATH_TARGET_ANDROID)
		if (static_cast<AndroidApplication&>(theApplication()).IsScreenRound()) {
			const auto& layout = PC::TouchButtons[(std::size_t)Slot::Menu];
			float sz = std::round(SmallButtonSize * DefaultRef * layout.Scale);
			_touchButtons[9] = MakeTouchButton(PlayerAction::Menu, TouchPause, Alignment::Top | Fixed,
				layout.EdgeOffset.X, layout.EdgeOffset.Y, sz, sz);
			_touchButtons[10] = {};
		} else
#endif
		{
			{
				const auto& layout = PC::TouchButtons[(std::size_t)Slot::Menu];
				float sz = std::round(SmallButtonSize * DefaultRef * layout.Scale);
				_touchButtons[9] = MakeTouchButton(PlayerAction::Menu, TouchPause, toAlign(layout.Anchor),
					layout.EdgeOffset.X, layout.EdgeOffset.Y, sz, sz);
			}
			{
				const auto& layout = PC::TouchButtons[(std::size_t)Slot::Console];
				float sz = std::round(SmallButtonSize * DefaultRef * layout.Scale);
				_touchButtons[10] = MakeTouchButton(PlayerAction::Console, AnimState::Default, toAlign(layout.Anchor),
					layout.EdgeOffset.X, layout.EdgeOffset.Y, sz, sz);
			}
		}
	}

	HUD::TouchButtonInfo HUD::MakeTouchButton(PlayerAction action, AnimState state, Alignment align, float edgeX, float edgeY, float w, float h)
	{
		TouchButtonInfo info;
		info.Action = action;
		info.Left = std::round(edgeX);
		info.Top = std::round(edgeY);
		info.Width = std::round(w);
		info.Height = std::round(h);
		info.Graphics = (state != AnimState::Default ? _metadata->FindAnimation(state) : nullptr);
		info.CurrentPointerId = -1;
		info.Align = align;
		return info;
	}

	HUD::TouchButtonInfo HUD::CreateTouchButton(PlayerAction action, AnimState state, Alignment align, float x, float y, float w, float h)
	{
		TouchButtonInfo info;
		info.Action = action;
		info.Left = std::round(x * LevelHandler::DefaultWidth * 0.5f);
		info.Top = std::round(y * LevelHandler::DefaultWidth * 0.5f);
		info.Width = std::round(w * LevelHandler::DefaultWidth * 0.5f);
		info.Height = std::round(h * LevelHandler::DefaultWidth * 0.5f);
		info.Graphics = (state != AnimState::Default ? _metadata->FindAnimation(state) : nullptr);
		info.CurrentPointerId = -1;
		info.Align = align;
		return info;
	}

	bool HUD::IsOnButton(const HUD::TouchButtonInfo& button, float x, float y)
	{
		float left = button.Left;
		if ((button.Align & Alignment::Right) == Alignment::Right) { left = ViewSize.X - button.Width - left; }
		else if ((button.Align & Alignment::Left) != Alignment::Left) { left = ViewSize.X / 2 - button.Width * 0.5f; }
		if (x < left) return false;

		float top = button.Top;
		if ((button.Align & Alignment::Bottom) == Alignment::Bottom) { top = ViewSize.Y - button.Height - top; }
		if (y < top) return false;

		float right = left + button.Width;
		if (x > right) return false;

		float bottom = top + button.Height;
		if (y > bottom) return false;

		return true;
	}

	void HUD::DrawJoystick(float centerX, float centerY, float outerRadius, float innerRadius, float thumbX, float thumbY)
	{
		auto* shader = ContentResolver::Get().GetShader(PrecompiledShader::TouchCircle);
		if (shader == nullptr) {
			return;
		}

		auto drawCircle = [&](float cx, float cy, float radius, float ringInnerFraction, Colorf color) {
			auto command = RentRenderCommand();
			if (command->GetMaterial().SetShader(shader)) {
				command->GetMaterial().ReserveUniformsDataMemory();
				command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			}
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);

			auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			// texRect: x=inner radius fraction (0=filled, e.g., 0.75=ring), y=softness, zw=unused
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(Vector4f(ringInnerFraction, 0.018f, 0.0f, 0.0f).Data());
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(Vector2f(radius * 2.0f, radius * 2.0f).Data());
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

			command->SetTransformation(Matrix4x4f::Translation(std::round(cx - radius), std::round(cy - radius), 0.0f));
			command->SetLayer(TouchButtonsLayer + 1);

			DrawRenderCommand(command);
		};

		// Outer ring (translucent white ring)
		drawCircle(centerX, centerY, outerRadius, 0.78f, Colorf(1.0f, 1.0f, 1.0f, 0.24f));
		// Outer ring border highlight
		drawCircle(centerX, centerY, outerRadius, 0.90f, Colorf(1.0f, 1.0f, 1.0f, 0.44f));
		// Thumb (filled circle)
		float thumbRadius = outerRadius * 0.28f;
		drawCircle(thumbX, thumbY, thumbRadius, 0.0f, Colorf(1.0f, 1.0f, 1.0f, 0.32f));
	}

	void HUD::UpdateRgbLights(float timeMult, Rendering::PlayerViewport* viewport)
	{
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (!PreferencesCache::EnableRgbLights) {
			_rgbHealthLast = 0.0f;
			return;
		}

		RgbLights& rgbLights = RgbLights::Get();
		if (!rgbLights.IsSupported()) {
			return;
		}

		_rgbLightsTime -= timeMult;
		if (_rgbLightsTime > 0.0f) {
			return;
		}

		_rgbLightsAnim += 0.06f;
		_rgbLightsTime += RgbLights::RefreshRate;

		Actors::ActorBase* actor = viewport->GetTargetActor();
		if (Actors::Player* player = runtime_cast<Actors::Player>(actor)) {
			float health = std::clamp((float)player->_health / player->_maxHealth, 0.0f, 1.0f);
			_rgbHealthLast = lerp(_rgbHealthLast, health, 0.2f);
		} else {
			_rgbHealthLast = 0.0f;
		}
		_rgbAmbientLight = viewport->_ambientLight.W;

		constexpr std::int32_t KeyMax2 = 14;
		Color colors[RgbLights::ColorsSize] {};

		if (auto captionTile = _levelHandler->_tileMap->GetCaptionTile()) {
			for (std::int32_t i = 0; i < std::int32_t(arraySize(KeyLayout)); i++) {
				std::int32_t x = KeyLayout[i] % AURA_KEYBOARD_WIDTH;
				std::int32_t y = KeyLayout[i] / AURA_KEYBOARD_WIDTH;
				Color tileColor = captionTile[y * 32 + x];
				colors[AURA_COLORS_LIMITED_SIZE + i] = ApplyRgbGradientAlpha(tileColor, x, y, _rgbLightsAnim, _rgbAmbientLight);
			}
		}

		std::int32_t percent, percentR, percentG;
		percent = (std::int32_t)(_rgbHealthLast * 255);
		percentG = percent * percent / 255;
		percentR = (255 - (percent - 120) * 2);
		percentR = std::clamp(percentR, 0, 255);

		for (std::int32_t i = 0; i < KeyMax2; i++) {
			std::int32_t intensity = (std::int32_t)((_rgbHealthLast - ((float)i / KeyMax2)) * 255 * KeyMax2);
			intensity = std::clamp(intensity, 0, 200);

			if (intensity > 0) {
				colors[(std::int32_t)AuraLight::Tilde + i] = Color(percentR * intensity / 255, percentG * intensity / 255, 0);
				colors[(std::int32_t)AuraLight::Tab + i] = Color(percentR * intensity / (255 * 12), percentG * intensity / (255 * 12), 0);
			}
		}

		rgbLights.Update(colors);
#endif
	}

	Color HUD::ApplyRgbGradientAlpha(Color color, std::int32_t x, std::int32_t y, float animProgress, float ambientLight)
	{
		constexpr std::int32_t Width = AURA_KEYBOARD_WIDTH;
		constexpr std::int32_t Height = AURA_KEYBOARD_HEIGHT;
		constexpr float AnimationMult = 0.11f;
		constexpr std::int32_t Spacing = 1;

		float ambientLightLower = std::clamp(ambientLight, 0.0f, 0.5f) * 2.0f;
		float ambientLightUpper = (std::clamp(ambientLight, 0.5f, 0.8f) - 0.5f) * 2.0f;

		float distance = sqrtf(powf((float)(Width - x), 2) + powf((float)(Height - y), 2));
		float value = cosf(AnimationMult * distance / (0.1f * Spacing) + animProgress);
		float alpha = lerp(powf((value + 1) * 0.5f, 12.0f) * ambientLightLower, 0.8f, ambientLightUpper);
		return Color(std::clamp((std::uint32_t)(color.R * alpha), 0u, 255u), std::clamp((std::uint32_t)(color.G * alpha), 0u, 255u), std::clamp((std::uint32_t)(color.B * alpha), 0u, 255u));
	}

	AuraLight HUD::KeyToAuraLight(Keys key)
	{
		switch (key) {
			case Keys::Backspace: return AuraLight::Backspace;
			case Keys::Tab: return AuraLight::Tab;
			case Keys::Return: return AuraLight::Enter;
			case Keys::Escape: return AuraLight::Esc;
			case Keys::Space: return AuraLight::Space;
			//case Keys::Quote: return AuraLight::Quote;
			//case Keys::Plus: return AuraLight::Plus;
			case Keys::Comma: return AuraLight::Comma;
			case Keys::Minus: return AuraLight::Minus;
			case Keys::Period: return AuraLight::Period;
			case Keys::Slash: return AuraLight::Slash;
			case Keys::D0: return AuraLight::Zero;
			case Keys::D1: return AuraLight::One;
			case Keys::D2: return AuraLight::Two;
			case Keys::D3: return AuraLight::Three;
			case Keys::D4: return AuraLight::Four;
			case Keys::D5: return AuraLight::Five;
			case Keys::D6: return AuraLight::Six;
			case Keys::D7: return AuraLight::Seven;
			case Keys::D8: return AuraLight::Eight;
			case Keys::D9: return AuraLight::Nine;
			case Keys::Semicolon: return AuraLight::Semicolon;
			case Keys::LeftBracket: return AuraLight::OpenBracket;
			case Keys::Backslash: return AuraLight::Backslash;
			case Keys::RightBracket: return AuraLight::CloseBracket;
			case Keys::Backquote: return AuraLight::Tilde;

			case Keys::A: return AuraLight::A;
			case Keys::B: return AuraLight::B;
			case Keys::C: return AuraLight::C;
			case Keys::D: return AuraLight::D;
			case Keys::E: return AuraLight::E;
			case Keys::F: return AuraLight::F;
			case Keys::G: return AuraLight::G;
			case Keys::H: return AuraLight::H;
			case Keys::I: return AuraLight::I;
			case Keys::J: return AuraLight::J;
			case Keys::K: return AuraLight::K;
			case Keys::L: return AuraLight::L;
			case Keys::M: return AuraLight::M;
			case Keys::N: return AuraLight::N;
			case Keys::O: return AuraLight::O;
			case Keys::P: return AuraLight::P;
			case Keys::Q: return AuraLight::Q;
			case Keys::R: return AuraLight::R;
			case Keys::S: return AuraLight::S;
			case Keys::T: return AuraLight::T;
			case Keys::U: return AuraLight::U;
			case Keys::V: return AuraLight::V;
			case Keys::W: return AuraLight::W;
			case Keys::X: return AuraLight::X;
			case Keys::Y: return AuraLight::Y;
			case Keys::Z: return AuraLight::Z;
			case Keys::Delete: return AuraLight::Delete;

			case Keys::NumPad0: return AuraLight::NumZero;
			case Keys::NumPad1: return AuraLight::NumOne;
			case Keys::NumPad2: return AuraLight::NumTwo;
			case Keys::NumPad3: return AuraLight::NumThree;
			case Keys::NumPad4: return AuraLight::NumFour;
			case Keys::NumPad5: return AuraLight::NumFive;
			case Keys::NumPad6: return AuraLight::NumSix;
			case Keys::NumPad7: return AuraLight::NumSeven;
			case Keys::NumPad8: return AuraLight::NumEight;
			case Keys::NumPad9: return AuraLight::NumNine;
			case Keys::NumPadPeriod: return AuraLight::NumPeriod;
			case Keys::NumPadDivide: return AuraLight::NumSlash;
			case Keys::NumPadMultiply: return AuraLight::NumAsterisk;
			case Keys::NumPadMinus: return AuraLight::NumMinus;
			case Keys::NumPadPlus: return AuraLight::NumPlus;
			case Keys::NumPadEnter: return AuraLight::NumEnter;
			case Keys::NumPadEquals: return AuraLight::NumEnter;

			case Keys::Up: return AuraLight::ArrowUp;
			case Keys::Down: return AuraLight::ArrowDown;
			case Keys::Right: return AuraLight::ArrowRight;
			case Keys::Left: return AuraLight::ArrowLeft;
			case Keys::Insert: return AuraLight::Insert;
			case Keys::Home: return AuraLight::Home;
			case Keys::End: return AuraLight::End;
			case Keys::PageUp: return AuraLight::PageUp;
			case Keys::PageDown: return AuraLight::PageDown;

			case Keys::F1: return AuraLight::F1;
			case Keys::F2: return AuraLight::F2;
			case Keys::F3: return AuraLight::F3;
			case Keys::F4: return AuraLight::F4;
			case Keys::F5: return AuraLight::F5;
			case Keys::F6: return AuraLight::F6;
			case Keys::F7: return AuraLight::F7;
			case Keys::F8: return AuraLight::F8;
			case Keys::F9: return AuraLight::F9;
			case Keys::F10: return AuraLight::F10;
			case Keys::F11: return AuraLight::F11;
			case Keys::F12: return AuraLight::F12;

			case Keys::NumLock: return AuraLight::NumLock;
			case Keys::CapsLock: return AuraLight::CapsLock;
			case Keys::ScrollLock: return AuraLight::ScrollLock;
			case Keys::RShift: return AuraLight::RightShift;
			case Keys::LShift: return AuraLight::LeftShift;
			case Keys::RCtrl: return AuraLight::RightCtrl;
			case Keys::LCtrl: return AuraLight::LeftCtrl;
			case Keys::RAlt: return AuraLight::RightAlt;
			case Keys::LAlt: return AuraLight::LeftAlt;
			case Keys::Pause: return AuraLight::PauseBreak;
			case Keys::Menu: return AuraLight::Menu;

			default: return AuraLight::Unknown;
		}
	}

	HUD::WeaponWheelState::WeaponWheelState()
		: Anim(0.0f), LastIndex(-1), Shown(false)
	{
	}
}