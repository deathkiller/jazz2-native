#include "ScoreboardSection.h"

#if defined(WITH_MULTIPLAYER)

#include "InGameMenu.h"
#include "MenuResources.h"
#include "../../Multiplayer/MpLevelHandler.h"

#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/FrameTimer.h"

using namespace Jazz2::Multiplayer;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	namespace
	{
		constexpr float RowHeight = 16.0f;
		// Pixels below the content top where the data rows (the clipped band) begin - leaves room for the title and
		// the column header drawn above the band
		constexpr std::int32_t RowsTop = 46;
		// Small gap kept clear at the bottom of the row band
		constexpr std::int32_t BottomMargin = 6;
	}

	ScoreboardSection::ScoreboardSection()
		: _scrollY(0), _contentHeight(0), _availableHeight(0), _scrollable(false),
			_touchTime(0.0f), _touchSpeed(0.0f), _touchDirection(0), _touchStartedAtBottom(false)
	{
	}

	TransitionInfo ScoreboardSection::GetTransition() const
	{
		// Drop in from the top (and slide back up on close) instead of the default horizontal slide
		TransitionInfo info;
		info.Style = TransitionStyle::SlideVertical;
		return info;
	}

	Recti ScoreboardSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + RowsTop, contentBounds.W, std::max(0, contentBounds.H - RowsTop - BottomMargin));
	}

	void ScoreboardSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_scrollY = 0;
		_navRepeat = 0.0f;
		_touchStart = Vector2f::Zero;
		_touchLast = Vector2f::Zero;
		_touchTime = 0.0f;
		_touchSpeed = 0.0f;
		_touchDirection = 0;
		_touchStartedAtBottom = false;
	}

	void ScoreboardSection::OnUpdate(float timeMult)
	{
		auto inGameMenu = runtime_cast<InGameMenu>(_root);
		auto* mpLevelHandler = (inGameMenu != nullptr ? inGameMenu->GetMultiplayerHandler() : nullptr);
		if (mpLevelHandler == nullptr) {
			_root->LeaveSection();
			return;
		}

		// Recompute the scrollable extents from the current scoreboard and viewport
		Recti contentBounds = _root->GetContentBounds();
		std::int32_t count = (std::int32_t)mpLevelHandler->GetScoreboard().size();
		_availableHeight = GetClipRectangle(contentBounds).H;
		_contentHeight = (std::int32_t)(count * RowHeight);
		_scrollable = (_contentHeight > _availableHeight);
		std::int32_t minScroll = std::min(0, _availableHeight - _contentHeight);

		// Kinetic touch momentum (mirrors ScrollView): coast after release, bounce slightly at the edges
		if (_touchSpeed > 0.0f) {
			if (_touchStart == Vector2f::Zero && _scrollable) {
				float y = _scrollY + (_touchSpeed * (std::int32_t)_touchDirection * TouchKineticDivider * timeMult);
				if (y < minScroll && _touchDirection < 0) {
					y = (float)minScroll;
					_touchDirection = 1;
					_touchSpeed *= TouchKineticDamping;
				} else if (y > 0.0f && _touchDirection > 0) {
					y = 0.0f;
					_touchDirection = -1;
					_touchSpeed *= TouchKineticDamping;
				}
				_scrollY = (std::int32_t)y;
			}

			_touchSpeed = std::max(_touchSpeed - TouchKineticFriction * TouchKineticDivider * timeMult, 0.0f);
		}

		_scrollY = std::clamp(_scrollY, minScroll, 0);
		_touchTime += timeMult;

		// Hold Up/Down to scroll continuously (one row per repeat, matching the menu lists)
		bool navUp, navDown, navSound;
		UpdateNavigation(timeMult, navUp, navDown, navSound);

		if (_root->ActionHit(PlayerAction::Menu) || _root->ActionHit(PlayerAction::Fire)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		} else if (navUp) {
			if (_scrollable && _scrollY < 0) {
				_scrollY = std::min(0, _scrollY + (std::int32_t)RowHeight);
				if (navSound) _root->PlaySfx("MenuSelect"_s, 0.4f);
			}
		} else if (navDown) {
			if (_scrollable && _scrollY > minScroll) {
				_scrollY = std::max(minScroll, _scrollY - (std::int32_t)RowHeight);
				if (navSound) _root->PlaySfx("MenuSelect"_s, 0.4f);
			} else if (_root->ActionHit(PlayerAction::Down)) {
				// Nothing more to scroll - a deliberate (fresh) Down press dismisses the scoreboard and returns to the
				// pause menu. Gated on the fresh press so a hold-scroll that reaches the bottom doesn't kick you out.
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->LeaveSection();
			}
		}
	}

	void ScoreboardSection::OnDraw(Canvas* canvas)
	{
		auto inGameMenu = runtime_cast<InGameMenu>(_root);
		auto* mpLevelHandler = (inGameMenu != nullptr ? inGameMenu->GetMultiplayerHandler() : nullptr);
		if (mpLevelHandler == nullptr) {
			return;
		}

		MpGameMode gameMode = mpLevelHandler->GetGameMode();
		// Mode-specific secondary column - only meaningful once the round is running (laps/treasure are all 0
		// during pre-game and the countdown), so it stays hidden until then.
		bool roundStarted = (mpLevelHandler->GetLevelState() >= MpLevelHandler::LevelState::Running);
		StringView extraLabel;
		bool hasExtra = false;
		if (roundStarted && (gameMode == MpGameMode::Race || gameMode == MpGameMode::TeamRace)) {
			extraLabel = _("Laps"); hasExtra = true;
		} else if (roundStarted && (gameMode == MpGameMode::TreasureHunt || gameMode == MpGameMode::TeamTreasureHunt)) {
			extraLabel = _("Treasure"); hasExtra = true;
		}

		Recti contentBounds = _root->GetContentBounds();
		float left = contentBounds.X + contentBounds.W * 0.06f;
		float width = contentBounds.W * 0.88f;
		float top = contentBounds.Y + 10.0f;
		std::int32_t charOffset = 0;

		// The drop-in animation is now handled by the common section transition (SlideVertical), so the content is
		// drawn at its resting position, full opacity and resting title scale
		constexpr float slideY = 0.0f;
		constexpr float alpha = 1.0f;
		constexpr float titleScale = 0.9f;
		auto faded = [](Colorf color, float a) -> Colorf {
			color.A *= a;
			return color;
		};

		// Column x positions (as a fraction of the table width)
		float colName = left + width * 0.04f;
		float colExtra = left + width * 0.60f;
		float colK = left + width * 0.70f;
		float colD = left + width * 0.78f;
		float colPts = left + width * 0.88f;
		float colPing = left + width * 0.98f;

		// Title
		_root->DrawElement(MenuGlow, 0, contentBounds.X + contentBounds.W * 0.5f, top + slideY + 8.0f, IMenuContainer::MainLayer, Alignment::Center,
			Colorf(1.0f, 1.0f, 1.0f, 0.3f * alpha), 6.0f, 4.0f, true, true);

		_root->DrawStringShadow(_("Scoreboard"), charOffset, contentBounds.X + contentBounds.W * 0.5f, top + slideY + 8.0f, IMenuContainer::FontLayer + 10,
			Alignment::Center, faded(Font::DefaultColor, alpha), titleScale, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

		float headerY = top + 20.0f;
		Colorf headerColor = faded(Colorf(0.5f, 0.5f, 0.5f, 0.5f), alpha);
		_root->DrawStringShadow(_("Player"), charOffset, colName, headerY + slideY, IMenuContainer::FontLayer,
			Alignment::TopLeft, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		if (hasExtra) {
			_root->DrawStringShadow(extraLabel, charOffset, colExtra, headerY + slideY, IMenuContainer::FontLayer,
				Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		}
		_root->DrawStringShadow("K"_s, charOffset, colK, headerY + slideY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		_root->DrawStringShadow("D"_s, charOffset, colD, headerY + slideY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		_root->DrawStringShadow(_("Pts"), charOffset, colPts, headerY + slideY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		_root->DrawStringShadow(_("Ping"), charOffset, colPing, headerY + slideY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

		// Scrollbar on the far right of the band, indicating the scroll position (only when there's overflow)
		if (_scrollable && _contentHeight > 0) {
			Recti clip = GetClipRectangle(contentBounds);
			float trackX = contentBounds.X + contentBounds.W - 6.0f;
			float trackTop = (float)clip.Y;
			float trackH = (float)clip.H;
			float thumbH = std::max(trackH * (float)_availableHeight / (float)_contentHeight, 12.0f);
			float range = (float)(_contentHeight - _availableHeight);
			float pos = (range > 0.0f ? (float)(-_scrollY) / range : 0.0f);
			float thumbY = trackTop + std::clamp(pos, 0.0f, 1.0f) * (trackH - thumbH);

			_root->DrawSolid(trackX, trackTop, IMenuContainer::MainLayer, Alignment::Top,
				Vector2f(2.0f, trackH), faded(Colorf(1.0f, 1.0f, 1.0f, 0.1f), alpha));
			_root->DrawSolid(trackX, thumbY, IMenuContainer::MainLayer, Alignment::Top,
				Vector2f(2.0f, thumbH), faded(Colorf(1.0f, 1.0f, 1.0f, 0.4f), alpha));
		}
	}

	void ScoreboardSection::OnDrawClipped(Canvas* canvas)
	{
		auto inGameMenu = runtime_cast<InGameMenu>(_root);
		auto* mpLevelHandler = (inGameMenu != nullptr ? inGameMenu->GetMultiplayerHandler() : nullptr);
		if (mpLevelHandler == nullptr) {
			return;
		}

		const auto& rows = mpLevelHandler->GetScoreboard();
		if (rows.empty()) {
			return;
		}

		MpGameMode gameMode = mpLevelHandler->GetGameMode();
		bool teamMode = IsTeamGameMode(gameMode);
		bool roundStarted = (mpLevelHandler->GetLevelState() >= MpLevelHandler::LevelState::Running);
		bool raceMode = (gameMode == MpGameMode::Race || gameMode == MpGameMode::TeamRace);
		bool hasExtra = roundStarted && (raceMode || gameMode == MpGameMode::TreasureHunt || gameMode == MpGameMode::TeamTreasureHunt);
		std::uint32_t totalLaps = (raceMode ? mpLevelHandler->GetTotalLaps() : 0);

		Recti contentBounds = _root->GetContentBounds();
		Recti clip = GetClipRectangle(contentBounds);
		float left = contentBounds.X + contentBounds.W * 0.06f;
		float width = contentBounds.W * 0.88f;
		std::int32_t charOffset = 0;

		constexpr float slideY = 0.0f;
		constexpr float alpha = 1.0f;
		auto faded = [](Colorf color, float a) -> Colorf {
			color.A *= a;
			return color;
		};

		float colName = left + width * 0.04f;
		float colExtra = left + width * 0.60f;
		float colK = left + width * 0.70f;
		float colD = left + width * 0.78f;
		float colPts = left + width * 0.88f;
		float colPing = left + width * 0.98f;

		float clipTop = (float)clip.Y;
		float clipBottom = (float)(clip.Y + clip.H);
		std::int32_t count = (std::int32_t)rows.size();
		char buffer[32];
		for (std::int32_t i = 0; i < count; i++) {
			float drawY = clipTop + i * RowHeight + (float)_scrollY + slideY;
			// Skip rows entirely outside the visible band (during the entry slide they start below it and rise in)
			if (drawY + RowHeight < clipTop || drawY > clipBottom) {
				continue;
			}

			const auto& row = rows[i];

			Colorf nameColor = (teamMode ? GetTeamColor(row.Team) : Font::DefaultColor);
			if (row.IsLocal) {
				// Highlight the local player's row
				_root->DrawSolid(left, drawY + RowHeight * 0.5f, IMenuContainer::MainLayer - 10, Alignment::Left,
					Vector2f(width, RowHeight), Colorf(1.0f, 1.0f, 1.0f, 0.1f * alpha));
			}

			std::size_t rankLength = formatInto(buffer, "{}.", i + 1);
			_root->DrawStringShadow({ buffer, rankLength }, charOffset, left, drawY, IMenuContainer::FontLayer,
				Alignment::TopLeft, faded(Colorf(0.5f, 0.5f, 0.5f, 0.5f), alpha), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			_root->DrawStringShadow(row.Name, charOffset, colName, drawY, IMenuContainer::FontLayer,
				Alignment::TopLeft, faded(nameColor, alpha), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			if (hasExtra) {
				std::size_t length;
				if (raceMode) {
					// "currentLap/lapCount" - Extra holds completed laps, so the current lap is +1 (capped at the total)
					std::uint32_t currentLap = (totalLaps > 0 ? std::min(row.Extra + 1, totalLaps) : row.Extra + 1);
					length = formatInto(buffer, "{}/{}", currentLap, totalLaps);
				} else {
					length = formatInto(buffer, "{}", row.Extra);
				}
				_root->DrawStringShadow({ buffer, length }, charOffset, colExtra, drawY, IMenuContainer::FontLayer,
					Alignment::Top, faded(Font::DefaultColor, alpha), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			}

			std::size_t length = formatInto(buffer, "{}", row.Kills);
			_root->DrawStringShadow({ buffer, length }, charOffset, colK, drawY, IMenuContainer::FontLayer,
				Alignment::Top, faded(Font::DefaultColor, alpha), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			length = formatInto(buffer, "{}", row.Deaths);
			_root->DrawStringShadow({ buffer, length }, charOffset, colD, drawY, IMenuContainer::FontLayer,
				Alignment::Top, faded(Font::DefaultColor, alpha), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			length = formatInto(buffer, "{}", row.Points);
			_root->DrawStringShadow({ buffer, length }, charOffset, colPts, drawY, IMenuContainer::FontLayer,
				Alignment::Top, faded(Font::DefaultColor, alpha), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			if (row.PingMs < 0) {
				_root->DrawStringShadow("-"_s, charOffset, colPing, drawY, IMenuContainer::FontLayer,
					Alignment::Top, faded(Colorf(0.5f, 0.5f, 0.5f, 0.5f), alpha), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			} else {
				length = formatInto(buffer, "{}", row.PingMs);
				_root->DrawStringShadow({ buffer, length }, charOffset, colPing, drawY, IMenuContainer::FontLayer,
					Alignment::Top, faded(Font::DefaultColor, alpha), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			}
		}
	}

	void ScoreboardSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					_touchStart = Vector2f(event.pointers[pointerIndex].x * viewSize.X, event.pointers[pointerIndex].y * viewSize.Y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
					_touchSpeed = 0.0f;
					// Remember whether the scroll is already at the bottom (or the list isn't scrollable) - a swipe
					// up from there closes the scoreboard, whereas a swipe up mid-list just scrolls
					std::int32_t minScroll = std::min(0, _availableHeight - _contentHeight);
					_touchStartedAtBottom = (_scrollY <= minScroll);
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * viewSize.X, event.pointers[pointerIndex].y * viewSize.Y);
						if (_scrollable) {
							float delta = touchMove.Y - _touchLast.Y;
							if (delta != 0.0f) {
								_scrollY += (std::int32_t)delta;
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
				bool wasTouch = (_touchStart != Vector2f::Zero);
				float swipeDeltaY = _touchLast.Y - _touchStart.Y;
				// A short, near-stationary touch is a tap (not a drag) and dismisses the scoreboard
				bool tapped = (wasTouch && (_touchStart - _touchLast).SqrLength() <= 100 && _touchTime <= FrameTimer::FramesPerSecond);
				_touchStart = Vector2f::Zero;
				if (tapped) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					break;
				}
				// A swipe up that began with nothing more to scroll (already at the bottom, or a short non-scrollable
				// list) returns to the pause menu - the mirror of the swipe-down that opens the scoreboard there.
				if (wasTouch && _touchStartedAtBottom && swipeDeltaY < -40.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					break;
				}
				// Otherwise it was a drag - leave the kinetic momentum to coast in OnUpdate.
				break;
			}
		}
	}
}

#endif
