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

#include "Mona/Util.h"
#include "Mona/Exceptions.h"
#include "Mona/String.h"
#include "Mona/Time.h"
#include <fstream>


#if !defined(_WIN32)
extern "C" char **environ; // TODO test it on linux!
#endif

using namespace std;

namespace Mona {


MapParameters	Util::_Environment;
mutex			Util::_MutexEnvironment;

map<thread::id, string>	Util::_ThreadNames;
mutex					Util::_MutexThreadNames;

static const char B64Table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char ReverseB64Table[128] = {
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
	64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
	64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
};

const string& Util::GetThreadName(thread::id id) {
	lock_guard<mutex> lock(_MutexThreadNames);
	return _ThreadNames[id];
}

void Util::SetThreadName(thread::id id, const string& name) {
	lock_guard<mutex> lock(_MutexThreadNames);
	_ThreadNames[id] = name;
}

UInt8 Util::Get7BitValueSize(UInt64 value) {
	UInt64 limit = 0x80;
	UInt8 result=1;
	while(value>=limit) {
		limit<<=7;
		++result;
	}
	return result;
}

// environment variables (TODO test on service windows!!)
const MapParameters& Util::Environment() {
	lock_guard<mutex> lock(_MutexEnvironment);
	if (_Environment.count() > 0)
		return _Environment;
	char *s = *environ;
	for (int i = 0; s = *(environ + i); ++i) {
		const char* temp = strchr(s, '=');
		string name;
		string value;
		if (temp) {
			name.assign(s, temp - s);
			value.assign(temp + 1);
		} else
			name.assign(s);
		_Environment.setString(name, value);
	}
	return _Environment;
}


size_t Util::UnpackUrl(const string& url, string& address, string& path, string& query) {
	
	path.clear();
	query.clear();

	auto it = url.begin();
	auto& end = url.end();
	while (it != end) {
		if (*it == '/' || *it == '\\') // no address, just path
			break;
		if (*it == ':') {
			// address
			++it;
			while (it != end && (*it == '/' || *it == '\\'))
				++it;
			if (it == end) // no address, no path, just "scheme://"
				return string::npos;
			auto itEnd(it);
			while (itEnd != end && *itEnd != '/' && *itEnd != '\\')
				++itEnd;
			address.assign(it, itEnd);
			it = itEnd;
			break;
		}
		++it;
	}

	// Normalize path => replace // by / and \ by / AND remove the last '/'
	path.assign(it,end);
	bool isFile(false);
    auto itPath(path.begin());
	auto itField(itPath);
    while (itPath != path.end()) {
        if (*itPath == '?') {
			// query
			query.assign(++itPath,path.end());
			// remove query part from path
			path.resize(path.size()-(path.end()-itPath)-1);
			// remove last slashes
			while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
				path.resize(path.size() - 1);
			break;
		}
        if (*itPath == '/' || *itPath == '\\') {
            ++itPath;
            while (itPath != path.end() && (*itPath == '/' || *itPath == '\\'))
                itPath = path.erase(itPath); // erase multiple slashes
           if (itPath == path.end()) {
				// remove the last /
                path.erase(--itPath);
				break;
			}
			itField = itPath;
			isFile = false;
		} else if (*itPath++ == '.')
			isFile = true;
	}
	return isFile ? (itField-path.begin()) : string::npos;
}


Parameters& Util::UnpackQuery(const string& query, Parameters& properties) {

	auto it = query.begin();
	auto& end = query.end();
	while (it != end) {

		// name
		string name;
		auto itEnd(it);
		while (itEnd != end && *itEnd != '=' && *itEnd != '&') {
			if (*itEnd == '+')
				name += ' ';
			else if (*itEnd == '%')
				name += DecodeURI(itEnd,end);
			else
				name += *itEnd;
			++itEnd;
		};
		it = itEnd;
		string value;
		if (it!=end && *it != '&') { // if it's '='
			++it;
			// value
			auto itEnd(it);
			while (itEnd != end && *itEnd != '&') {
				if (*itEnd == '+')
					value += ' ';
				else if (*itEnd == '%')
					value += DecodeURI(itEnd,end);
				else
					value += *itEnd;
				++itEnd;
			};
			if (itEnd != end) // if it's '&'
				++itEnd;
			it = itEnd;
		}
		properties.setString(name,value);
	}
	return properties;
}

char Util::DecodeURI(const string::const_iterator& it,const string::const_iterator& end) {
	auto itURI(it);
	char hi = *itURI++;
	if (it == end)
		return '%'; // syntax error
	char lo = *it;
	char c;
	if (hi >= '0' && hi <= '9')
		c = hi - '0';
	else if (hi >= 'A' && hi <= 'F')
		c = hi - 'A' + 10;
	else if (hi >= 'a' && hi <= 'f')
		c = hi - 'a' + 10;
	else
		return '%'; // syntax error
	c *= 16;
	if (lo >= '0' && lo <= '9')
		c += lo - '0';
	else if (lo >= 'A' && lo <= 'F')
		c += lo - 'A' + 10;
	else if (lo >= 'a' && lo <= 'f')
		c += lo - 'a' + 10;
	else
		return '%'; // syntax error
	return c;
}


void Util::Dump(const UInt8* in,UInt32 size,Buffer& out,const string& header) {
	UInt32 len = 0;
	UInt32 i = 0;
	UInt32 c = 0;
	UInt8 b;
	out.resize((UInt32)ceil((double)size / 16) * 67 + (header.empty() ? 0 : (header.size() + 2)),false);

	if(!header.empty()) {
		out[len++] = '\t';
		c = header.size();
		memcpy(&out[len],header.c_str(),c);
		len += c;
		out[len++] = '\n';
	}

	while (i<size) {
		c = 0;
		out[len++] = '\t';
		while ( (c < 16) && (i+c < size) ) {
			b = in[i+c];
            snprintf((char*)&out[len],out.size()-len,"%X%X ",b>>4, b & 0x0f );
			len += 3;
			++c;
		}
		while (c++ < 16) {
			memcpy((char*)&out[len],"   \0",4);
			len += 3;
		}
		out[len++] = ' ';
		c = 0;
		while ( (c < 16) && (i+c < size) ) {
			b = in[i+c];
			if (b > 31)
				out[len++] = b;
			else
				out[len++] = '.';
			++c;
		}
		while (c++ < 16)
			out[len++] = ' ';
		i += 16;
		out[len++] = '\n';
	}
}



bool Util::ReadIniFile(Exception& ex,const string& path,Parameters& parameters) {
	ifstream ifile(path, ios::in | ios::binary | ios::ate);
	if (!ifile.good()) {
		ex.set(Exception::FILE, "Impossible to open ", path, " file");
		return false;
	}
	UInt32 size = (UInt32)ifile.tellg();
	if (size == 0)
		return true;
	vector<char> buffer(size);
	ifile.seekg(0);
	ifile.read(buffer.data(), size);
	UInt32 i(0);
	string section;
	while (i<size) {
		char c(0);
		do {
			c = buffer[i++];
		} while (isspace(c) && i < size);
		if (i==size)
			return true;
		if (c == ';') {
			while (c != '\n' && i<size)
				c = buffer[i++];
		} else if (c == '[') {
			section.clear();
			do {
				c = buffer[i++];
			} while (isblank(c) && i < size);
			do {
				section += c;
				if (i == size)
					break;
				c = buffer[i++];
			} while (c != ']' && c != '\n');
		} else {
			string key;
			while (i < size && c != '=' && c != '\n') {
				key += c;
				c = buffer[i++];
			}
			string value;
			if (i < size && c == '=') {
				do {
					c = buffer[i++];
				} while (isblank(c) && i < size);
				do {
					value += c;
					if (i == size)
						break;
					c = buffer[i++];
				} while (c != '\n');
			}
			string fullKey(String::Trim(section, String::TRIM_RIGHT));
			if (!fullKey.empty())
				fullKey += '.';
			fullKey.append(String::Trim(key, String::TRIM_RIGHT));
			parameters.setString(fullKey, String::Trim(value, String::TRIM_RIGHT));
		}
	}
	return true;
}

UInt8* Util::UnformatHex(UInt8* data,UInt32& size) {
	UInt32 sizeResult = (UInt32)ceil(size/ 2.0);
	int j = 0;
	for (int i = 0; i < sizeResult; ++i) {
		const char first = data[j++];
		const char second = j == size ? '0' : data[j++];
		data[i] = ((first - (isdigit(first) ? '0' : '7')) << 4) | ((second - (isdigit(second) ? '0' : '7')) & 0x0F);
	}
	size = sizeResult;
	return data;
}

string& Util::AppendHex(const UInt8* data, UInt32 size, string& result,UInt8 options) {
	static UInt32 Distance('A'-'9'-1); // = 7 in ASCII
	UInt32 i = 0;
	bool skipLeft(false);
	if (options&HEX_TRIM_LEFT) {
		for (i; i < size; ++i) {
			if ((data[i] >> 4)>0)
				break;
			if ((data[i] & 0x0F) > 0) {
				skipLeft = true;
				break;
			}
		}
	}
	UInt8 value;
	for (i; i < size; ++i) {
		if (options&HEX_CPP)
			result.append("\\x");
		value = data[i] >> 4;
		if (!skipLeft)
			result.append(1,value>9 ? ('0' + value + Distance) : '0' + value);
		else
			skipLeft = false;
		value = data[i] & 0x0F;
		result.append(1,value>9 ? ('0' + value + Distance) : '0' + value);
	}
	return result;
}


Buffer& Util::ToBase64(const UInt8* data, UInt32 size, Buffer& result) {
	int i = 0;
	int j = 0;
	UInt32 accumulator = 0;
	UInt32 bits = 0;
	result.resize((UInt32)ceil(size/3.0)*4,false);

	for (i = 0; i < size;++i) {
		accumulator = (accumulator << 8) | (data[i] & 0xFFu);
		bits += 8;
		while (bits >= 6) {
			bits -= 6;
			result[j++] = B64Table[(accumulator >> bits) & 0x3Fu];
		}
	}
	if (bits > 0) { // Any trailing bits that are missing.
		accumulator <<= 6 - bits;
		result[j++] = B64Table[accumulator & 0x3Fu];
	}
	while (result.size() > j) // padding with '='
		result[j++] = '=';
	return result;
}

bool Util::FromBase64(const UInt8* data, UInt32 size, Buffer& result) {
	UInt32 bits = 0;
	UInt32 accumulator = 0;
	result.resize(size/4 * 3,false);
	int j = 0;

	for (int i = 0; i < size; ++i) {
		const int c = data[i];
		if (isspace(c) || c == '=')
			continue;

		if ((c > 127) || (c < 0) || (ReverseB64Table[c] > 63))
			return false;
		
		accumulator = (accumulator << 6) | ReverseB64Table[c];
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			result[j++] = ((accumulator >> bits) & 0xFFu);
		}
	}
	result.resize(j,true);
	return true;
}

} // namespace Mona
