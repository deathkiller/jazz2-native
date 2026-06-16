#include "ScoreboardSection.h"

#if defined(WITH_MULTIPLAYER)

#include "InGameMenu.h"
#include "../../Multiplayer/MpLevelHandler.h"

#include "../../../nCine/I18n.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::UI::Menu
{
	namespace
	{
		constexpr float RowHeight = 16.0f;
		constexpr float HeaderHeight = 40.0f;
	}

	ScoreboardSection::ScoreboardSection()
		: _scrollOffset(0), _animation(0.0f)
	{
	}

	void ScoreboardSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_scrollOffset = 0;
		_animation = 0.0f;
	}

	std::int32_t ScoreboardSection::GetVisibleRows() const
	{
		Recti contentBounds = _root->GetContentBounds();
		std::int32_t rows = (std::int32_t)((contentBounds.H - HeaderHeight) / RowHeight);
		return std::max(1, rows);
	}

	void ScoreboardSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.02f, 1.0f);
		}

		auto inGameMenu = runtime_cast<InGameMenu>(_root);
		auto* mpLevelHandler = (inGameMenu != nullptr ? inGameMenu->GetMultiplayerHandler() : nullptr);
		if (mpLevelHandler == nullptr) {
			_root->LeaveSection();
			return;
		}

		std::int32_t count = (std::int32_t)mpLevelHandler->GetScoreboard().size();
		std::int32_t maxScroll = std::max(0, count - GetVisibleRows());

		if (_root->ActionHit(PlayerAction::Menu) || _root->ActionHit(PlayerAction::Fire)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		} else if (_root->ActionHit(PlayerAction::Up)) {
			if (_scrollOffset > 0) {
				_scrollOffset--;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.4f);
			} else {
				// Scrolled past the top - return to the previous section (the pause menu)
				//_root->PlaySfx("MenuSelect"_s, 0.5f);
				//_root->LeaveSection();
			}
		} else if (_root->ActionHit(PlayerAction::Down)) {
			if (_scrollOffset < maxScroll) {
				_scrollOffset++;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.4f);
			} else {
				// Scrolled past the bottom - return to the previous section (the pause menu)
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

		const auto& rows = mpLevelHandler->GetScoreboard();
		MpGameMode gameMode = mpLevelHandler->GetGameMode();
		bool teamMode = IsTeamGameMode(gameMode);

		// Mode-specific secondary column
		StringView extraLabel;
		bool hasExtra = false;
		if (gameMode == MpGameMode::Race || gameMode == MpGameMode::TeamRace) {
			extraLabel = _("Laps"); hasExtra = true;
		} else if (gameMode == MpGameMode::TreasureHunt || gameMode == MpGameMode::TeamTreasureHunt) {
			extraLabel = _("Treasure"); hasExtra = true;
		}

		Recti contentBounds = _root->GetContentBounds();
		float left = contentBounds.X + contentBounds.W * 0.06f;
		float width = contentBounds.W * 0.88f;
		float top = contentBounds.Y + 10.0f;
		std::int32_t charOffset = 0;

		// Column x positions (as a fraction of the table width)
		float colName = left + width * 0.04f;
		float colExtra = left + width * 0.60f;
		float colK = left + width * 0.70f;
		float colD = left + width * 0.78f;
		float colPts = left + width * 0.88f;
		float colPing = left + width * 0.98f;

		// Title
		_root->DrawStringShadow(_("Scoreboard"), charOffset, contentBounds.X + contentBounds.W * 0.5f, top, IMenuContainer::FontLayer + 10,
			Alignment::Top, Font::DefaultColor, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

		float headerY = top + 20.0f;
		Colorf headerColor = Colorf(0.5f, 0.5f, 0.5f, 0.5f);
		_root->DrawStringShadow(_("Player"), charOffset, colName, headerY, IMenuContainer::FontLayer,
			Alignment::TopLeft, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		if (hasExtra) {
			_root->DrawStringShadow(extraLabel, charOffset, colExtra, headerY, IMenuContainer::FontLayer,
				Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		}
		_root->DrawStringShadow("K"_s, charOffset, colK, headerY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		_root->DrawStringShadow("D"_s, charOffset, colD, headerY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		_root->DrawStringShadow(_("Pts"), charOffset, colPts, headerY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		_root->DrawStringShadow(_("Ping"), charOffset, colPing, headerY, IMenuContainer::FontLayer,
			Alignment::Top, headerColor, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

		std::int32_t visibleRows = GetVisibleRows();
		std::int32_t count = (std::int32_t)rows.size();
		std::int32_t scroll = std::min(_scrollOffset, std::max(0, count - visibleRows));

		float rowY = headerY + 16.0f;
		char buffer[32];
		for (std::int32_t i = scroll; i < count && i < scroll + visibleRows; i++) {
			const auto& row = rows[i];

			Colorf nameColor = (teamMode ? GetTeamColor(row.Team) : Font::DefaultColor);
			if (row.IsLocal) {
				// Highlight the local player's row
				_root->DrawSolid(left, rowY + RowHeight * 0.5f, IMenuContainer::MainLayer - 10, Alignment::Left,
					Vector2f(width, RowHeight), Colorf(1.0f, 1.0f, 1.0f, 0.1f));
			}

			std::size_t rankLength = formatInto(buffer, "{}.", i + 1);
			_root->DrawStringShadow({ buffer, rankLength }, charOffset, left, rowY, IMenuContainer::FontLayer,
				Alignment::TopLeft, Colorf(0.5f, 0.5f, 0.5f, 0.5f), 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			_root->DrawStringShadow(row.Name, charOffset, colName, rowY, IMenuContainer::FontLayer,
				Alignment::TopLeft, nameColor, 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			if (hasExtra) {
				std::size_t length = formatInto(buffer, "{}", row.Extra);
				_root->DrawStringShadow({ buffer, length }, charOffset, colExtra, rowY, IMenuContainer::FontLayer,
					Alignment::Top, Font::DefaultColor, 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			}

			std::size_t length = formatInto(buffer, "{}", row.Kills);
			_root->DrawStringShadow({ buffer, length }, charOffset, colK, rowY, IMenuContainer::FontLayer,
				Alignment::Top, Font::DefaultColor, 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			length = formatInto(buffer, "{}", row.Deaths);
			_root->DrawStringShadow({ buffer, length }, charOffset, colD, rowY, IMenuContainer::FontLayer,
				Alignment::Top, Font::DefaultColor, 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			length = formatInto(buffer, "{}", row.Points);
			_root->DrawStringShadow({ buffer, length }, charOffset, colPts, rowY, IMenuContainer::FontLayer,
				Alignment::Top, Font::DefaultColor, 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);

			if (row.PingMs < 0) {
				_root->DrawStringShadow("-"_s, charOffset, colPing, rowY, IMenuContainer::FontLayer,
					Alignment::Top, Colorf(0.5f, 0.5f, 0.5f, 0.5f), 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			} else {
				length = formatInto(buffer, "{}", row.PingMs);
				_root->DrawStringShadow({ buffer, length }, charOffset, colPing, rowY, IMenuContainer::FontLayer,
					Alignment::Top, Font::DefaultColor, 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
			}

			rowY += RowHeight;
		}

		// Scroll hints
		if (scroll > 0) {
			_root->DrawStringShadow("^"_s, charOffset, contentBounds.X + contentBounds.W * 0.5f, headerY + 13.0f, IMenuContainer::FontLayer,
				Alignment::Top, Colorf(0.7f, 0.7f, 0.7f, 0.5f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		}
		if (scroll + visibleRows < count) {
			_root->DrawStringShadow("v"_s, charOffset, contentBounds.X + contentBounds.W * 0.5f, rowY, IMenuContainer::FontLayer,
				Alignment::Top, Colorf(0.7f, 0.7f, 0.7f, 0.5f), 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
		}
	}

	void ScoreboardSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			// A tap anywhere returns to the previous section
			_root->LeaveSection();
		}
	}
}

#endif
