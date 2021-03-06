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

#include "Mona/Mona.h"
#include "Mona/SocketAddress.h"
#include "Mona/PoolThreads.h"
#include "Mona/PoolBuffers.h"
#include "Mona/Expirable.h"
#include <memory>
#include <deque>

namespace Mona {

class SocketManager;
class SocketSender;

class Socket : virtual Object,Expirable<Socket> {
	friend class SocketManager;
	friend class DatagramSocket;
	friend class StreamSocket;
	friend class ServerSocket;
public:

	// Destroys the Socket (Closes the socket if it is still open)
	virtual ~Socket();

	enum ShutdownType {
		RECV = 0,
		SEND = 1,
		BOTH = 2
	};

	int		available(Exception& ex) { return ioctl(ex, FIONREAD, 0); }
	
	SocketAddress& address(Exception& ex, SocketAddress& address) const;
	SocketAddress& peerAddress(Exception& ex, SocketAddress& address) const;


	void setSendBufferSize(Exception& ex,int size) { setOption(ex, SOL_SOCKET, SO_SNDBUF, size); }
	int  getSendBufferSize(Exception& ex) { return getOption(ex,SOL_SOCKET, SO_SNDBUF); }
	
	void setReceiveBufferSize(Exception& ex, int size) { setOption(ex, SOL_SOCKET, SO_RCVBUF, size); }
	int  getReceiveBufferSize(Exception& ex) { getOption(ex,SOL_SOCKET, SO_RCVBUF); }

	void setNoDelay(Exception& ex, bool flag) { setOption(ex,IPPROTO_TCP, TCP_NODELAY, flag ? 1 : 0); }
	bool getNoDelay(Exception& ex) { return getOption(ex, IPPROTO_TCP, TCP_NODELAY) != 0; }

	void setKeepAlive(Exception& ex, bool flag) { setOption(ex, SOL_SOCKET, SO_KEEPALIVE, flag ? 1 : 0); }
	bool getKeepAlive(Exception& ex) { return getOption(ex,SOL_SOCKET, SO_KEEPALIVE) != 0; }

	void setReuseAddress(Exception& ex, bool flag) { setOption(ex, SOL_SOCKET, SO_REUSEADDR, flag ? 1 : 0); }
	bool getReuseAddress(Exception& ex) { return getOption(ex, SOL_SOCKET, SO_REUSEADDR) != 0; }

	void setOOBInline(Exception& ex, bool flag) { setOption(ex, SOL_SOCKET, SO_OOBINLINE, flag ? 1 : 0); }
	bool getOOBInline(Exception& ex) { return getOption(ex, SOL_SOCKET, SO_OOBINLINE) != 0; }

	void setLinger(Exception& ex, bool on, int seconds);
	bool getLinger(Exception& ex, int& seconds);

	void setReusePort(bool flag);
	bool getReusePort();

	template<typename SocketSenderType>
	// Can be called from one other thread than main thread (by the poolthread)
	bool send(Exception& ex,const std::shared_ptr<SocketSenderType>& pSender) {
		// return if no data to send
		if (!pSender->available())
			return true;

		ASSERT_RETURN(_initialized == true,false)
		if (!managed(ex))
			return false;

		// We can write immediatly if there are no queue packets to write,
		// and if it remains some data to write (flush returns false)
		std::lock_guard<std::mutex>	lock(_mutexAsync);
		if (!_senders.empty() || !pSender->flush(ex, *this)) {
            _senders.emplace_back(pSender);
			manageWrite(ex);
			return !ex;
		}
		return true;
	}

	template<typename SocketSenderType>
	PoolThread* send(Exception& ex,const std::shared_ptr<SocketSenderType>& pSender, PoolThread* pThread) {
		ASSERT_RETURN(_initialized == true, pThread)
		if (!managed(ex))
			return pThread;
		shareThis(pSender->_expirableSocket);
		pSender->_pThis = pSender;
		pThread = poolThreads().enqueue<SocketSenderType>(ex,pSender, pThread);
		return pThread;
	}

	const PoolBuffers&	poolBuffers();
	PoolThreads&		poolThreads();

protected:
	// Can be called by a separated thread!
	virtual void			onError(const std::string& error) = 0;

	void					close();
	const SocketManager&	manager;
	
private:
	// Creates a Socket
	Socket(const SocketManager& manager, int type = SOCK_STREAM);

	template<typename SocketType, typename ...Args>
	SocketType* acceptConnection(Exception& ex, Args&&... args) {
		ASSERT_RETURN(_initialized == true, NULL)

		char buffer[IPAddress::MAX_ADDRESS_LENGTH];
		struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(buffer);
		NET_SOCKLEN saLen = sizeof(buffer);

		NET_SOCKET sockfd;
		do {
			sockfd = ::accept(_sockfd, pSA, &saLen);  // TODO acceptEx?
		} while (sockfd == NET_INVALID_SOCKET && Net::LastError() == EINTR);
		if (sockfd == NET_INVALID_SOCKET) {
			Net::SetError(ex);
			return NULL;
		}
		SocketAddress address(*pSA);
		if (!onConnection(address))
			return NULL;
		SocketType* pSocket = new SocketType(address,args ...);
		Socket* pSocketBase = (Socket*)pSocket;
		std::lock_guard<std::mutex>	lock(pSocketBase->_mutexInit);
		if (!pSocketBase->init(ex, sockfd)) {
			delete pSocket;
			pSocket = NULL;
		}
		return pSocket;
	}
	void rejectConnection();

	bool connect(Exception& ex, const SocketAddress& address);
	bool bind(Exception& ex, const SocketAddress& address, bool reuseAddress = true);
	bool listen(Exception& ex, int backlog = 64);

	int receiveBytes(Exception& ex, void* buffer, int length, int flags = 0);
	int receiveFrom(Exception& ex, void* buffer, int length, SocketAddress& address, int flags = 0);


	void shutdown(Exception& ex, ShutdownType type = BOTH);

	int sendBytes(Exception& ex, const void* buffer, int length, int flags = 0);
	int sendTo(Exception& ex, const void* buffer, int length, const SocketAddress& address, int flags = 0);

	void setBroadcast(Exception& ex, bool flag) { setOption(ex, SOL_SOCKET, SO_BROADCAST, flag ? 1 : 0); }
	bool getBroadcast(Exception& ex) { return getOption(ex, SOL_SOCKET, SO_BROADCAST) != 0; }

	

	virtual bool    onConnection(const SocketAddress& address) { return true; }
	// if ex of onReadable is raised, it's given to onError
	virtual void	onReadable(Exception& ex) = 0;
	
	

	// Creates the underlying native socket
	bool	init(Exception& ex, IPAddress::Family family);
	bool	init(Exception& ex, NET_SOCKET sockfd);

	bool    managed(Exception& ex);

	// flush async sending
	void    manageWrite(Exception& ex);
	void	flushSenders(Exception& ex);

	template<typename Type>
	Type& getOption(Exception& ex, int level, int option, Type& value) {
		ASSERT_RETURN(_initialized == true, value)
            NET_SOCKLEN length(sizeof(value));
		if (::getsockopt(_sockfd, level, option, reinterpret_cast<char*>(&value), &length) == -1)
			Net::SetError(ex);
		return value;
	}
	int		getOption(Exception& ex, int level, int option) { int value; return getOption<int>(ex, level, option, value); }

	template<typename Type>
	void setOption(Exception& ex, int level, int option, const Type& value) {
		ASSERT(_initialized == true)
            NET_SOCKLEN length(sizeof(value));
		if (::setsockopt(_sockfd, level, option, reinterpret_cast<const char*>(&value), length) == -1)
			Net::SetError(ex);
	}
	void	setOption(Exception& ex, int level, int option, int value) { setOption<int>(ex, level, option, value); }

	// A wrapper for the ioctl system call
	int		ioctl(Exception& ex,NET_IOCTLREQUEST request,int value);

	std::unique_ptr<Socket>*					_ppSocket; // deleted by the socketmanager, and exists when managed!

	std::mutex									_mutexAsync;
	bool										_writing;
	std::deque<std::shared_ptr<SocketSender>>	_senders;

	std::mutex				_mutexManaged;
	volatile bool			_managed;

	NET_SOCKET				_sockfd;
	std::mutex				_mutexInit;
	volatile bool			_initialized;
	int						_type;
};



} // namespace Mona
