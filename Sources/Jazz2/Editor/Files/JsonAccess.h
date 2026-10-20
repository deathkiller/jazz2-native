#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

#include <jsoncpp/json.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Reads values from JSON that came from outside the game, without trusting their types

		jsoncpp is built without exceptions, so a conversion to a type the value doesn't have --- e.g. `asInt()`
		on a string --- ends the process. Hand-edited settings, rules files next to tile sets and descriptions in
		level scripts are all read through these, a value of an unexpected type gives the default instead.
	*/
	namespace JsonAccess
	{
		/** @brief Returns a member of an object, or a null value if it's not an object or has no such member */
		inline const Json::Value& Member(const Json::Value& value, const char* key)
		{
			static const Json::Value null;
			if (!value.isObject()) {
				return null;
			}
			const Json::Value* found = value.find(key, key + std::strlen(key));
			return (found != nullptr ? *found : null);
		}

		/** @brief Returns an integer value, or the default if it's not a number in range */
		inline std::int32_t ToInt(const Json::Value& value, std::int32_t defaultValue)
		{
			if (value.isInt()) {
				return value.asInt();
			}
			if (value.isDouble()) {
				double d = value.asDouble();
				if (std::isfinite(d) && d >= double(std::numeric_limits<std::int32_t>::min()) && d <= double(std::numeric_limits<std::int32_t>::max())) {
					return std::int32_t(d);
				}
			}
			return defaultValue;
		}

		/** @brief Returns an unsigned integer value, or the default if it's not a number in range */
		inline std::uint32_t ToUInt(const Json::Value& value, std::uint32_t defaultValue)
		{
			if (value.isUInt()) {
				return value.asUInt();
			}
			if (value.isDouble()) {
				double d = value.asDouble();
				if (std::isfinite(d) && d >= 0.0 && d <= double(std::numeric_limits<std::uint32_t>::max())) {
					return std::uint32_t(d);
				}
			}
			return defaultValue;
		}

		/** @brief Returns a floating-point value, or the default if it's not a finite number */
		inline float ToFloat(const Json::Value& value, float defaultValue)
		{
			if (value.isNumeric()) {
				double d = value.asDouble();
				if (std::isfinite(d)) {
					return float(d);
				}
			}
			return defaultValue;
		}

		/** @brief Returns a boolean value, or the default if it's not a boolean */
		inline bool ToBool(const Json::Value& value, bool defaultValue)
		{
			return (value.isBool() ? value.asBool() : defaultValue);
		}

		/** @brief Returns a string value, or the default if it's not a string */
		inline String ToString(const Json::Value& value, StringView defaultValue = {})
		{
			const char* begin;
			const char* end;
			if (value.isString() && value.getString(&begin, &end)) {
				return String(begin, std::size_t(end - begin));
			}
			return String(defaultValue);
		}

		/** @brief Returns an integer member of an object, or the default */
		inline std::int32_t GetInt(const Json::Value& object, const char* key, std::int32_t defaultValue)
		{
			return ToInt(Member(object, key), defaultValue);
		}

		/** @brief Returns an unsigned integer member of an object, or the default */
		inline std::uint32_t GetUInt(const Json::Value& object, const char* key, std::uint32_t defaultValue)
		{
			return ToUInt(Member(object, key), defaultValue);
		}

		/** @brief Returns a floating-point member of an object, or the default */
		inline float GetFloat(const Json::Value& object, const char* key, float defaultValue)
		{
			return ToFloat(Member(object, key), defaultValue);
		}

		/** @brief Returns a boolean member of an object, or the default */
		inline bool GetBool(const Json::Value& object, const char* key, bool defaultValue)
		{
			return ToBool(Member(object, key), defaultValue);
		}

		/** @brief Returns a string member of an object, or the default */
		inline String GetString(const Json::Value& object, const char* key, StringView defaultValue = {})
		{
			return ToString(Member(object, key), defaultValue);
		}
	}
}

#endif
