#pragma once

#include "../Common.h"
#include "StringView.h"

#include <ctime>

#if defined(DEATH_USE_WCHAR)
#	include <string>
#endif

#if defined(DEATH_TARGET_WINDOWS)
// Forward declarations to avoid include <CommonWindows.h> here
struct _SYSTEMTIME;
struct _FILETIME;
#endif

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	// Forward declarations for the Death::Containers namespace
	class TimeSpan;

	/**
		@brief Instant in time, typically expressed as a date and time of day
	*/
	class DateTime
	{
	public:
		/** @brief Well-known timezones */
		enum Tz
		{
			/** @brief Specifies time in the current timezone */
			Local,

			// Underscore stands for minus
			GMT_12, GMT_11, GMT_10, GMT_9, GMT_8, GMT_7,
			GMT_6, GMT_5, GMT_4, GMT_3, GMT_2, GMT_1,
			GMT0,
			GMT1, GMT2, GMT3, GMT4, GMT5, GMT6,
			GMT7, GMT8, GMT9, GMT10, GMT11, GMT12, GMT13,

			// Universal Coordinated Time (the new and politically correct name for GMT)
			UTC = GMT0,

			// Europe
			WET = GMT0,					// Western Europe Time
			WEST = GMT1,				// Western Europe Summer Time
			CET = GMT1,					// Central Europe Time
			CEST = GMT2,				// Central Europe Summer Time
			EET = GMT2,					// Eastern Europe Time
			EEST = GMT3,				// Eastern Europe Summer Time
			MSK = GMT3,					// Moscow Time
			MSD = GMT4,					// Moscow Summer Time

			// US and Canada
			AST = GMT_4,				// Atlantic Standard Time
			ADT = GMT_3,				// Atlantic Daylight Time
			EST = GMT_5,				// Eastern Standard Time
			EDT = GMT_4,				// Eastern Daylight Saving Time
			CST = GMT_6,				// Central Standard Time
			CDT = GMT_5,				// Central Daylight Saving Time
			MST = GMT_7,				// Mountain Standard Time
			MDT = GMT_6,				// Mountain Daylight Saving Time
			PST = GMT_8,				// Pacific Standard Time
			PDT = GMT_7,				// Pacific Daylight Saving Time
			HST = GMT_10,				// Hawaiian Standard Time
			AKST = GMT_9,				// Alaska Standard Time
			AKDT = GMT_8,				// Alaska Daylight Saving Time

			// Australia
			A_WST = GMT8,				// Western Standard Time
			A_CST = GMT13 + 1,			// Central Standard Time (+9.5)
			A_EST = GMT10,				// Eastern Standard Time
			A_ESST = GMT11,				// Eastern Summer Time

			// New Zealand
			NZST = GMT12,				// Standard Time
			NZDT = GMT13				// Daylight Saving Time
		};

		/**
			@brief Represents a timezone that can be used in timezone conversions in @ref DateTime
		*/
		class TimeZone
		{
		public:
			TimeZone(Tz tz) noexcept;
			constexpr TimeZone(std::int32_t offset = 0) noexcept : _offset(offset) { }

			/** @brief Returns `true` if it describes local timezone */
			constexpr bool IsLocal() const noexcept
			{
				return (_offset == INT32_MIN);
			}

			/** @brief Returns a time difference from UTC in seconds */
			std::int32_t GetOffset() const noexcept;

		private:
			std::int32_t _offset;
		};

		/**
			@brief Describes a date and time represented by @ref DateTime
		*/
		struct Tm
		{
			std::int32_t Millisecond, Second, Minute, Hour, Day, DayOfYear, Month, Year;

			Tm() noexcept;
			Tm(const struct tm& tm, TimeZone tz) noexcept;

			bool IsValid() const noexcept;

			explicit operator bool() const noexcept { return IsValid(); }

			std::int32_t GetWeekDay() noexcept
			{
				if (_dayOfWeek == -1) {
					ComputeWeekDay();
				}
				return _dayOfWeek;
			}

			void AddMonths(std::int32_t monDiff) noexcept;
			void AddDays(std::int32_t dayDiff) noexcept;

		private:
			void ComputeWeekDay() noexcept;

			TimeZone _tz;
			std::int32_t _dayOfWeek;
		};

		/** @brief Returns @ref DateTime that is set to the current date and time on this computer, expressed as the local time */
		static DateTime Now() noexcept;
		/** @brief Returns @ref DateTime that is set to the current date and time on this computer, expressed as the UTC time */
		static DateTime UtcNow() noexcept;

		/** @brief Returns @ref DateTime created from number of milliseconds since 00:00, Jan 1 1970 UTC */
		static constexpr DateTime FromUnixMilliseconds(std::int64_t value) noexcept
		{
			DateTime dt;
			dt._time = value;
			return dt;
		}

		/** @brief Creates invalid @ref DateTime structure */
		constexpr DateTime() noexcept : _time(INT64_MIN) {}

		/** @brief Creates uninitialized @ref DateTime structure */
		explicit DateTime(NoInitT) noexcept {}

		/** @brief Creates @ref DateTime structure from standard `time_t` value */
		constexpr DateTime(time_t timet) noexcept;
		/** @brief Creates @ref DateTime structure from standard `tm` structure */
		inline DateTime(const struct tm& tm) noexcept;
		/** @brief Creates @ref DateTime structure from partitioned @ref DateTime */
		inline DateTime(const Tm& tm) noexcept;

		/** 
		 * @brief Creates @ref DateTime structure from individual parts
		 *
		 * @param year Years
		 * @param month Months after the year (0-11)
		 * @param day Days after the month (1-31)
		 * @param hour Hours after the day (0-23)
		 * @param minute Minutes after the hour (0-59)
		 * @param second Seconds after the minute (0-59*)
		 * @param milliseconds Milliseconds after the second (0-999)
		 */
		inline DateTime(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour = 0, std::int32_t minute = 0, std::int32_t second = 0, std::int32_t milliseconds = 0) noexcept;

#if defined(DEATH_TARGET_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Creates @ref DateTime structure from Windows® `SYSTEMTIME` structure
		 * 
		 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		DateTime(const SYSTEMTIME& st) noexcept;
#else
		DateTime(const struct _SYSTEMTIME& st) noexcept;
#endif

		/**
		 * @brief Creates @ref DateTime structure from Windows® `FILETIME` structure
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		DateTime(const FILETIME& st) noexcept;
#else
		DateTime(const struct _FILETIME& ft) noexcept;
#endif
#endif

		/** @brief Returns the year component of the date represented by this instance */
		std::int32_t GetYear(TimeZone tz = Local) const noexcept { return Partitioned(tz).Year; }
		/** @brief Returns the month component of the date represented by this instance */
		std::int32_t GetMonth(TimeZone tz = Local) const noexcept { return Partitioned(tz).Month; }
		/** @brief Returns the day of the month represented by this instance */
		std::int32_t GetDay(TimeZone tz = Local) const noexcept { return Partitioned(tz).Day; }
		/** @brief Returns the day of the week represented by this instance */
		std::int32_t GetWeekDay(TimeZone tz = Local) const noexcept { return Partitioned(tz).GetWeekDay(); }
		/** @brief Returns the hour component of the date represented by this instance */
		std::int32_t GetHour(TimeZone tz = Local) const noexcept { return Partitioned(tz).Hour; }
		/** @brief Returns the minute component of the date represented by this instance */
		std::int32_t GetMinute(TimeZone tz = Local) const noexcept { return Partitioned(tz).Minute; }
		/** @brief Returns the second component of the date represented by this instance */
		std::int32_t GetSecond(TimeZone tz = Local) const noexcept { return Partitioned(tz).Second; }
		/** @brief Returns the millisecond component of the date represented by this instance */
		std::int32_t GetMillisecond(TimeZone tz = Local) const noexcept { return Partitioned(tz).Millisecond; }

		/** @brief Sets @ref DateTime structure to the value corresponding to standard `time_t` value */
		constexpr DateTime& Set(time_t timet) noexcept;
		/** @brief Sets @ref DateTime structure to the value corresponding to standard `tm` structure */
		DateTime& Set(const struct tm& tm) noexcept;
		/** @brief Sets @ref DateTime structure to the value corresponding to partitioned @ref DateTime */
		DateTime& Set(const Tm& tm) noexcept;

		/**
		 * @brief Sets @ref DateTime structure from individual parts
		 *
		 * @param year Years
		 * @param month Months after the year (0-11)
		 * @param day Days after the month (1-31)
		 * @param hour Hours after the day (0-23)
		 * @param minute Minutes after the hour (0-59)
		 * @param second Seconds after the minute (0-59*)
		 * @param millisec Milliseconds after the second (0-999)
		 */
		DateTime& Set(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour = 0, std::int32_t minute = 0, std::int32_t second = 0, std::int32_t millisec = 0) noexcept;

		/** @brief Sets the year component of the date represented by this instance */
		DateTime& SetYear(std::int32_t year) noexcept;
		/** @brief Sets the month component of the date represented by this instance */
		DateTime& SetMonth(std::int32_t month) noexcept;
		/** @brief Sets the day of the month represented by this instance */
		DateTime& SetDay(std::int32_t day) noexcept;
		/** @brief Sets the hour component of the date represented by this instance */
		DateTime& SetHour(std::int32_t hour) noexcept;
		/** @brief Sets the minute component of the date represented by this instance */
		DateTime& SetMinute(std::int32_t minute) noexcept;
		/** @brief Sets the second component of the date represented by this instance */
		DateTime& SetSecond(std::int32_t second) noexcept;
		/** @brief Sets the millisecond component of the date represented by this instance */
		DateTime& SetMillisecond(std::int32_t millisecond) noexcept;

		/** @brief Resets the time component of the date represented by this instance to 0:00:00 */
		DateTime& ResetTime() noexcept;

		constexpr bool IsValid() const noexcept { return (_time != INT64_MIN); }

		explicit operator bool() const noexcept { return IsValid(); }

		/** @brief Returns @ref DateTime structure partitioned into individual components */
		Tm Partitioned(TimeZone tz = Local) const noexcept;

		/** @brief Returns number of milliseconds since 00:00, Jan 1 1970 UTC */
		constexpr std::int64_t ToUnixMilliseconds() const noexcept;
		/** @brief Returns number of seconds since 00:00, Jan 1 1970 UTC */
		constexpr time_t GetTicks() const noexcept;

		/** @brief Creates a new @ref DateTime structure moved from the local timezone to a given timezone */
		inline DateTime ToTimezone(TimeZone tz, bool noDST = false) const noexcept;
		/** @brief Creates a new @ref DateTime structure moved from a given timezone to the local timezone */
		inline DateTime FromTimezone(TimeZone tz, bool noDST = false) const noexcept;

		/** @brief Moves @ref DateTime structure from the local timezone to a given timezone */
		void AdjustToTimezone(TimeZone tz, bool noDST = false) noexcept;
		/** @brief Moves @ref DateTime structure from a given timezone to the local timezone */
		void AdjustFromTimezone(TimeZone tz, bool noDST = false) noexcept;

#if defined(DEATH_TARGET_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Returns @ref DateTime structure converted to Windows® `SYSTEMTIME` structure
		 * 
		 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		SYSTEMTIME ToWin32() const noexcept;
#else
		struct _SYSTEMTIME ToWin32() const noexcept;
#endif
#endif

		/** @brief Converts the specified string representation of a date and time to its @ref DateTime equivalent */
		bool TryParse(StringView input, StringView format, StringView* endParse = nullptr) noexcept;
#if defined(DEATH_USE_WCHAR)
		/** @overload */
		bool TryParse(std::wstring_view input, std::wstring_view format, std::wstring_view::const_iterator* endParse = nullptr) noexcept;
#endif

		/** @brief Converts this instance to the string representation */
		String ToString() const noexcept;

		constexpr DateTime& operator+=(const TimeSpan& ts) noexcept;
		constexpr DateTime operator+(const TimeSpan& ts) const noexcept
		{
			DateTime dt(*this);
			dt += ts;
			return dt;
		}

		constexpr DateTime& operator-=(const TimeSpan& ts) noexcept;
		constexpr DateTime operator-(const TimeSpan& ts) const noexcept
		{
			DateTime dt(*this);
			dt -= ts;
			return dt;
		}

		constexpr TimeSpan operator-(const DateTime& dt) const noexcept;

		constexpr bool operator<(const DateTime& dt) const noexcept
		{
			return (_time < dt._time);
		}
		constexpr bool operator<=(const DateTime& dt) const noexcept
		{
			return (_time <= dt._time);
		}
		constexpr bool operator>(const DateTime& dt) const noexcept
		{
			return (_time > dt._time);
		}
		constexpr bool operator>=(const DateTime& dt) const noexcept
		{
			return (_time >= dt._time);
		}
		constexpr bool operator==(const DateTime& dt) const noexcept
		{
			return (_time == dt._time);
		}
		constexpr bool operator!=(const DateTime& dt) const noexcept
		{
			return (_time != dt._time);
		}

	private:
		std::int64_t _time;

		constexpr bool IsInStdRange() const noexcept;
	};

	/**
		@brief Time interval
	*/
	class TimeSpan
	{
	public:
		/** @brief Returns @ref TimeSpan that represents a specified number of milliseconds */
		static constexpr TimeSpan FromMilliseconds(std::int64_t milliseconds) noexcept
		{
			return TimeSpan(milliseconds);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of seconds */
		static constexpr TimeSpan FromSeconds(std::int64_t seconds) noexcept
		{
			return TimeSpan(0, 0, seconds);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of minutes */
		static constexpr TimeSpan FromMinutes(std::int32_t minutes) noexcept
		{
			return TimeSpan(0, minutes, 0);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of hours */
		static constexpr TimeSpan FromHours(std::int32_t hours) noexcept
		{
			return TimeSpan(hours, 0, 0);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of days */
		static constexpr TimeSpan FromDays(std::int32_t days) noexcept
		{
			return FromHours(24 * days);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of weeks */
		static constexpr TimeSpan FromWeeks(std::int32_t days) noexcept
		{
			return FromDays(7 * days);
		}

		/** @brief Creates empty @ref TimeSpan structure */
		constexpr TimeSpan() noexcept : _value(0) {}
		/** @brief Creates uninitialized @ref TimeSpan structure */
		explicit TimeSpan(NoInitT) noexcept {}
		/** @brief Creates @ref TimeSpan structure from milliseconds */
		constexpr TimeSpan(std::int64_t milliseconds) noexcept : _value(milliseconds) {}

		/**
		 * @brief Creates @ref TimeSpan structure from individual parts
		 *
		 * @param hours Hours
		 * @param minutes Minutes after the hour (0-59)
		 * @param seconds Seconds after the minute (0-59*)
		 * @param milliseconds Milliseconds after the second (0-999)
		 */
		constexpr TimeSpan(std::int32_t hours, std::int32_t minutes, std::int64_t seconds = 0, std::int64_t milliseconds = 0) noexcept;
		
		/** @brief Returns the value of this instance expressed in whole weeks */
		constexpr std::int32_t GetTotalWeeks() const noexcept;
		/** @brief Returns the value of this instance expressed in whole days */
		constexpr std::int32_t GetTotalDays() const noexcept;
		/** @brief Returns the value of this instance expressed in whole hours */
		constexpr std::int64_t GetTotalHours() const noexcept;
		/** @brief Returns the value of this instance expressed in whole minutes */
		constexpr std::int64_t GetTotalMinutes() const noexcept;
		/** @brief Returns the value of this instance expressed in whole seconds */
		constexpr std::int64_t GetTotalSeconds() const noexcept;
		/** @brief Returns the value of this instance expressed in whole milliseconds */
		constexpr std::int64_t GetTotalMilliseconds() const noexcept { return _value; }
		
		constexpr std::int64_t GetValue() const noexcept { return _value; }

		/** @brief Converts this instance to the string representation */
		String ToString() const noexcept;

		constexpr TimeSpan& operator+=(const TimeSpan& ts) noexcept
		{
			_value += ts.GetValue();
			return *this;
		}
		constexpr TimeSpan operator+(const TimeSpan& ts) const noexcept
		{
			return TimeSpan(GetValue() + ts.GetValue());
		}

		constexpr TimeSpan& operator-=(const TimeSpan& ts) noexcept
		{
			_value -= ts.GetValue();
			return *this;
		}
		constexpr TimeSpan operator-(const TimeSpan& ts) const noexcept
		{
			return TimeSpan(GetValue() - ts.GetValue());
		}

		constexpr TimeSpan& operator*=(std::int32_t n) noexcept
		{
			_value *= n;
			return *this;
		}
		constexpr TimeSpan operator*(std::int32_t n) const noexcept
		{
			return TimeSpan(GetValue() * n);
		}

		constexpr TimeSpan& operator-() noexcept
		{
			_value = -GetValue();
			return *this;
		}

		constexpr bool operator!() const noexcept
		{
			return (_value == 0);
		}

		constexpr bool operator<(const TimeSpan& ts) const noexcept
		{
			return GetValue() < ts.GetValue();
		}
		constexpr bool operator<=(const TimeSpan& ts) const noexcept
		{
			return GetValue() <= ts.GetValue();
		}
		constexpr bool operator>(const TimeSpan& ts) const noexcept
		{
			return GetValue() > ts.GetValue();
		}
		constexpr bool operator>=(const TimeSpan& ts) const noexcept
		{
			return GetValue() >= ts.GetValue();
		}
		constexpr bool operator==(const TimeSpan& ts) const noexcept
		{
			return GetValue() == ts.GetValue();
		}
		constexpr bool operator!=(const TimeSpan& ts) const noexcept
		{
			return GetValue() != ts.GetValue();
		}

	private:
		std::int64_t _value;
	};

	constexpr DateTime::DateTime(time_t timet) noexcept
		: _time(0)
	{
		Set(timet);
	}

	inline DateTime::DateTime(const struct tm& tm) noexcept
	{
		Set(tm);
	}

	inline DateTime::DateTime(const Tm& tm) noexcept
	{
		Set(tm);
	}

	constexpr DateTime& DateTime::Set(time_t timet) noexcept
	{
		if (timet == (time_t)-1) {
			_time = INT64_MIN;
		} else {
			_time = timet;
			_time *= 1000;
		}

		return *this;
	}

	inline DateTime& DateTime::Set(const Tm& tm) noexcept
	{
		return Set(tm.Year, tm.Month, tm.Day, tm.Hour, tm.Minute, tm.Second, tm.Millisecond);
	}

	inline DateTime::DateTime(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour, std::int32_t minute, std::int32_t second, std::int32_t milliseconds) noexcept
	{
		Set(year, month, day, hour, minute, second, milliseconds);
	}

	constexpr std::int64_t DateTime::ToUnixMilliseconds() const noexcept
	{
		return _time;
	}

	constexpr time_t DateTime::GetTicks() const noexcept
	{
		if (!IsInStdRange()) {
			return (time_t)-1;
		}
		return (time_t)(_time / 1000);
	}

	inline DateTime DateTime::ToTimezone(DateTime::TimeZone tz, bool noDST) const noexcept
	{
		DateTime dt(*this);
		dt.AdjustToTimezone(tz, noDST);
		return dt;
	}

	inline DateTime DateTime::FromTimezone(DateTime::TimeZone tz, bool noDST) const noexcept
	{
		DateTime dt(*this);
		dt.AdjustFromTimezone(tz, noDST);
		return dt;
	}

	constexpr DateTime& DateTime::operator+=(const TimeSpan& ts) noexcept
	{
		_time += ts.GetValue();
		return *this;
	}

	constexpr DateTime& DateTime::operator-=(const TimeSpan& ts) noexcept
	{
		_time -= ts.GetValue();
		return *this;
	}

	constexpr TimeSpan DateTime::operator-(const DateTime& dt) const noexcept
	{
		return TimeSpan(ToUnixMilliseconds() - dt.ToUnixMilliseconds());
	}

	constexpr bool DateTime::IsInStdRange() const noexcept
	{
		return (_time >= 0 && (_time / 1000) < INT32_MAX);
	}

	constexpr TimeSpan::TimeSpan(std::int32_t hours, std::int32_t minutes, std::int64_t seconds, std::int64_t milliseconds) noexcept
		: _value(0)
	{
		_value = hours;
		_value *= 60;
		_value += minutes;
		_value *= 60;
		_value += seconds;
		_value *= 1000;
		_value += milliseconds;
	}

	constexpr std::int64_t TimeSpan::GetTotalSeconds() const noexcept
	{
		return _value / 1000;
	}

	constexpr std::int64_t TimeSpan::GetTotalMinutes() const noexcept
	{
		return _value / (60 * 1000LL);
	}

	constexpr std::int64_t TimeSpan::GetTotalHours() const noexcept
	{
		return _value / (60 * 60 * 1000LL);
	}

	constexpr std::int32_t TimeSpan::GetTotalDays() const noexcept
	{
		return static_cast<std::int32_t>(_value / (24 * 60 * 60 * 1000LL));
	}

	constexpr std::int32_t TimeSpan::GetTotalWeeks() const noexcept
	{
		return static_cast<std::int32_t>(_value / (7 * 24 * 60 * 60 * 1000LL));
	}

}}