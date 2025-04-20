// Copyright 2007-2010 Baptiste Lepilleur and The JsonCpp Authors
// Distributed under MIT license, or public domain if desired and
// recognized in your jurisdiction.
// See file LICENSE for detail or copy at http://jsoncpp.sourceforge.net/LICENSE

#ifndef JSON_WRITER_H_INCLUDED
#define JSON_WRITER_H_INCLUDED

#include "value.h"

#include <ostream>
#include <string>
#include <vector>

// Disable warning C4251: <data member>: <type> needs to have dll-interface to
// be used by...
#if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING) && defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251)
#endif // if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)

#pragma pack(push)
#pragma pack()

namespace Json
{
	class Value;

	/**
	 *
	 * Usage:
	 *  @cpp
	 *  using namespace Json;
	 *  void writeToStdout(StreamWriter::Factory const& factory, Value const& value)
	 * { std::unique_ptr<StreamWriter> const writer( factory.newStreamWriter());
	 *    writer->write(value, &std::cout);
	 *    std::cout << std::endl;  // add lf and flush
	 *  }
	 *  @ce
	 */
	class JSON_API StreamWriter {
	protected:
		OStream* sout_; // not owned; will not delete
	public:
		StreamWriter();
		virtual ~StreamWriter();
		/** Write Value into document as configured in sub-class.
		 *   Do not take ownership of sout, but maintain a reference during function.
		 *   \pre sout != NULL
		 *   @return zero on success (For now, we always return zero, so check the
		 *   stream instead.) @throw std::exception possibly, depending on
		 * configuration
		 */
		virtual int write(Value const& root, OStream* sout) = 0;

		/** @brief A simple abstract factory.
		 */
		class JSON_API Factory {
		public:
			virtual ~Factory();
			/** @brief Allocate a CharReader via operator new().
			 * @throw std::exception if something goes wrong (e.g. invalid settings)
			 */
			virtual StreamWriter* newStreamWriter() const = 0;
		}; // Factory
	}; // StreamWriter

	/** @brief Write into stringstream, then return string, for convenience.
	 * A StreamWriter will be created from the factory, used, and then deleted.
	 */
	StringContainer JSON_API writeString(StreamWriter::Factory const& factory, Value const& root);

	/** @brief Build a StreamWriter implementation.

	* Usage:
	*   \code
	*   using namespace Json;
	*   Value value = ...;
	*   StreamWriterBuilder builder;
	*   builder["commentStyle"] = "None";
	*   builder["indentation"] = "   ";  // or whatever you like
	*   std::unique_ptr<Json::StreamWriter> writer(
	*      builder.newStreamWriter());
	*   writer->write(value, &std::cout);
	*   std::cout << std::endl;  // add lf and flush
	*   \endcode
	*/
	class JSON_API StreamWriterBuilder : public StreamWriter::Factory {
	public:
		// Note: We use a Json::Value so that we can add data-members to this class
		// without a major version bump.
		/** Configuration of this builder.
		 *  Available settings (case-sensitive):
		 *  - "commentStyle": "None" or "All"
		 *  - "indentation":  "<anything>".
		 *  - Setting this to an empty string also omits newline characters.
		 *  - "enableYAMLCompatibility": false or true
		 *  - slightly change the whitespace around colons
		 *  - "dropNullPlaceholders": false or true
		 *  - Drop the "null" string from the writer's output for nullValues.
		 *    Strictly speaking, this is not valid JSON. But when the output is being
		 *    fed to a browser's JavaScript, it makes for smaller output and the
		 *    browser can handle the output just fine.
		 *  - "useSpecialFloats": false or true
		 *  - If true, outputs non-finite floating point values in the following way:
		 *    NaN values as "NaN", positive infinity as "Infinity", and negative
		 *  infinity as "-Infinity".
		 *  - "precision": int
		 *  - Number of precision digits for formatting of real values.
		 *  - "precisionType": "significant"(default) or "decimal"
		 *  - Type of precision for formatting of real values.
		 *  - "emitUTF8": false or true
		 *  - If true, outputs raw UTF8 strings instead of escaping them.

		 *  You can examine 'settings_` yourself
		 *  to see the defaults. You can also write and read them just like any
		 *  JSON Value.
		 *  \sa setDefaults()
		 */
		Json::Value settings_;

		StreamWriterBuilder();
		~StreamWriterBuilder() override;

		/**
		 * @throw std::exception if something goes wrong (e.g. invalid settings)
		 */
		StreamWriter* newStreamWriter() const override;

		/** @return true if 'settings' are legal and consistent;
		 *   otherwise, indicate bad settings via 'invalid'.
		 */
		bool validate(Json::Value* invalid) const;
		/** A simple way to update a specific setting.
		 */
		Value& operator[](const StringContainer& key);

		/** Called by ctor, but you can use this to reset settings_.
		 * \pre 'settings' != NULL (but Json::null is fine)
		 * \remark Defaults:
		 * \snippet src/lib_json/json_writer.cpp StreamWriterBuilderDefaults
		 */
		static void setDefaults(Json::Value* settings);
	};

#if defined(JSON_HAS_INT64)
	StringContainer JSON_API valueToString(Int value);
	StringContainer JSON_API valueToString(UInt value);
#endif // if defined(JSON_HAS_INT64)
	StringContainer JSON_API valueToString(LargestInt value);
	StringContainer JSON_API valueToString(LargestUInt value);
	StringContainer JSON_API valueToString(
		double value, unsigned int precision = Value::defaultRealPrecision,
		PrecisionType precisionType = PrecisionType::significantDigits);
	StringContainer JSON_API valueToString(bool value);
	StringContainer JSON_API valueToQuotedString(const char* value);
	StringContainer JSON_API valueToQuotedString(const char* value, size_t length);

	/// @brief Output using the StyledStreamWriter.
	/// \see Json::operator>>()
	JSON_API OStream& operator<<(OStream&, const Value& root);

} // namespace Json

#pragma pack(pop)

#if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)
#pragma warning(pop)
#endif // if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)

#endif // JSON_WRITER_H_INCLUDED
