/*
Copyright 2014 Mona
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

This file is a part of Mona.
*/

#include "Mona/Time.h"
#include "Mona/String.h"

using namespace std;

namespace Mona {

const string Time::ISO8601_FORMAT("%Y-%m-%dT%H:%M:%S%z");
const string Time::ISO8601_FRAC_FORMAT("%Y-%m-%dT%H:%M:%s%z");
const string Time::RFC822_FORMAT("%w, %e %b %y %H:%M:%S %Z");
const string Time::RFC1123_FORMAT("%w, %e %b %Y %H:%M:%S %Z");
const string Time::HTTP_FORMAT("%w, %d %b %Y %H:%M:%S %Z");
const string Time::RFC850_FORMAT("%W, %e-%b-%y %H:%M:%S %Z");
const string Time::RFC1036_FORMAT("%W, %e %b %y %H:%M:%S %Z");
const string Time::ASCTIME_FORMAT("%w %b %f %H:%M:%S %Y");
const string Time::SORTABLE_FORMAT("%Y-%m-%d %H:%M:%S");

const string Time::WEEKDAY_NAMES[] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday"
};


const string Time::MONTH_NAMES[] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December"
};

Time& Time::update(struct tm& tmtime, int milli /* = 0*/, int micro /* = 0*/) {

	// Convert Local to GMT
	time_t gmttime = timegm(&tmtime);

    Int64 monatime = (gmttime * 1000000ll) + (milli * 1000ll) + (micro);

	_time = chrono::system_clock::time_point(chrono::microseconds(monatime));
	return *this;
}

bool Time::IsValid(struct tm& tmtime, int milli /* = 0 */, int micro /* = 0 */) {

	// Does it can be converted?
	if (timegm(&tmtime) == -1)
		return false;	
	
	if (milli < 0 || milli > 999)
		return false;

	if (micro < 0 || micro > 999)
		return false;
	
	return true;
}

bool Time::isElapsed(Int64 time) const {

	chrono::microseconds dt = chrono::microseconds(time);
	chrono::time_point<chrono::system_clock> tp = chrono::system_clock::now();

	// Get the time elapsed
	chrono::microseconds dtSinceNow = chrono::duration_cast<chrono::microseconds>(tp - _time);

	// Is the time elapsed?
	return dtSinceNow > dt;
}

Int64 Time::elapsed() const {
	chrono::time_point<chrono::system_clock> tp = chrono::system_clock::now();

	// Get the time elapsed
	chrono::microseconds dtSinceNow = chrono::duration_cast<chrono::microseconds>(tp - _time);

	// Is the time elapsed?
	return dtSinceNow.count();
}

string& Time::toString(const string& fmt, string& out, int timezone /*= UTC*/) const {
	out.clear();
	out.reserve(32);
	struct tm datetm;
	return formatDate(toGMT(datetm), fmt, out, timezone);
}

string& Time::toLocaleString(const string& fmt, string& out, int timezone /*= UTC*/) const {
	out.clear();
	out.reserve(32);
	struct tm datetm;
	return formatDate(toLocal(datetm), fmt, out, timezone);
}

string& Time::formatDate(const struct tm& datetm, const string& fmt, string& out, int timezone /*= UTC*/) const {

	auto it = fmt.begin(), &end = fmt.end();

	while (it != end) {

		if (*it == '%') {

			if (++it != end) {
				
				switch (*it) {
				case 'w': out.append(WEEKDAY_NAMES[datetm.tm_wday], 0, 3); break;
				case 'W': out.append(WEEKDAY_NAMES[datetm.tm_wday]); break;
				case 'b': out.append(MONTH_NAMES[datetm.tm_mon], 0, 3); break;
				case 'B': out.append(MONTH_NAMES[datetm.tm_mon]); break;
				case 'd': String::Append(out, Format<int>("%02d", datetm.tm_mday)); break;
				case 'e': String::Append(out, datetm.tm_mday); break;
				case 'f': String::Append(out, Format<int>("%2d", datetm.tm_mday)); break;
				case 'm': String::Append(out, Format<int>("%02d", datetm.tm_mon + 1)); break;
				case 'n': String::Append(out, datetm.tm_mon + 1); break;
				case 'o': String::Append(out, Format<int>("%2d", datetm.tm_mon + 1)); break;
				case 'y': String::Append(out, Format<int>("%02d", datetm.tm_year % 100)); break;
				case 'Y': String::Append(out, Format<int>("%04d", datetm.tm_year + 1900)); break;
				case 'H': String::Append(out, Format<int>("%02d", datetm.tm_hour)); break;
				case 'h': String::Append(out, Format<int>("%02d", hour2AMPM(datetm.tm_hour))); break;
				case 'a': String::Append(out, (datetm.tm_hour < 12) ? "am" : "pm"); break;
				case 'A': String::Append(out, (datetm.tm_hour < 12) ? "AM" : "PM"); break;
				case 'M': String::Append(out, Format<int>("%02d", datetm.tm_min)); break;
				case 'S': String::Append(out, Format<int>("%02d", datetm.tm_sec)); break;
				case 's': String::Append(out, Format<int>("%02d", datetm.tm_sec));
					out += '.';
					String::Append(out, Format<int>("%06d", millisec() * 1000 + microsec()));
					break;
				case 'i': String::Append(out, Format<int>("%03d", millisec())); break;
				case 'c': String::Append(out, millisec() / 100); break;
				case 'F': String::Append(out, Format<int>("%06d", millisec() * 1000 + microsec())); break;
				case 'z': tzFormat(timezone,out); break;
				case 'Z': tzFormat(timezone, out, false); break;
				default:  out += *it;
				}
				++it;
			}
		}
		else out += *it++;
	}
	return out;
}

string& Time::tzFormat(int tzDifferential, string& out, bool bISO /* = true */) const {
	if (tzDifferential == UTC) {
		out += (bISO) ? "Z" : "GMT";
		return out;
	}
	int tzd = (tzDifferential < 0)? -tzDifferential : tzDifferential;
	out += (tzDifferential < 0) ? '-' : '+';
	String::Append(out, Format<int>("%02d", tzd / 3600));
	if (bISO) out += ':';
	return String::Append(out, Format<int>("%02d", (tzd % 3600) / 60));
}

struct tm& Time::toLocal(struct tm& tmtime) const {
	time_t date = chrono::system_clock::to_time_t(_time);
	LOCALTIME(date, tmtime);
	return tmtime;
}

struct tm& Time::toGMT(struct tm& tmtime) const {
	time_t date = chrono::system_clock::to_time_t(_time);
	GMTIME(date, tmtime);
	return tmtime;
}


int Time::hour2AMPM(int hour) const {
	if (hour < 1)
		return 12;
	else if (hour > 12)
		return hour - 12;
	return hour;
}

} // namespace Mona
