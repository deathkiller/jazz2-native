#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../WeaponDefinition.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
	/**
		@brief A weapon a script library of JJ2+ offers, as MLLE sets it up
	*/
	struct ScriptWeaponClass
	{
		/** @brief File name of the library */
		String Library;
		/** @brief Script code that creates the weapon, such as `ArcaneWeapons::TornadoGun::Weapon()` */
		String Constructor;
	};

	/** @brief Most weapons of a script, MLLE sets up the first nine */
	constexpr std::int32_t MaxScriptWeapons = 9;

	/** @brief Type of a parameter of a weapon of a script, as `jjSTREAM` keeps it */
	enum class ScriptParamType : std::uint8_t
	{
		Bool,			/**< One byte, 0 or 1 */
		Int8,			/**< One byte */
		UInt8,			/**< One byte */
		Int16,			/**< Two bytes */
		UInt16,			/**< Two bytes */
		Int32,			/**< Four bytes */
		UInt32,			/**< Four bytes */
		Int64,			/**< Eight bytes */
		UInt64,			/**< Eight bytes */
		Float,			/**< Four bytes */
		Double,			/**< Eight bytes */
		String			/**< Four bytes of the length and the text */
	};

	/** @brief A named value of a parameter of a weapon of a script */
	struct ScriptParamChoice
	{
		/** @brief Name */
		String Name;
		/** @brief Value */
		double Value = 0.0;
	};

	/** @brief A parameter of a weapon of a script, in the order the library reads them */
	struct ScriptWeaponParam
	{
		/** @brief Name shown in the editor */
		String Name;
		/** @brief What it does, shown as a tooltip */
		String Description;
		/** @brief Type */
		ScriptParamType Type = ScriptParamType::Bool;
		/** @brief Value of a number or a choice when the weapon is chosen */
		double Default = 0.0;
		/** @brief Value of a text when the weapon is chosen */
		String DefaultText;
		/** @brief Smallest value, the smallest of the type if it's not a number */
		double Min;
		/** @brief Largest value, the largest of the type if it's not a number */
		double Max;
		/** @brief Named values, a choice of them is shown instead of a number if there are any */
		SmallVector<ScriptParamChoice, 0> Choices;

		ScriptWeaponParam();
	};

	/** @brief Where a description of a weapon of a script comes from */
	enum class ScriptWeaponTemplateSource : std::uint8_t
	{
		Json,			/**< A description of the editor (`.json`) */
		MlleIni,		/**< A description of MLLE (`.ini`) */
		Script			/**< Guessed from the library, see @ref GuessScriptWeaponTemplate() */
	};

	/**
		@brief Description of a weapon of a script, its library and its parameters

		Read from `Editor/Weapons/` of the content and of the user content, see @ref FindScriptWeaponTemplates().
	*/
	struct ScriptWeaponTemplate
	{
		/** @brief Name */
		String Name;
		/** @brief File name of the library */
		String Library;
		/** @brief Script code that creates the weapon */
		String Constructor;
		/** @brief Absolute path of its image, empty for none */
		String Image;
		/** @brief Callbacks of the level script that have to pass on to the hook of the weapons, as `onMain` */
		SmallVector<String, 0> Hooks;
		/** @brief Parameters */
		SmallVector<ScriptWeaponParam, 0> Params;
		/** @brief Where it comes from */
		ScriptWeaponTemplateSource Source = ScriptWeaponTemplateSource::Json;
		/** @brief File it comes from */
		String SourcePath;
		/** @brief Whether all parameters are known, a guess may know only the first of them */
		bool Complete = true;
	};

	/** @brief Value of a parameter of a weapon of a script */
	struct ScriptParamValue
	{
		/** @brief Value of a number, a choice or a bool */
		double Number = 0.0;
		/** @brief Value of a text */
		String Text;
	};

	/**
	 * @brief Reads the weapons of a description of MLLE, an `.ini` file with a section of each weapon
	 *
	 * The keys are `Name`, `ImageFilename`, `LibraryFilename`, `Initialization`, `Hooks` and `Options`, the options
	 * `Name:type[:default]` separated by `|`. Returns how many were read.
	 */
	std::int32_t ReadMlleWeaponTemplates(StringView text, StringView sourcePath, SmallVectorImpl<ScriptWeaponTemplate>& templates);
	/**
	 * @brief Reads the weapons of a description of the editor, a `.json` file
	 *
	 * An object or an array of them, or an object with `Weapons`, each with `Name`, `Library`, `Constructor`, `Image`,
	 * `Hooks` and `Params`, each parameter with `Name`, `Type`, `Default`, `Min`, `Max`, `Description` and `Options`
	 * of `Name` and `Value`. Returns how many were read, -1 if the text isn't JSON.
	 */
	std::int32_t ReadWeaponTemplates(StringView text, StringView sourcePath, SmallVectorImpl<ScriptWeaponTemplate>& templates);
	/** @brief Reads all descriptions of weapons (`.json` and `.ini`) in directories */
	void FindScriptWeaponTemplates(Death::Containers::ArrayView<const String> directories, SmallVectorImpl<ScriptWeaponTemplate>& templates);
	/**
	 * @brief Guesses the parameters of a weapon from the text of its library
	 *
	 * The function of the weapon that gets a `jjSTREAM@` is looked for in the namespace of the weapon, and what it
	 * pops from it, in order, by the declarations of what it pops into, in the library and then in @p includes (as
	 * `MLLE-Weapons.asc`), a weapon without such a function takes none. Returns `false` if the class of the weapon
	 * isn't found, a guess that stops at a value of an unknown type isn't @ref ScriptWeaponTemplate::Complete.
	 */
	bool GuessScriptWeaponTemplate(StringView library, StringView text, StringView constructor, ScriptWeaponTemplate& result,
		Death::Containers::ArrayView<const StringView> includes = {});

	/** @brief Returns the size of a value of a parameter of a type, 0 for a text */
	std::int32_t GetScriptParamSize(ScriptParamType type);
	/** @brief Returns the parameters as the weapon has them when it's chosen */
	void MakeDefaultScriptParams(const ScriptWeaponTemplate& weaponTemplate, SmallVectorImpl<std::uint8_t>& data);
	/**
	 * @brief Reads the values of the parameters, those missing are as the template has them
	 *
	 * Returns how many bytes the parameters of the template take, the rest is beyond what it describes.
	 */
	std::size_t ReadScriptParams(const ScriptWeaponTemplate& weaponTemplate, Death::Containers::ArrayView<const std::uint8_t> data,
		SmallVectorImpl<ScriptParamValue>& values);
	/** @brief Writes the values of the parameters, followed by @p rest */
	void WriteScriptParams(const ScriptWeaponTemplate& weaponTemplate, Death::Containers::ArrayView<const ScriptParamValue> values,
		Death::Containers::ArrayView<const std::uint8_t> rest, SmallVectorImpl<std::uint8_t>& data);
	/** @brief Returns `true` if a template is the one of the library and the code that creates a weapon */
	bool IsScriptWeaponTemplateOf(const ScriptWeaponTemplate& weaponTemplate, StringView library, StringView constructor);
	/** @brief Returns the call of the hook of the weapons a callback of the level script has to make, as `processMain(`, empty if it's not known */
	StringView GetScriptWeaponHookCall(StringView callback);

	/**
	 * @brief Finds the weapons in the text of a script library
	 *
	 * A weapon is a class that isn't abstract and derives from `MLLEWeaponApply` or from `MLLEWeapons::WeaponInterface`,
	 * in the namespaces around it. A class of the standard weapon interface alone (`se::WeaponInterface`) isn't one,
	 * MLLE sets it up by a wrapper. Comments and strings are skipped.
	 */
	void FindScriptWeapons(StringView library, StringView text, SmallVectorImpl<ScriptWeaponClass>& weapons);
	/** @brief Finds the weapons of all script libraries (`*.asc`) in directories, the libraries of MLLE itself are left out */
	void FindScriptWeapons(Death::Containers::ArrayView<const String> directories, SmallVectorImpl<ScriptWeaponClass>& weapons);

	/**
	 * @brief Finds the weapons a level script sets up by itself, outside of the lines MLLE generates
	 *
	 * A call such as `LaserBlaster::NonBouncing().Apply(WEAPON::ICE, MLLE::WeaponHook)` --- @p setUps gets what the call is
	 * made on for each weapon, by @ref WeaponType, empty for the weapons the script leaves alone. Comments are skipped.
	 */
	void FindHandWrittenScriptWeapons(StringView script, String (&setUps)[MaxScriptWeapons]);

	/**
	 * @brief Returns the text of a level script with the lines MLLE generates for the weapons of the script made anew
	 *
	 * The lines marked `///@MLLE-Generated` that set the weapons up --- `MLLE::Setup()` and the `#include` and
	 * `#pragma require` of the libraries --- are written for @p weapons, all other lines are kept as they are, with
	 * the line endings the text has. A script without such lines gets them at its start, if it needs them. Returns
	 * the text as it is if nothing changes.
	 */
	String UpdateScriptWeaponLines(StringView script, const WeaponsDescription& weapons);
}

#endif
