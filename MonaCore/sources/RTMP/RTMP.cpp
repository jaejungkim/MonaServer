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

#include "Mona/RTMP/RTMP.h"
#include "Mona/Util.h"
#include "Mona/Logs.h"



namespace Mona {


const UInt8 FPKey[] = {
	0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20,
	0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
	0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79,
	0x65, 0x72, 0x20, 0x30, 0x30, 0x31
};

const UInt8 FMSKey[] = {
	0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20,
	0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c,
	0x61, 0x73, 0x68, 0x20, 0x4d, 0x65, 0x64, 0x69,
	0x61, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72,
	0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Media Server 001
	0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8,
	0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57,
	0x6e, 0xec, 0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab,
	0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
}; // 68

UInt8	 AlignData[1536];

UInt32 RTMP::GetDigestPos(const UInt8* data,bool middle) {
	UInt16 pos = 13;
	if(middle)
		pos += 764;
	return ((data[pos-4] + data[pos-3] + data[pos-2] + data[pos-1])%728)+pos;
}

UInt32 RTMP::GetDHPos(const UInt8* data,bool middle) {
	UInt16 pos = 772;
	if(!middle)
		pos += 764;
	pos = ((data[pos-3] + data[pos-2] + data[pos-1] + data[pos])%632)+9;
	if(!middle)
		pos += 764;
	return pos;
}

const UInt8* RTMP::ValidateClient(Crypto& crypto,BinaryReader& reader,bool& middleKey) {
	middleKey=false;
	UInt32 position = reader.position();
	if (reader.read32() == 0) {
		WARN("This version of player doesn't support validation");
        // TODO fix the return value warning
		return false;
	}
	
	const UInt8* keyChallenge = ValidateClientScheme(crypto,reader, false);
	if (!keyChallenge) {
		keyChallenge = ValidateClientScheme(crypto,reader, true);
		middleKey = true;
	}
	reader.reset(position);
	return keyChallenge;
}

const UInt8* RTMP::ValidateClientScheme(Crypto& crypto,BinaryReader& reader,bool middleKey) {
	reader.reset();
	UInt16 pos = GetDigestPos(reader.current(),middleKey);

	UInt8 content[1504];
	reader.reset(1);
	memcpy(content, reader.current(), pos-1);
	reader.reset(pos);
	memcpy(content+pos-1, reader.current()+HMAC_KEY_SIZE,reader.available()-HMAC_KEY_SIZE);

	UInt8 hash[HMAC_KEY_SIZE];
	crypto.hmac(EVP_sha256(),FPKey,sizeof(FPKey),content,sizeof(content),hash);

	if(memcmp(hash,reader.current(),HMAC_KEY_SIZE)==0)
		return reader.current();
	return NULL;
}


void RTMP::WriteDigestAndKey(Crypto& crypto,UInt8* data,const UInt8* challengeKey,bool middleKey) {
	UInt32 serverDigestOffset = RTMP::GetDigestPos(data, middleKey);

	UInt8 content[1504];
	memcpy(content, data+1, serverDigestOffset-1);
	memcpy(content + serverDigestOffset-1, data + serverDigestOffset + HMAC_KEY_SIZE,1505 - serverDigestOffset);

	UInt8 hash[HMAC_KEY_SIZE];
	crypto.hmac(EVP_sha256(),FMSKey,36,content,sizeof(content),hash);

	//put the digest in place
	memcpy(data+serverDigestOffset,hash,sizeof(hash));

		//compute the key
	crypto.hmac(EVP_sha256(),FMSKey,sizeof(FMSKey),challengeKey,HMAC_KEY_SIZE,hash);

	//generate the hash
	crypto.hmac(EVP_sha256(),hash,HMAC_KEY_SIZE,data + 1537,1504,data+3041);
}


void RTMP::ComputeRC4Keys(Crypto& crypto,const UInt8* pubKey,UInt32 pubKeySize,const UInt8* farPubKey,UInt32 farPubKeySize,const Buffer& sharedSecret,RC4_KEY& decryptKey,RC4_KEY& encryptKey) {

	UInt8 hash[HMAC_KEY_SIZE];
	RC4_set_key(&decryptKey, 16, crypto.hmac(EVP_sha256(),sharedSecret.data(),sharedSecret.size(),pubKey,pubKeySize,hash));
	RC4_set_key(&encryptKey, 16, crypto.hmac(EVP_sha256(),sharedSecret.data(),sharedSecret.size(),farPubKey,farPubKeySize,hash));

	//bring the keys to correct cursor
	RC4(&encryptKey, 1536, AlignData, AlignData);
}




} // namespace Mona
