#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Events/EventCatalog.h"
#include "../../WeaponDefinition.h"
#include "../../Formats/WeaponsChunk.h"

#include <Containers/Function.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
	class IEditorContext;
	class LevelDocument;

	/**
	 * @brief Shows the weapons of the level
	 *
	 * Each of the weapons can keep the weapon of the game with other options, be a custom weapon the level describes
	 * --- with a preview of its shots and a play-test to try it --- or a weapon the script of the level sets up.
	 */
	void ShowWeaponsDialog(IEditorContext& session);
	/** @brief Reads the weapons of a level, returns why if they can't be changed, see @ref Formats::WeaponsChunk::Read() */
	Formats::WeaponsChunk::ReadResult ReadLevelWeapons(const LevelDocument& document, WeaponsDescription& weapons);
	/** @brief Changes the weapons of the level as an undoable step, changes while a value is dragged are merged until @p final */
	bool ChangeLevelWeapons(IEditorContext& session, StringView name, Death::Containers::Function<void(WeaponsDescription&)>&& change, bool final = true);
	/** @brief Returns the name of a weapon, as the level names it or as the game does */
	String GetLevelWeaponName(const WeaponsDescription& weapons, WeaponType weaponType);
	/** @brief Returns the name of a weapon of the game */
	StringView GetStockWeaponName(WeaponType weaponType);
	/**
	 * @brief Returns the weapons of the level being edited, read again only when they changed
	 *
	 * For what asks for them every frame. They're as the game has them if they can't be read.
	 */
	const WeaponsDescription& GetCachedLevelWeapons(IEditorContext& session);
	/**
	 * @brief Returns the label of an option of a parameter of an event
	 *
	 * An option of a weapon (see @ref EventParamField::IsWeapon) is shown by the name the level gives the weapon, others as
	 * they are.
	 */
	String GetEventOptionLabel(IEditorContext& session, const EventParamField& field, const ParamOption& option);
}

#endif
