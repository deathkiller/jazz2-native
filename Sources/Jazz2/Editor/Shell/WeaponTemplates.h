#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../WeaponDefinition.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <jsoncpp/forwards.h>

#include <memory>
#include <string>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
#endif

	/** @brief An image a template of a custom weapon takes from a PNG file next to it, with how its frames are laid out */
	struct WeaponTemplateImage
	{
		/** @brief Absolute path of the PNG file */
		String Path;
		/** @brief Frames in a row, 0 to tell them by the size of the image as an import does */
		std::int32_t FrameColumns = 0;
		/** @brief Rows of frames, 0 to tell them by the size of the image as an import does */
		std::int32_t FrameRows = 0;
		/** @brief Number of frames, 0 for all of them */
		std::int32_t FrameCount = 0;
		/** @brief Speed of the animation, as `FrameRate` of a metadata file */
		float FrameRate = 10.0f;
		/** @brief Point of a frame that's at the position of the actor */
		std::int32_t HotspotX = 0;
		/** @brief Point of a frame that's at the position of the actor */
		std::int32_t HotspotY = 0;
		/** @brief Whether the hotspot is given, the middle of a frame otherwise */
		bool HasHotspot = false;
	};

	/**
		@brief Template of a custom weapon, read from a `.json` file in `Editor/Weapons/`

		An item with `"Kind": "Custom"` of the same files as the templates of the weapons of scripts (see
		@ref ReadWeaponTemplates()). Its images and sounds are files next to it, embedded in the level when the template
		is applied to one of its weapons, see @ref ApplyCustomWeaponTemplate().
	*/
	struct CustomWeaponTemplate
	{
		/** @brief Name */
		String Name;
		/** @brief Absolute path of its image shown in the editor, empty for none */
		String Image;
		/** @brief File it comes from */
		String SourcePath;
		/** @brief Weapon of the game it's made from */
		WeaponType BasedOn = WeaponType::Blaster;
		/** @brief The item of the file as it is */
		std::shared_ptr<const Json::Value> Description;
	};

	/**
	 * @brief Reads a template of a custom weapon from an item of a `.json` file
	 *
	 * Returns `false` if it isn't one, an item without `"Kind": "Custom"`.
	 */
	bool ReadCustomWeaponTemplate(const Json::Value& value, StringView sourcePath, CustomWeaponTemplate& result);

	/**
	 * @brief Makes a weapon as a template describes it
	 *
	 * @p weapon is a custom weapon made from @ref CustomWeaponTemplate::BasedOn already, what the template doesn't
	 * describe is kept. @p addImage and @p addSound embed a file in the level and return its index, -1 if it can't be,
	 * the sprite or the sound is as in the game then. Returns `false` if any file couldn't be embedded.
	 */
	bool ApplyCustomWeaponTemplate(const CustomWeaponTemplate& weaponTemplate, WeaponDefinition& weapon,
		Function<std::int32_t(const WeaponTemplateImage&)>&& addImage, Function<std::int32_t(StringView)>&& addSound);

	/**
	 * @brief Describes a custom weapon as an item of a template file
	 *
	 * @p imageFile and @p soundFile save an embedded image or sound of the level as a file next to the template and
	 * return its name, empty if it can't be saved.
	 */
	void WriteCustomWeaponTemplate(const WeaponDefinition& weapon, const WeaponsDescription& weapons,
		Function<String(std::int32_t)>&& imageFile, Function<String(std::int32_t)>&& soundFile, Json::Value& result);

	/** @brief Returns the text of a template file, tab-indented, numbers as short as they're typed */
	std::string WriteWeaponTemplateFile(const Json::Value& value);
}

#endif
