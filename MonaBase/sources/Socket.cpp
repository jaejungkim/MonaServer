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


#include "Mona/Socket.h"
#include "Mona/SocketManager.h"
#include "Mona/SocketSender.h"

using namespace std;

namespace Mona {


Socket::Socket(const SocketManager& manager, int type) : Expirable<Socket>(this), _type(type),_initialized(false), _managed(false), manager(manager), _sockfd(NET_INVALID_SOCKET), _writing(false) {}

Socket::~Socket() {
	close();
	if (_initialized)
		NET_CLOSESOCKET(_sockfd);
	expire(); // prevent deletion for SocketSender
}

PoolThreads& Socket::poolThreads() {
	return manager.poolThreads;
}

const PoolBuffers& Socket::poolBuffers() {
	return manager.poolBuffers;
}

void Socket::close() {
	lock_guard<mutex>	lock(_mutexManaged);
	if (!_managed)
		return;
	_managed = false;
	manager.remove(*this);
	_senders.clear();
}

bool Socket::init(Exception& ex, IPAddress::Family family) {
	lock_guard<mutex>	lock(_mutexInit);
	if (_initialized)
		return true;
	ASSERT_RETURN(_sockfd == NET_INVALID_SOCKET,false)
	if (!Net::InitializeNetwork(ex))
		return false;
	_sockfd = ::socket(family == IPAddress::IPv6 ? AF_INET6 : AF_INET, _type, 0);
	if (_sockfd == NET_INVALID_SOCKET) {
		Net::SetError(ex);
		return false;
	}

	_initialized = true;

	if (!managed(ex)) {
		_initialized = false;
		NET_CLOSESOCKET(_sockfd);
		_sockfd = NET_INVALID_SOCKET;
		return false;
	}

#if defined(__MACH__) && defined(__APPLE__) || defined(__FreeBSD__)
	// SIGPIPE sends a signal that if unhandled (which is the default)
	// will crash the process. This only happens on UNIX, and not Linux.
	//
	// In order to have sockets behave the same across platforms, it is
	// best to just ignore SIGPIPE all together.
	setOption(ex,SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif

	return true;
}

bool Socket::init(Exception& ex, NET_SOCKET sockfd) {
	_sockfd = sockfd;
	_initialized = true;
	bool managed = false;
	if (!this->managed(ex)) {
		NET_CLOSESOCKET(_sockfd);
		_sockfd = NET_INVALID_SOCKET;
		return false;
	}
	return true;
}

bool Socket::managed(Exception& ex) {
	lock_guard<mutex>	lock(_mutexManaged);
	if (_managed)
		return true;
	return _managed = manager.add(ex, *this);
}


bool Socket::connect(Exception& ex, const SocketAddress& address) {
	if (!_initialized && !init(ex, address.family()))
		return false;
	if (!managed(ex))
		return false;
	int rc = ::connect(_sockfd, &address.addr(), sizeof(address.addr()));
	if (rc) {
		int err = Net::LastError();
		if (err != NET_EINPROGRESS && err != NET_EWOULDBLOCK) {
			Net::SetError(ex, err, address.toString());
			return false;
		}	
	}
	return true;
}


bool Socket::bind(Exception& ex, const SocketAddress& address, bool reuseAddress) {
	if (!_initialized && !init(ex, address.family()))
		return false;
	if (!managed(ex))
		return false;

	// TODO? if (address.family() == IPAddress::IPv6)
		// setOption(ex, IPPROTO_IPV6, IPV6_V6ONLY, 1);
	if (reuseAddress) {
		setReuseAddress(ex,true);
		setReusePort(true);
	}
	int rc = ::bind(_sockfd, &address.addr(), sizeof(address.addr()));
	if (rc != 0) {
		Net::SetError(ex, Net::LastError(), address.toString());
		return false;
	}
	return true;
}

	
bool Socket::listen(Exception& ex, int backlog) {
	ASSERT_RETURN(_initialized==true, false)
	if (!managed(ex))
		return false;

	if (::listen(_sockfd, backlog) != 0) {
		Net::SetError(ex);
		return false;
	}
	return true;
}

void Socket::rejectConnection() {
	if (!_initialized)
		return;
	NET_SOCKET sockfd;
	do {
		sockfd = ::accept(_sockfd, NULL, 0);  // TODO acceptEx?
	} while (sockfd == NET_INVALID_SOCKET && Net::LastError() == NET_EINTR);
	if (sockfd != NET_INVALID_SOCKET)
        NET_CLOSESOCKET(sockfd);
}

void Socket::shutdown(Exception& ex,ShutdownType type) {
	ASSERT(_initialized == true)
	if (::shutdown(_sockfd, type) != 0)
		Net::SetError(ex);
}


int Socket::sendBytes(Exception& ex, const void* buffer, int length, int flags) {
	int rc;
	do {
		ASSERT_RETURN(_initialized == true, 0)
		rc = ::send(_sockfd, reinterpret_cast<const char*>(buffer), length, flags);
	} while (rc < 0 && Net::LastError() == NET_EINTR);
	if (rc < 0) {
		int err = Net::LastError();
		if (err == NET_EAGAIN || err == NET_EWOULDBLOCK)
			return 0;
		Net::SetError(ex, err);
	}
	return rc;
}


int Socket::receiveBytes(Exception& ex, void* buffer, int length, int flags) {
	int rc;
	do {
		ASSERT_RETURN(_initialized == true, 0)
		rc = ::recv(_sockfd, reinterpret_cast<char*>(buffer), length, flags);
	} while (rc < 0 && Net::LastError() == NET_EINTR);
	if (rc < 0) {
		int err = Net::LastError();
		if (err == NET_EAGAIN || err == NET_EWOULDBLOCK)
			return 0;
		Net::SetError(ex, err);
	}
	return rc;
}


int Socket::sendTo(Exception& ex, const void* buffer, int length, const SocketAddress& address, int flags) {
	if (!_initialized && !init(ex, address.family()))
		return 0;
	if (!managed(ex))
		return false;

	int rc;
	do {
		ASSERT_RETURN(_initialized == true, 0)
		rc = ::sendto(_sockfd, reinterpret_cast<const char*>(buffer), length, flags, &address.addr(), sizeof(address.addr()));
	}
	while (rc < 0 && Net::LastError() == NET_EINTR);
	if (rc < 0) {
		int err = Net::LastError();
		if (err == NET_EAGAIN || err == NET_EWOULDBLOCK)
			return 0;
		Net::SetError(ex, err);
	}
	return rc;
}


int Socket::receiveFrom(Exception& ex, void* buffer, int length, SocketAddress& address, int flags) {
	if (!_initialized && !init(ex, address.family()))
		return 0;
	if (!managed(ex))
		return false;

	char abuffer[IPAddress::MAX_ADDRESS_LENGTH];
	struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(abuffer);
	NET_SOCKLEN saLen = sizeof(abuffer);
	int rc;
	do {
		ASSERT_RETURN(_initialized == true, 0)
		rc = ::recvfrom(_sockfd, reinterpret_cast<char*>(buffer), length, flags, pSA, &saLen);
	} while (rc < 0 && Net::LastError() == NET_EINTR);
	if (rc < 0) {
		int err = Net::LastError();
		if (err == NET_EAGAIN || err == NET_EWOULDBLOCK)
			return 0;
		Net::SetError(ex, err);
	}
	address.set(*pSA);
	return rc;
}

SocketAddress& Socket::address(Exception& ex, SocketAddress& address) const {
	ASSERT_RETURN(_initialized == true, address)
	char	addressBuffer[IPAddress::MAX_ADDRESS_LENGTH];
	struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(addressBuffer);
	NET_SOCKLEN saLen = sizeof(addressBuffer);
	if (::getsockname(_sockfd, pSA, &saLen) == 0) {
		address.set(*pSA);
		return address;
	}
	Net::SetError(ex);
	return address;
}

	
SocketAddress& Socket::peerAddress(Exception& ex, SocketAddress& address) const {
	ASSERT_RETURN(_initialized == true, address)
	char	addressBuffer[IPAddress::MAX_ADDRESS_LENGTH];
	struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(addressBuffer);
	NET_SOCKLEN saLen = sizeof(addressBuffer);
	if (::getpeername(_sockfd, pSA, &saLen) == 0) {
		address.set(*pSA);
		return address;
	}
	Net::SetError(ex);
	return address;
}

void Socket::setLinger(Exception& ex,bool on, int seconds) {
	struct linger l;
	l.l_onoff  = on ? 1 : 0;
	l.l_linger = seconds;
	setOption<struct linger>(ex,SOL_SOCKET, SO_LINGER, l);
}

	
bool Socket::getLinger(Exception& ex, int& seconds) {
	struct linger l;
	getOption<struct linger>(ex, SOL_SOCKET, SO_LINGER, l);
	seconds = l.l_linger;
	return l.l_onoff != 0;
}


void Socket::setReusePort(bool flag) {
#ifdef SO_REUSEPORT
	Exception ex; // ignore error, since not all implementations support SO_REUSEPORT, even if the macro is defined
	setOption(ex,SOL_SOCKET, SO_REUSEPORT, flag ? 1 : 0);
#endif
}


bool Socket::getReusePort() {
#ifdef SO_REUSEPORT
	Exception ex;
	return getOption(ex,SOL_SOCKET, SO_REUSEPORT) != 0;
#else
	return false;
#endif
}

int Socket::ioctl(Exception& ex,NET_IOCTLREQUEST request,int value) {
	ASSERT_RETURN(_initialized == true, value)
#if defined(_WIN32)
	int rc = ioctlsocket(_sockfd, request, reinterpret_cast<u_long*>(&value));
#else
	int rc = ::ioctl(_sockfd, request, &value);
#endif
	if (rc != 0)
		Net::SetError(ex);
	return value;
}

void Socket::manageWrite(Exception& ex) {
	if (!_writing) {
		_writing = true;
		manager.startWrite(ex, *this);
	}
}

// Can be called from one other thread than main thread (by the manager socket thread)
void Socket::flushSenders(Exception& ex) {
	lock_guard<mutex>	lock(_mutexAsync);
	while (!_senders.empty()) {
		if (!_senders.front()->flush(ex,*this)) {
			if (!_writing)
				_writing = manager.startWrite(ex,*this);
			return;
		}
		_senders.pop_front();
	}
	if (_writing && _senders.empty())
		_writing = !manager.stopWrite(ex, *this);
}


} // namespace Mona
