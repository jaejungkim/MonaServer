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

#include "Mona/TCPSession.h"
#include "Mona/Protocol.h"

using namespace std;


namespace Mona {

TCPSession::TCPSession(const SocketAddress& peerAddress, Protocol& protocol, Invoker& invoker) : TCPClient(peerAddress,invoker.sockets), Session(protocol, invoker),_consumed(false),_decoding(false) {
	((SocketAddress&)peer.address).set(peerAddress);
}

void TCPSession::onError(const string& error) {
	WARN("Protocol ", protocol().name, ", ", error);
}

UInt32 TCPSession::onReception(const UInt8* data, UInt32 size) {
	if (died)
		return 0;
	PacketReader packet(data, size);
	_decoding = false;
	if (!buildPacket(packet)) {
		if (!_decoding && _consumed) {
			flush(); // flush
			_consumed = false;
		}
		return size;
	}

	UInt32 rest = size - packet.position() - packet.available();
	if (!_decoding) {
		receiveWithoutFlush(packet);
		if (rest == 0) {
			flush(); // flush
			_consumed = false;
		} else
			_consumed = true;
	}
	return rest;
}

void TCPSession::receive(PacketReader& packet) {
	Session::receiveWithoutFlush(packet);
	bool empty = _decodings.empty();
	while (!_decodings.empty() && _decodings.front().unique())
		_decodings.pop_front();
	if (!_decodings.empty())
		_decodings.pop_front();
	if (!empty && _decodings.empty()) {
		flush();
		_consumed = true;
	}
}



} // namespace Mona
