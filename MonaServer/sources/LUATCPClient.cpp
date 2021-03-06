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

#include "LUATCPClient.h"
#include "Service.h"

using namespace std;
using namespace Mona;


LUATCPClient::LUATCPClient(const SocketAddress& peerAddress,const SocketManager& manager, lua_State* pState) : _pState(pState), TCPClient(peerAddress, manager) {
}

LUATCPClient::LUATCPClient(const SocketManager& manager,lua_State* pState) : _pState(pState),TCPClient(manager) {
}

LUATCPClient::~LUATCPClient() {
}

void LUATCPClient::onError(const std::string& error) {
	_error = error;
	disconnect();
}


UInt32 LUATCPClient::onReception(const UInt8* data,UInt32 size) {
	UInt32 rest(0);
	SCRIPT_BEGIN(_pState)
		SCRIPT_MEMBER_FUNCTION_BEGIN(LUATCPClient,*this,"onReception")
			SCRIPT_WRITE_BINARY(data,size)
			SCRIPT_FUNCTION_CALL
			if (SCRIPT_CAN_READ)
				rest = SCRIPT_READ_UINT(0);
		SCRIPT_FUNCTION_END
	SCRIPT_END
	return rest;
}

void LUATCPClient::onDisconnection(){
	SCRIPT_BEGIN(_pState)
		SCRIPT_MEMBER_FUNCTION_BEGIN(LUATCPClient,*this,"onDisconnection")
			if (!_error.empty())
				SCRIPT_WRITE_STRING(_error.c_str())
			SCRIPT_FUNCTION_CALL
		SCRIPT_FUNCTION_END
	SCRIPT_END
	_error.clear();
}

int	LUATCPClient::Destroy(lua_State* pState) {
	SCRIPT_DESTRUCTOR_CALLBACK(LUATCPClient,client)
		delete &client;
	SCRIPT_CALLBACK_RETURN
}

int	LUATCPClient::Connect(lua_State* pState) {
	SCRIPT_CALLBACK(LUATCPClient,client)
		string host("127.0.0.1");
		if (SCRIPT_NEXT_TYPE == LUA_TSTRING)
			host = SCRIPT_READ_STRING("127.0.0.1");
		UInt16 port = SCRIPT_READ_UINT(0);
		Exception ex;
		SocketAddress address;
		if (port == 0)
			address.set(ex,host);
		else
			address.set(ex, host, port);
		if (!ex)
			client.connect(ex, address);
		if (ex)
			SCRIPT_WRITE_STRING(ex.error().c_str())
	SCRIPT_CALLBACK_RETURN
}

int	LUATCPClient::Disconnect(lua_State* pState) {
	SCRIPT_CALLBACK(LUATCPClient,client)
		client.disconnect();
	SCRIPT_CALLBACK_RETURN
}


int	LUATCPClient::Send(lua_State* pState) {
	SCRIPT_CALLBACK(LUATCPClient,client)
		SCRIPT_READ_BINARY(data,size)
		Exception ex;
		client.send(ex,data, size);
		if (ex)
			SCRIPT_WRITE_STRING(ex.error().c_str())
	SCRIPT_CALLBACK_RETURN
}

int LUATCPClient::Get(lua_State* pState) {
	SCRIPT_CALLBACK(LUATCPClient,client)
		const char* name = SCRIPT_READ_STRING("");
		if(strcmp(name,"connect")==0) {
			SCRIPT_WRITE_FUNCTION(&LUATCPClient::Connect)
		} else if (strcmp(name, "disconnect") == 0) {
			SCRIPT_WRITE_FUNCTION(&LUATCPClient::Disconnect)
		} else if (strcmp(name, "send") == 0) {
			SCRIPT_WRITE_FUNCTION(&LUATCPClient::Send)
		} else if (strcmp(name, "address") == 0) {
			if(client.connected())
				SCRIPT_WRITE_STRING(client.address().toString().c_str())
			else
				SCRIPT_WRITE_NIL
		} else if (strcmp(name, "peerAddress") == 0) {
			if(client.connected())
				SCRIPT_WRITE_STRING(client.peerAddress().toString().c_str())
			else
				SCRIPT_WRITE_NIL
		} else if (strcmp(name, "connected") == 0)
			SCRIPT_WRITE_BOOL(client.connected())
	SCRIPT_CALLBACK_RETURN
}

int LUATCPClient::Set(lua_State* pState) {
	SCRIPT_CALLBACK(LUATCPClient,client)
		lua_rawset(pState,1); // consumes key and value
	SCRIPT_CALLBACK_RETURN
}
