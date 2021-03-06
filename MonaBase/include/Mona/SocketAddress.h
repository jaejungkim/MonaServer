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

#pragma once

#include "Mona.h"
#include "Mona/IPAddress.h"


namespace Mona {


class SocketAddressCommon;

/// This class represents an internet (IP) endpoint/socket
/// address. The address can belong either to the
/// IPv4 or the IPv6 address family and consists of a
/// host address and a port number.
class SocketAddress : public virtual NullableObject {
public:
	/// Creates a wildcard (all zero) IPv4 SocketAddress
	SocketAddress(IPAddress::Family family = IPAddress::IPv4);
		
	/// Create/Set SocketAddress from a native socket address
	SocketAddress(const struct sockaddr& addr);
	void set(const struct sockaddr& addr);
	
	SocketAddress(const SocketAddress& other);
	void set(const SocketAddress& other);

	/// Creates a SocketAddress from an IP address and a port number.
	SocketAddress(const IPAddress& host, UInt16 port);
	void set(const IPAddress& host, UInt16 port);
	
	/// set SocketAddress from an IP address and a port number.
	bool set(Exception& ex, const std::string& host, UInt16 port) { return setIntern(ex, host, port,false); }
	bool setWithDNS(Exception& ex, const std::string& host, UInt16 port) { return setIntern(ex, host, port,true); }

	/// set SocketAddress from an IP address and a service name or port number
	bool set(Exception& ex, const std::string& host, const std::string& port) { return setIntern(ex, host, resolveService(ex,port),false); }
	bool setWithDNS(Exception& ex, const std::string& host, const std::string& port) { return setIntern(ex, host, resolveService(ex,port),true); }

	/// set SocketAddress from an IP address or host name and a port number/service name
	bool set(Exception& ex, const std::string& hostAndPort) { return setIntern(ex, hostAndPort, false); }
	bool setWithDNS(Exception& ex, const std::string& hostAndPort) { return setIntern(ex, hostAndPort, true); }
	
	void reset();

	const IPAddress&		host() const;
	UInt16					port() const;
	IPAddress::Family		family() const;

	// Returns a string representation of the address
	const std::string&		toString() const;
	
	// Native socket address
	const struct sockaddr&	addr() const;
	
	bool operator < (const SocketAddress& address) const;
	bool operator == (const SocketAddress& address) const { return port() == address.port() && host() == address.host(); }
	bool operator != (const SocketAddress& address) const { return port() != address.port() || host() != address.host(); }
	
	// Returns a wildcard IPv4 or IPv6 address (0.0.0.0)
	static const SocketAddress& Wildcard(IPAddress::Family family = IPAddress::IPv4);


	static UInt16	Split(const std::string& address,std::string& host);
private:
	bool setIntern(Exception& ex,const std::string& host, UInt16 port,bool resolveHost);
	bool setIntern(Exception& ex, const std::string& hostAndPort, bool resolveHost);

	UInt16 resolveService(Exception& ex, const std::string& service);

	std::shared_ptr<SocketAddressCommon>	_pAddress;

	mutable std::string		_toString;
};



} // namespace Mona
