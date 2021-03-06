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

#include "Mona/SocketManager.h"
#include "Mona/Logs.h"
#if !defined(_WIN32)
#include "sys/epoll.h"
#endif

using namespace std;

namespace Mona {


#if defined(_WIN32)
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
#endif


SocketManager::SocketManager(TaskHandler& handler, const PoolBuffers& poolBuffers, PoolThreads& poolThreads, UInt32 bufferSize, const string& name) : poolBuffers(poolBuffers),
    _fakeSocket(*this), _selfHandler(false), poolThreads(poolThreads), _eventFD(0), _sockfd(NET_INVALID_SOCKET), _eventSystem(0), bufferSize(bufferSize), Startable(name), Task(handler), _currentEvent(0), _currentError(0), _eventInit(false), _ppSocket(NULL) {
	_fakeSocket._initialized = true;
}
SocketManager::SocketManager(const PoolBuffers& poolBuffers, PoolThreads& poolThreads, UInt32 bufferSize, const string& name) : poolBuffers(poolBuffers),
    _fakeSocket(*this), _selfHandler(true), poolThreads(poolThreads), _eventFD(0), _sockfd(NET_INVALID_SOCKET), _eventSystem(0), bufferSize(bufferSize), Startable(name), Task((TaskHandler&)*this), _currentEvent(0), _currentError(0), _eventInit(false), _ppSocket(NULL) {
	_fakeSocket._initialized = true;
}

bool SocketManager::start(Exception& ex) {
	TaskHandler::start();
	return Startable::start(ex);
}

void SocketManager::stop() {
	if (!Startable::running())
		return;
	TaskHandler::stop();
	_eventInit.wait();
	clear();
#if defined(_WIN32)
	if (_eventSystem > 0)
		PostMessage(_eventSystem, WM_QUIT, 0, 0);
#else
	if (_eventSystem > 0)
		::close(_eventFD);
#endif
	Startable::stop();
	_eventInit.reset();
}



void SocketManager::clear() {
	lock_guard<mutex> lock(_mutex);
	for(auto& it : _sockets) {
		if(_eventSystem>0) {
#if defined(_WIN32)
			WSAAsyncSelect(it.first,_eventSystem,0,0);
			PostMessage(_eventSystem,0,(WPARAM)it.second,0);
#else
            write(_eventFD,&it.second,sizeof(it.second));
#endif
		}
	}
	_counter = 0;
	_sockets.clear();
}


bool SocketManager::add(Exception& ex,Socket& socket) const {
	if (!Startable::running()) {
		ex.set(Exception::SOCKET, name(), "is not running");
		return false;
	}

	_eventInit.wait();

	if(_eventSystem==0) {
		ex.set(Exception::SOCKET, name(), " hasn't been able to start, impossible to manage sockets");
		return false;
	}

    NET_SOCKET sockfd = socket._sockfd;


	lock_guard<mutex> lock(_mutex);

	auto& it = _sockets.lower_bound(sockfd);
	if (it != _sockets.end() && it->first == sockfd)
		return true; // already managed
	
	
	unique_ptr<Socket>* ppSocket = new unique_ptr<Socket>(&socket);
#if defined(_WIN32)
	int flags = FD_ACCEPT | FD_CLOSE | FD_READ;
	if (WSAAsyncSelect(sockfd, _eventSystem, 104, flags) != 0) {
		ppSocket->release();
		delete ppSocket;
		Net::SetError(ex);
		return false;
	}
#else
	epoll_event event;
	event.events = EPOLLIN | EPOLLRDHUP;
	 event.data.fd = sockfd;
	 event.data.ptr = ppSocket;
	 int res = epoll_ctl(_eventSystem, EPOLL_CTL_ADD,sockfd, &event);
	if(res<0) {
		ppSocket->release();
		delete ppSocket;
        Net::SetError(ex);
		return false;
	}
#endif

	++_counter;
	socket._ppSocket = ppSocket;
	_sockets.emplace_hint(it, sockfd, ppSocket);

	if(bufferSize>0) {
		socket.setReceiveBufferSize(ex, bufferSize);
		socket.setSendBufferSize(ex, bufferSize);
	}

	return true;
}

bool SocketManager::startWrite(Exception& ex, Socket& socket) const {
#if defined(_WIN32)
	if (WSAAsyncSelect(socket._sockfd, _eventSystem, 104, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE) != 0) {
		Net::SetError(ex);
		return false;
	}
#else
	epoll_event event;
	event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
	event.data.fd = socket._sockfd;
	event.data.ptr = socket._ppSocket;
    int res = epoll_ctl(_eventSystem, EPOLL_CTL_MOD, event.data.fd, &event);
	if(res<0) {
        Net::SetError(ex);
		return false;
	}
#endif
	return true;
}

bool SocketManager::stopWrite(Exception& ex, Socket& socket) const {
#if defined(_WIN32)
	if (WSAAsyncSelect(socket._sockfd, _eventSystem, 104, FD_ACCEPT | FD_CLOSE | FD_READ) != 0) {
		Net::SetError(ex);
		return false;
	}
#else
	epoll_event event;
	event.events = EPOLLIN | EPOLLRDHUP;
	event.data.fd = socket._sockfd;
	event.data.ptr = socket._ppSocket;
    int res = epoll_ctl(_eventSystem, EPOLL_CTL_MOD, event.data.fd, &event);
	if(res<0) {
        Net::SetError(ex);
		return false;
	}
#endif
	return true;
}


void SocketManager::remove(Socket& socket) const {
	lock_guard<mutex>	lock(_mutex);
	auto& it = _sockets.find(socket._sockfd);
	if(it == _sockets.end())
		return;

	_eventInit.wait();

	if(_eventSystem>0) {
#if defined(_WIN32)
		WSAAsyncSelect(it->first,_eventSystem,0,0);
		PostMessage(_eventSystem,0,(WPARAM)it->second,0);
#else
		epoll_event event; // Will be ignored by the epoll_ctl call, but is required to work with kernel < 2.6.9
		epoll_ctl(_eventSystem, EPOLL_CTL_DEL, it->first,&event);
		write(_eventFD,&it->second,sizeof(it->second));
#endif
	}

	--_counter;
	socket._ppSocket = NULL;
	_sockets.erase(it);
}

void SocketManager::handle(Exception& ex) {
	if (_ex) {
		ex.set(_ex);
		return;
	}

	Socket* pSocket(NULL);
	if (_ppSocket)
		pSocket = _ppSocket->get();
	if (!pSocket) {
		auto& it = _sockets.find(_sockfd);
		if(it==_sockets.end())
			return;
		pSocket = it->second->get();
	}
	if (!pSocket)
		return;

	if(_currentError!=0) {
		Exception curEx;
		Net::SetError(curEx, _currentError);
		pSocket->onError(curEx.error());
		return;
	}

	/// now, read or accept event!

#if defined(_WIN32)
	if(_currentEvent==FD_READ && pSocket->available(_exSkip)==0) // In the linux case, when _currentEvent==SELECT_READ with 0 bytes it's a ACCEPT event!
		return;
#endif
	Exception socketEx;
	pSocket->onReadable(socketEx);
	if (socketEx)
		pSocket->onError(socketEx.error());
}

void SocketManager::requestHandle() {
	Exception ex;
	giveHandle(ex);
}

void SocketManager::run(Exception& exc) {
	Exception& ex(_selfHandler ? exc : _ex);
	const char* name = Startable::name().c_str();
#if defined(_WIN32)
	WNDCLASSEX wc;
	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = WndProc;
	wc.lpszClassName = name;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	RegisterClassEx(&wc);
	_eventSystem = CreateWindow(name, name, WS_EX_LEFT, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
	if(_eventSystem==0)
		ex.set(Exception::NETWORK, name, " starting failed, impossible to manage sockets");
#else
	int pipefds[2] = {};
    pipe(pipefds);
    int readFD = pipefds[0];
    _eventFD = pipefds[1];
	if(_eventFD>0)
		_eventSystem = epoll_create(1); // Argument is ignored, but has to be greater or equal to 1
	if(_eventFD<=0 || _eventSystem<=0) {
		_eventSystem = _eventFD = 0;
		ex.set(Exception::NETWORK, name, " starting failed, impossible to manage sockets");
	} else {
		// Add the event to terminate the epoll_wait!
		epoll_event event;
		event.events = EPOLLIN;
		event.data.fd = readFD;
		epoll_ctl(_eventSystem, EPOLL_CTL_ADD,readFD, &event);	
	}
#endif

	_eventInit.set();
	if (ex) {
		Task::waitHandle();
		return;
	}



#if defined(_WIN32)
	MSG		  msg;
    while(GetMessage(&msg,_eventSystem, 0, 0)) {
		if(msg.wParam==0)
			continue;
		if(msg.message==0) {
			unique_ptr<Socket>* ppSocket = (unique_ptr<Socket>*)msg.wParam;
			ppSocket->release(); // don't delete the pSocket!
			delete ppSocket;
			continue;
		} else if (msg.message != 104) // unknown message
			continue;
		_currentEvent = WSAGETSELECTEVENT(msg.lParam);
		_fakeSocket._sockfd = _sockfd = msg.wParam;
		if(_currentEvent == FD_WRITE) {

			// protected for _sockets access
			lock_guard<mutex> lock(_mutex);
			auto& it = _sockets.find(_sockfd);
			if (it != _sockets.end()) {
				Socket* pSocket = (*it->second).get();
				Exception curEx;
				pSocket->flushSenders(curEx);
				if (curEx)
					pSocket->onError(curEx.error());
			}

		} else if (_currentEvent != FD_READ || _fakeSocket.available(_exSkip)) {
			if (_currentEvent!=FD_CLOSE)
				_currentError = WSAGETSELECTERROR(msg.lParam);
			Task::waitHandle();
		}

		_sockfd = INVALID_SOCKET;
	}
	DestroyWindow(_eventSystem);

#else

    int count = _counter+1;
	vector<epoll_event> events(count);

	for(;;) {

		int results = epoll_wait(_eventSystem,&events[0],events.size(), -1);

		if(results<0 && errno!=EINTR) {
			Net::SetError(ex);
			Task::waitHandle();
			break;
		}

		// for each ready socket
		int i=0;
		for(i;i<results;++i) {
			epoll_event event = events[i];
			if(event.data.fd==readFD) {
				unique_ptr<Socket>* ppSocket(NULL);
                read(readFD,&ppSocket,sizeof(ppSocket));
				if(!ppSocket) {
					i=-1; // termination signal!
					break;
				}
				ppSocket->release(); // don't delete the pSocket!
				delete ppSocket;
				continue;		
			}
		
			_currentEvent = event.events;
			if(_currentEvent&EPOLLERR)
                _currentError = Net::LastError();
			if(_currentError==0 && _currentEvent&EPOLLOUT) {
				// protected for _ppSocket access access
				lock_guard<mutex> lock(_mutex);
                _ppSocket = (unique_ptr<Socket>*)event.data.ptr;
				if(_ppSocket->get()) {
					Exception curEx;
					(*_ppSocket)->flush(curEx);
					if (curEx)
						(*_ppSocket)->onError(curEx.error());
				}
			} else {
                _ppSocket = (unique_ptr<Socket>*)event.data.ptr;
				Task::waitHandle();
			}
			_currentError = 0;
			_ppSocket = NULL;
		}
		if(i==-1)
			break; // termination signal!
        count = _counter+1;
		if(count!=events.size())
			events.resize(count);
	}

	::close(_eventSystem);
#endif

}

	


} // namespace Mona
