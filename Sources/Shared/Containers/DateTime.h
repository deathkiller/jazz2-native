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
		@brief Represents an instant in time, typically expressed as a date and time of day
	*/
	class DateTime
	{
	public:
		enum Tz
		{
			// The time in the current time zone
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
			@brief Represents a timezone that can be used by timezone conversions in @ref DateTime
		*/
		class TimeZone
		{
		public:
			TimeZone(Tz tz);
			TimeZone(std::int32_t offset = 0) : _offset(offset) { }

			bool IsLocal() const
			{
				return (_offset == -1);
			}

			std::int32_t GetOffset() const;

		private:
			std::int32_t _offset;
		};

		/**
			@brief Describes a date and time represented by @ref DateTime
		*/
		struct Tm
		{
			std::int32_t Millisecond, Second, Minute, Hour, Day, DayOfYear, Month, Year;

			Tm();
			Tm(const struct tm& tm, const TimeZone tz);

			bool IsValid() const;

			std::int32_t GetWeekDay()
			{
				if (_dayOfWeek == -1) {
					ComputeWeekDay();
				}
				return _dayOfWeek;
			}

			void AddMonths(std::int32_t monDiff);
			void AddDays(std::int32_t dayDiff);

		private:
			void ComputeWeekDay();

			TimeZone _tz;
			std::int32_t _dayOfWeek;
		};

		static DateTime Now();
		static DateTime UtcNow();

		DateTime() : _time(INT64_MIN) { }

		inline DateTime(time_t timet);
		inline DateTime(const struct tm& tm);
		inline DateTime(const Tm& tm);

		inline DateTime(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour = 0, std::int32_t minute = 0, std::int32_t second = 0, std::int32_t millisec = 0);

#if defined(DEATH_TARGET_WINDOWS)
		DateTime(const struct _SYSTEMTIME& st);
		DateTime(const struct _FILETIME& ft);
#endif

		std::int32_t GetYear(const TimeZone tz = Local) const { return Partitioned(tz).Year; }
		std::int32_t GetMonth(const TimeZone tz = Local) const { return Partitioned(tz).Month; }
		std::int32_t GetDay(const TimeZone tz = Local) const { return Partitioned(tz).Day; }
		std::int32_t GetWeekDay(const TimeZone tz = Local) const { return Partitioned(tz).GetWeekDay(); }
		std::int32_t GetHour(const TimeZone tz = Local) const { return Partitioned(tz).Hour; }
		std::int32_t GetMinute(const TimeZone tz = Local) const { return Partitioned(tz).Minute; }
		std::int32_t GetSecond(const TimeZone tz = Local) const { return Partitioned(tz).Second; }
		std::int32_t GetMillisecond(const TimeZone tz = Local) const { return Partitioned(tz).Millisecond; }

		inline DateTime& Set(time_t timet);
		DateTime& Set(const Tm& tm);
		DateTime& Set(const struct tm& tm);

		DateTime& Set(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour = 0, std::int32_t minute = 0, std::int32_t second = 0, std::int32_t millisec = 0);

		DateTime& SetYear(std::int32_t year);
		DateTime& SetMonth(std::int32_t month);
		DateTime& SetDay(std::int32_t day);
		DateTime& SetHour(std::int32_t hour);
		DateTime& SetMinute(std::int32_t minute);
		DateTime& SetSecond(std::int32_t second);
		DateTime& SetMillisecond(std::int32_t millisecond);

		DateTime& ResetTime();

		inline bool IsValid() const { return (_time != INT64_MIN); }

		Tm Partitioned(const TimeZone tz = Local) const;

		std::int64_t GetValue() const;
		time_t GetTicks() const;

		inline DateTime ToTimezone(const TimeZone tz, bool noDST = false) const;
		inline DateTime FromTimezone(const TimeZone tz, bool noDST = false) const;

		void AdjustToTimezone(const TimeZone tz, bool noDST = false);
		void AdjustFromTimezone(const TimeZone tz, bool noDST = false);

#if defined(DEATH_TARGET_WINDOWS)
		struct _SYSTEMTIME ToWin32() const;
#endif

		bool TryParse(const StringView input, const StringView format, StringView* endParse = nullptr);
#if defined(DEATH_USE_WCHAR)
		bool TryParse(const std::wstring_view input, const std::wstring_view format, std::wstring_view::const_iterator* endParse = nullptr);
#endif

		inline DateTime& operator+=(const TimeSpan& ts);
		inline DateTime operator+(const TimeSpan& ts) const
		{
			DateTime dt(*this);
			dt += ts;
			return dt;
		}

		inline DateTime& operator-=(const TimeSpan& ts);
		inline DateTime operator-(const TimeSpan& ts) const
		{
			DateTime dt(*this);
			dt -= ts;
			return dt;
		}

		inline TimeSpan operator-(const DateTime& dt) const;

		inline bool operator<(const DateTime& dt) const
		{
			return (_time < dt._time);
		}
		inline bool operator<=(const DateTime& dt) const
		{
			return (_time <= dt._time);
		}
		inline bool operator>(const DateTime& dt) const
		{
			return (_time > dt._time);
		}
		inline bool operator>=(const DateTime& dt) const
		{
			return (_time >= dt._time);
		}
		inline bool operator==(const DateTime& dt) const
		{
			return (_time == dt._time);
		}
		inline bool operator!=(const DateTime& dt) const
		{
			return (_time != dt._time);
		}

	private:
		std::int64_t _time;

		inline bool IsInStdRange() const;
	};

	/**
		@brief Represents a time interval
	*/
	class TimeSpan
	{
	public:
		/** @brief Returns @ref TimeSpan that represents a specified number of milliseconds */
		static TimeSpan FromMilliseconds(std::int64_t milliseconds)
		{
			return TimeSpan(0, 0, 0, milliseconds);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of seconds */
		static TimeSpan FromSeconds(std::int64_t seconds)
		{
			return TimeSpan(0, 0, seconds);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of minutes */
		static TimeSpan FromMinutes(std::int32_t minutes)
		{
			return TimeSpan(0, minutes, 0);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of hours */
		static TimeSpan FromHours(std::int32_t hours)
		{
			return TimeSpan(hours, 0, 0);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of days */
		static TimeSpan FromDays(std::int32_t days)
		{
			return FromHours(24 * days);
		}

		/** @brief Returns @ref TimeSpan that represents a specified number of weeks */
		static TimeSpan FromWeeks(std::int32_t days)
		{
			return FromDays(7 * days);
		}

		TimeSpan() { }
		TimeSpan(std::int64_t diff) : _value(diff) { }

		inline TimeSpan(std::int32_t hours, std::int32_t minutes, std::int64_t seconds = 0, std::int64_t milliseconds = 0);
		
		inline std::int32_t GetWeeks() const;
		inline std::int32_t GetDays() const;
		inline std::int32_t GetHours() const;
		inline std::int32_t GetMinutes() const;
		inline std::int64_t GetSeconds() const;

		std::int64_t GetMilliseconds() const { return _value; }
		std::int64_t GetValue() const { return _value; }

		TimeSpan& operator+=(const TimeSpan& ts)
		{
			_value += ts.GetValue();
			return *this;
		}
		inline TimeSpan operator+(const TimeSpan& ts) const
		{
			return TimeSpan(GetValue() + ts.GetValue());
		}

		TimeSpan& operator-=(const TimeSpan& ts)
		{
			_value -= ts.GetValue();
			return *this;
		}
		inline TimeSpan operator-(const TimeSpan& ts) const
		{
			return TimeSpan(GetValue() - ts.GetValue());
		}

		TimeSpan& operator*=(std::int32_t n)
		{
			_value *= n;
			return *this;
		}
		inline TimeSpan operator*(std::int32_t n) const
		{
			return TimeSpan(GetValue() * n);
		}

		TimeSpan& operator-()
		{
			_value = -GetValue();
			return *this;
		}

		bool operator!() const
		{
			return (_value == 0);
		}

		inline bool operator<(const TimeSpan& ts) const
		{
			return GetValue() < ts.GetValue();
		}
		inline bool operator<=(const TimeSpan& ts) const
		{
			return GetValue() <= ts.GetValue();
		}
		inline bool operator>(const TimeSpan& ts) const
		{
			return GetValue() > ts.GetValue();
		}
		inline bool operator>=(const TimeSpan& ts) const
		{
			return GetValue() >= ts.GetValue();
		}
		inline bool operator==(const TimeSpan& ts) const
		{
			return GetValue() == ts.GetValue();
		}
		inline bool operator!=(const TimeSpan& ts) const
		{
			return GetValue() != ts.GetValue();
		}

	private:
		std::int64_t _value;
	};

	inline DateTime::DateTime(time_t timet)
	{
		Set(timet);
	}

	inline DateTime::DateTime(const struct tm& tm)
	{
		Set(tm);
	}

	inline DateTime::DateTime(const Tm& tm)
	{
		Set(tm);
	}

	inline DateTime& DateTime::Set(time_t timet)
	{
		if (timet == (time_t)-1) {
			_time = INT64_MIN;
		} else {
			_time = timet;
			_time *= 1000;
		}

		return *this;
	}

	inline DateTime& DateTime::Set(const Tm& tm)
	{
		return Set(tm.Year, tm.Month, tm.Day, tm.Hour, tm.Minute, tm.Second, tm.Millisecond);
	}

	inline DateTime::DateTime(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour, std::int32_t minute, std::int32_t second, std::int32_t millisec)
	{
		Set(year, month, day, hour, minute, second, millisec);
	}

	inline std::int64_t DateTime::GetValue() const
	{
		return _time;
	}

	inline time_t DateTime::GetTicks() const
	{
		if (!IsInStdRange()) {
			return (time_t)-1;
		}
		return (time_t)(_time / 1000);
	}

	inline DateTime DateTime::ToTimezone(const DateTime::TimeZone tz, bool noDST) const
	{
		DateTime dt(*this);
		dt.AdjustToTimezone(tz, noDST);
		return dt;
	}

	inline DateTime DateTime::FromTimezone(const DateTime::TimeZone tz, bool noDST) const
	{
		DateTime dt(*this);
		dt.AdjustFromTimezone(tz, noDST);
		return dt;
	}

	inline DateTime& DateTime::operator+=(const TimeSpan& ts)
	{
		_time += ts.GetValue();
		return *this;
	}

	inline DateTime& DateTime::operator-=(const TimeSpan& ts)
	{
		_time -= ts.GetValue();
		return *this;
	}

	inline TimeSpan DateTime::operator-(const DateTime& dt) const
	{
		return TimeSpan(GetValue() - dt.GetValue());
	}

	inline bool DateTime::IsInStdRange() const
	{
		return (_time >= 0 && (_time / 1000) < INT32_MAX);
	}

	inline TimeSpan::TimeSpan(std::int32_t hours, std::int32_t minutes, std::int64_t seconds, std::int64_t milliseconds)
	{
		_value = hours;
		_value *= 60;
		_value += minutes;
		_value *= 60;
		_value += seconds;
		_value *= 1000;
		_value += milliseconds;
	}

	inline std::int64_t TimeSpan::GetSeconds() const
	{
		return _value / 1000;
	}

	inline std::int32_t TimeSpan::GetMinutes() const
	{
		return static_cast<std::int32_t>(GetSeconds() / 60);
	}

	inline std::int32_t TimeSpan::GetHours() const
	{
		return GetMinutes() / 60;
	}

	inline std::int32_t TimeSpan::GetDays() const
	{
		return GetHours() / 24;
	}

	inline std::int32_t TimeSpan::GetWeeks() const
	{
		return GetDays() / 7;
	}
}}