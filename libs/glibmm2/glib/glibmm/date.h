// -*- c++ -*-
// Generated by gtkmmproc -- DO NOT MODIFY!
#ifndef _GLIBMM_DATE_H
#define _GLIBMM_DATE_H


/* $Id: date.hg,v 1.6 2005/11/29 15:53:27 murrayc Exp $ */

/* Copyright (C) 2002 The gtkmm Development Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


  #undef G_DISABLE_DEPRECATED //So we can use deprecated functions in our deprecated methods.
 
#include <glibmm/ustring.h>

#include <glib/gdate.h>
#include <glib/gtypes.h>

#ifndef DOXYGEN_SHOULD_SKIP_THIS
extern "C" { struct tm; }
#endif

namespace Glib
{

/** Julian calendar date.
 */
class Date
{
public:
  typedef guint8  Day;
  typedef guint16 Year;

  /** @addtogroup glibmmEnums Enums and Flags */

/**
 * @ingroup glibmmEnums
 */
enum Month
{
  BAD_MONTH,
  JANUARY,
  FEBRUARY,
  MARCH,
  APRIL,
  MAY,
  JUNE,
  JULY,
  AUGUST,
  SEPTEMBER,
  OCTOBER,
  NOVEMBER,
  DECEMBER
};


  /**
 * @ingroup glibmmEnums
 */
enum Weekday
{
  BAD_WEEKDAY,
  MONDAY,
  TUESDAY,
  WEDNESDAY,
  THURSDAY,
  FRIDAY,
  SATURDAY,
  SUNDAY
};


  /**
 * @ingroup glibmmEnums
 */
enum DMY
{
  DAY,
  MONTH,
  YEAR
};


  static const Day     BAD_DAY    = 0;
  static const Year    BAD_YEAR   = 0;
  static const guint32 BAD_JULIAN = 0;

  Date();
  Date(Day day, Month month, Year year);
  explicit Date(guint32 julian_day);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
  explicit Date(const GDate& castitem);
#endif

  void clear();
  /** Clear the date. The cleared dates will not represent an existing date, but will not contain garbage.
   * @param month Month to set.
   */

  /** Parses a user-inputted string str, and try to figure out what date it represents, taking the current locale into account. If the string is successfully parsed, the date will be valid after the call. Otherwise, it will be invalid.   
   * This function is not appropriate for file formats and the like; it isn't very precise, and its exact behavior varies with the locale. It's intended to be a heuristic routine that guesses what the user means by a given string (and it does work pretty well in that capacity).   
   * @param str String to parse.
   */
  void set_parse (const Glib::ustring& str);

  #ifndef GLIBMM_DISABLE_DEPRECATED

  /** Sets the value of a date from a GTime (time_t) value. 
   *
   * @param time GTime value to set.
   *
   * @deprecated Please use set_time(time_t) or set_time(const GTimeVal&).
   */
  void set_time(GTime time);
  #endif // GLIBMM_DISABLE_DEPRECATED


  /** Sets the value of a date from a <type>time_t</type> value. 
   *
   * @param timet time_t value to set
   *
   * @see set_time_current()
   *
   * Since: 2.10
   */
  void set_time(time_t timet);

  /** Sets the value of a date from a GTimeVal value.  Note that the
   * tv_usec member is ignored, because Glib::Date can't make use of the
   * additional precision.
   *
   * @see set_time_current()
   * 
   * @param timeval GTimeVal value to set
   *
   * Since: 2.10
   */
  void set_time(const GTimeVal& timeval);

  void set_time_current();

  /** Sets the month of the year. If the resulting day-month-year triplet is invalid, the date will be invalid.
   * @param month Month to set.
   */
  void set_month(Month month);

  /** Sets the day of the month. If the resulting day-month-year triplet is invalid, the date will be invalid.
   * @param day Day to set.
   */
  void set_day(Day day);

  /** Sets the year. If the resulting day-month-year triplet is invalid, the date will be invalid.
   * @param year Year to set.
   */
  void set_year(Year year);

  /** Sets the value of a day, month, and year.. If the resulting day-month-year triplet is invalid, the date will be invalid.
   * @param day Day to set.
   * @param month Month to set.
   * @param year Year to set.
   */
  void set_dmy(Day day, Month month, Year year);

  /** Sets the value of a GDate from a Julian day number.
   * @param julian_day Julian day to set.
   */
   void set_julian(guint32 julian_day);

  //TODO: Why return Date& (which is always *this) from these methods?
  //Isn't it enough to also change the current instance?
  //Maybe we need a copy constructor too.
  //murrayc
  
  /** Add a number of days to a Date.
   * @param n_days Days to add.
   * @return Resulting Date
   */
  Date& add_days(int n_days);

  /** Subtract n_days to a Date.
   * @param n_days Days to subtract.
   * @return Resulting Date
   */
  Date& subtract_days(int n_days);

  /** Add n_months to a Date.
   * @param n_months Months to add.
   * @return Resulting Date
   */
  Date& add_months(int n_months);

  /** Subtract n_months to a Date.
   * @param n_months Months to subtract.
   * @return Resulting Date
   */
  Date& subtract_months(int n_months);

  /** Add n_days to a Date.
   * @param n_years Years to add.
   * @return Resulting Date
   */
  Date& add_years(int n_years);

  /** Subtract n_years to a Date.
   * @param n_years Years to subtract.
   * @return Resulting Date
   */
  Date& subtract_years(int n_years);

  /** Calculate days between two dates.
   * @param rhs Date.
   * @return Numbers of days.
   */
  int days_between(const Date& rhs) const;

  /** Compare two dates.
   * @param rhs Date to compare.
   * @return Result of comparition.
   */
  int compare(const Date& rhs) const;

  /** If date is prior to min_date, sets date equal to min_date. 
   * If date falls after max_date, sets date equal to max_date. All dates must be valid.
   * See also clamp_min() and clamp_max(). 
   * @param min_date Date minimum value.
   * @param max_date Date maximum value.
   * @return Date in interval.
   */
  Date& clamp(const Date& min_date, const Date& max_date);

  /** If date is prior to min_date, sets date equal to min_date.
   * See also clamp(), and clamp_max().
   * @param min_date Date minimum value.
   * @return Date in interval.
   */
  Date& clamp_min(const Date& min_date);

  /** If date falls after max_date, sets date equal to max_date.
   * See also clamp(), and clamp_min().
   * @param max_date Date maximum value.
   * @return Date in interval.
   */
  Date& clamp_max(const Date& max_date);
  
  /** Checks if date is less than or equal to other date, and swap the values if this is not the case.
   * @param other Date ro compare.
   * @return Date.
   */
  void order(Date& other);

  /** Returns the day of the week for a Date. The date must be valid.
   * @return Day of the week as a Date::Weekday.
   */
  Weekday get_weekday() const;

  /** Returns the month of the year. The date must be valid.
   * @return Month of the year as a Date::Month. 
   */
  Month        get_month()               const;

  /** Returns the year of a Date. The date must be valid.
   * @return Year in which the date falls.
   */
  Year         get_year()                const;

  /** Returns the day of the month. The date must be valid.
   * @return Day of the month..
   */
  Day          get_day()                 const;

  /** Returns the Julian day or "serial number" of the Date. 
   * The Julian day is simply the number of days since January 1, Year 1; 
   * i.e., January 1, Year 1 is Julian day 1; January 2, Year 1 is Julian day 2, etc. 
   * The date must be valid.
   * @return Julian day.
   */
  guint32      get_julian()              const;

  /** Returns the day of the year, where Jan 1 is the first day of the year.
   * The date must be valid.
   * @return Julian day.
   */
  unsigned int get_day_of_year()         const;

  /** Returns the week of the year, where weeks are understood to start on Monday. 
   * If the date is before the first Monday of the year, return 0. 
   * The date must be valid.
   * @return Week of the year.
   */
  unsigned int get_monday_week_of_year() const;

  /** Returns the week of the year during which this date falls, if weeks are understood to being on Sunday. 
   * Can return 0 if the day is before the first Sunday of the year.
   * The date must be valid.
   * @return Week of the year.
   */
  unsigned int get_sunday_week_of_year() const;

  /** Returns true if the date is on the first of a month. 
   * The date must be valid.
   * @return true if the date is the first of the month. 
   */
  bool         is_first_of_month()       const;

  /** Returns true if the date is the last day of the month.
   * The date must be valid.
   * @return true if the date is the last day of the month.
   */
  bool         is_last_of_month()        const;

  /** Returns the number of days in a month, taking leap years into account.
   * @param month Month.
   * @param year Year.
   * @return Number of days in month during the year.
   */
  static guint8 get_days_in_month(Month month, Year year);

  /** Returns the number of weeks in the year, where weeks are taken to start on Monday. Will be 52 or 53. 
   * (Years always have 52 7-day periods, plus 1 or 2 extra days depending on whether it's a leap year. This function is basically telling you how many Mondays are in the year, i.e. there are 53 Mondays if one of the extra days happens to be a Monday.)
   * @param year Year to count weeks in.
   * @return Number of weeks.
   */
  static guint8 get_monday_weeks_in_year(Year year);

  /** Returns the number of weeks in the year, where weeks are taken to start on Sunday. Will be 52 or 53. 
   * (Years always have 52 7-day periods, plus 1 or 2 extra days depending on whether it's a leap year. This function is basically telling you how many Sundays are in the year, i.e. there are 53 Sundays if one of the extra days happens to be a Sunday.)
   * @param year Year to count weeks in.
   * @return Number of weeks.
   */
  static guint8 get_sunday_weeks_in_year(Year year);

  /** Returns true if the year is a leap year.
   * @param year Year to check.
   * @return true if the year is a leap year.
   */
  static bool   is_leap_year(Year year);

  /** Convert date to string.
   * @param format A format string as used by @c strftime(), in UTF-8
   * encoding.  Only date formats are allowed, the result of time formats
   * is undefined.
   * @return The formatted date string.
   * @throw Glib::ConvertError
   */
  Glib::ustring format_string(const Glib::ustring& format) const;

  /** Fills in the date-related bits of a struct tm using the date value. Initializes the non-date parts with something sane but meaningless.
   * @param dest Struct tm to fill.
   */
  void to_struct_tm(struct tm& dest) const;

  /** Returns true if the Date represents an existing day. 
   * @return true if the date is valid.
   */
  bool valid() const;

  /** Returns true if the day of the month is valid (a day is valid if it's between 1 and 31 inclusive).
   * @param day Day to check.
   * @return true if the day is valid.
   */
  static bool valid_day(Day day);

  /** Returns true if the month value is valid. The 12 Date::Month enumeration values are the only valid months.
   * @param month Month to check.
   * @return true if the month is valid.
   */
  static bool valid_month(Month month);


  /** Returns true if the year is valid. 
   * Any year greater than 0 is valid, though there is a 16-bit limit to what Date will understand.
   * @param year Year to check.
   * @return true if the year is valid.
   */
  static bool valid_year(Year year);

  /** Returns true if the weekday is valid. 
   * The 7 Date::Weekday enumeration values are the only valid.
   * @param weekday Weekday to check.
   * @return true if the weekday is valid.
   */
  static bool valid_weekday(Weekday weekday);

  /** Returns true if the Julian day is valid. 
   * Anything greater than zero is basically a valid Julian, though there is a 32-bit limit.
   * @param julian_day Julian day to check.
   * @return true if the Julian day is valid.
   */
  static bool valid_julian(guint32 julian_day);


  /** Returns true if the day-month-year triplet forms a valid, existing day in the range of days Date understands (Year 1 or later, no more than a few thousand years in the future).
   * @param day Day to check.
   * @param month Month to check.
   * @param year Year to check.
   * @return true if the date is a valid one. 
   */
  static bool valid_dmy(Day day, Month month, Year year);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
  GDate*       gobj()       { return &gobject_; }
  const GDate* gobj() const { return &gobject_; }
#endif

private:
  GDate gobject_;
};


/** @relates Glib::Date */
inline bool operator==(const Date& lhs, const Date& rhs)
  { return (lhs.compare(rhs) == 0); }

/** @relates Glib::Date */
inline bool operator!=(const Date& lhs, const Date& rhs)
  { return (lhs.compare(rhs) != 0); }

/** @relates Glib::Date */
inline bool operator<(const Date& lhs, const Date& rhs)
  { return (lhs.compare(rhs) < 0); }

/** @relates Glib::Date */
inline bool operator>(const Date& lhs, const Date& rhs)
  { return (lhs.compare(rhs) > 0); }

/** @relates Glib::Date */
inline bool operator<=(const Date& lhs, const Date& rhs)
  { return (lhs.compare(rhs) <= 0); }

/** @relates Glib::Date */
inline bool operator>=(const Date& lhs, const Date& rhs)
  { return (lhs.compare(rhs) >= 0); }

} // namespace Glib


#endif /* _GLIBMM_DATE_H */

