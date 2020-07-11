/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef DCPLUSPLUS_DCPP_USER_CONNECTION_H
#define DCPLUSPLUS_DCPP_USER_CONNECTION_H

#include "AdcCommand.h"
#include "forward.h"
#include "BufferedSocketListener.h"
#include "BufferedSocket.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "User.h"
#include "UserConnectionListener.h"

namespace dcpp {

class UserConnection : public Speaker<UserConnectionListener>, 
	private BufferedSocketListener, public Flags, private CommandHandler<UserConnection>,
	private boost::noncopyable
{
public:
	friend class ConnectionManager;
	
	static const string FEATURE_MINISLOTS;
	static const string FEATURE_XML_BZLIST;
	static const string FEATURE_ADCGET;
	static const string FEATURE_ZLIB_GET;
	static const string FEATURE_TTHL;
	static const string FEATURE_TTHF;
	static const string FEATURE_ADC_BAS0;
	static const string FEATURE_ADC_BASE;
	static const string FEATURE_ADC_BZIP;
	static const string FEATURE_ADC_TIGR;
	static const string FEATURE_ADC_MCN1;
	static const string FEATURE_ADC_UBN1;
	static const string FEATURE_ADC_CPMI;

	static const string FILE_NOT_AVAILABLE;
	static const string FEATURE_AIRDC;

	enum Modes {
		MODE_COMMAND = BufferedSocket::MODE_LINE,
		MODE_DATA = BufferedSocket::MODE_DATA
	};

	enum Flags {
		FLAG_NMDC					= 0x01,
		FLAG_OP						= FLAG_NMDC << 1,
		FLAG_UPLOAD					= FLAG_OP << 1,
		FLAG_DOWNLOAD				= FLAG_UPLOAD << 1,
		FLAG_PM						= FLAG_DOWNLOAD << 1,
		FLAG_INCOMING				= FLAG_PM << 1,
		FLAG_ASSOCIATED				= FLAG_INCOMING << 1,
		FLAG_SUPPORTS_MINISLOTS		= FLAG_ASSOCIATED << 1,
		FLAG_SUPPORTS_XML_BZLIST	= FLAG_SUPPORTS_MINISLOTS << 1,
		FLAG_SUPPORTS_ADCGET		= FLAG_SUPPORTS_XML_BZLIST << 1,
		FLAG_SUPPORTS_ZLIB_GET		= FLAG_SUPPORTS_ADCGET << 1,
		FLAG_SUPPORTS_TTHL			= FLAG_SUPPORTS_ZLIB_GET <<1,
		FLAG_SUPPORTS_TTHF			= FLAG_SUPPORTS_TTHL << 1,
		FLAG_RUNNING				= FLAG_SUPPORTS_TTHF << 1,
		FLAG_MCN1					= FLAG_RUNNING << 1,
		FLAG_SMALL_SLOT				= FLAG_MCN1 << 1,
		FLAG_UBN1					= FLAG_SMALL_SLOT << 1,
		FLAG_CPMI					= FLAG_UBN1 << 1,
		FLAG_TRUSTED				= FLAG_CPMI << 1

	};
	
	enum States {
		// ConnectionManager
		STATE_UNCONNECTED,
		STATE_CONNECT,

		// Handshake
		STATE_SUPNICK,		// ADC: SUP, Nmdc: $Nick
		STATE_INF,
		STATE_LOCK,
		STATE_DIRECTION,
		STATE_KEY,

		// UploadManager
		STATE_GET,			// Waiting for GET
		STATE_SEND,			// Waiting for $Send

		// DownloadManager
		STATE_SND,	// Waiting for SND
		STATE_IDLE, // No more downloads for the moment

		// Up & down
		STATE_RUNNING,		// Transmitting data

	};
	
	enum SlotTypes: uint8_t {	
		NOSLOT		= 0,
		STDSLOT		= 1,
		EXTRASLOT	= 2,
		PARTIALSLOT	= 3,
		MCNSLOT		= 4,
		SMALLSLOT	= 5
	};

	short getNumber() const { return (short)((((size_t)this)>>2) & 0x7fff); }

	// NMDC stuff
	void myNick(const string& aNick) { send("$MyNick " + Text::fromUtf8(aNick, encoding) + '|'); }
	void lock(const string& aLock, const string& aPk) { send ("$Lock " + aLock + " Pk=" + aPk + '|'); }
	void key(const string& aKey) { send("$Key " + aKey + '|'); }
	void direction(const string& aDirection, int aNumber) { send("$Direction " + aDirection + " " + Util::toString(aNumber) + '|'); }
	void fileLength(const string& aLength) { send("$FileLength " + aLength + '|'); }
	void error(const string& aError) { send("$Error " + aError + '|'); }
	void listLen(const string& aLength) { send("$ListLen " + aLength + '|'); }
	
	void maxedOut(size_t qPos = 0);
	
	
	void sendError(const std::string& msg = FILE_NOT_AVAILABLE, AdcCommand::Error aError=AdcCommand::ERROR_FILE_NOT_AVAILABLE);
	void supports(const StringList& feat);
	void getListLen() { send("$GetListLen|"); }

	// ADC Stuff
	void sup(const StringList& features);
	void inf(bool withToken, int mcnSlots = 0);
	void get(const string& aType, const string& aName, const int64_t aStart, const int64_t aBytes) {  send(AdcCommand(AdcCommand::CMD_GET).addParam(aType).addParam(aName).addParam(Util::toString(aStart)).addParam(Util::toString(aBytes))); }
	void snd(const string& aType, const string& aName, const int64_t aStart, const int64_t aBytes) {  send(AdcCommand(AdcCommand::CMD_SND).addParam(aType).addParam(aName).addParam(Util::toString(aStart)).addParam(Util::toString(aBytes))); }
	void send(const AdcCommand& c) { send(c.toString(0, isSet(FLAG_NMDC))); }

	void setDataMode(int64_t aBytes = -1) noexcept { dcassert(socket); socket->setDataMode(aBytes); }
	void setLineMode(size_t rollback) noexcept { dcassert(socket); socket->setLineMode(rollback); }

	void connect(const Socket::AddressInfo& aServer, const string& aPort, const string& localPort, BufferedSocket::NatRoles natRole, const UserPtr& aUser = nullptr);
	void accept(const Socket& aServer);

	void handlePM(const AdcCommand& c, bool echo) noexcept;
	bool sendPrivateMessageHooked(const OutgoingChatMessage& aMessage, string& error_);

	template<typename F>
	void callAsync(F f) { if(socket) socket->callAsync(f); }

	void disconnect(bool graceless = false) { if(socket) socket->disconnect(graceless); }
	void transmitFile(InputStream* f) { socket->transmitFile(f); }

	const string& getDirectionString() const noexcept {
		dcassert(isSet(FLAG_UPLOAD) ^ isSet(FLAG_DOWNLOAD));
		return isSet(FLAG_UPLOAD) ? UPLOAD : DOWNLOAD;
	}

	const UserPtr& getUser() const { return user; }
	UserPtr& getUser() { return user; }
	HintedUser getHintedUser() const { return HintedUser(user, hubUrl); }

	bool isSecure() const noexcept { return socket && socket->isSecure(); }
	bool isTrusted() const noexcept { return socket && socket->isTrusted(); }
	std::string getEncryptionInfo() const noexcept { return socket ? socket->getEncryptionInfo() : Util::emptyString; }
	ByteVector getKeyprint() const noexcept { return socket ? socket->getKeyprint() : ByteVector(); }
	bool verifyKeyprint(const string& expKeyp, bool allowUntrusted) noexcept { return socket ? socket->verifyKeyprint(expKeyp, allowUntrusted) : true; }

	const string& getRemoteIp() const noexcept { if(socket) return socket->getIp(); else return Util::emptyString; }
	Download* getDownload() noexcept { dcassert(isSet(FLAG_DOWNLOAD)); return download; }
	void setDownload(Download* d) noexcept { dcassert(isSet(FLAG_DOWNLOAD)); download = d; }
	Upload* getUpload() noexcept { dcassert(isSet(FLAG_UPLOAD)); return upload; }
	void setUpload(Upload* u) noexcept { dcassert(isSet(FLAG_UPLOAD)); upload = u; }
	
	void handle(AdcCommand::SUP t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::INF t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::GET t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::SND t, const AdcCommand& c) { fire(t, this, c);	}
	void handle(AdcCommand::STA t, const AdcCommand& c);
	void handle(AdcCommand::RES t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::GFI t, const AdcCommand& c) { fire(t, this, c);	}
	void handle(AdcCommand::MSG t, const AdcCommand& c);
	void handle(AdcCommand::PMI t, const AdcCommand& c);

	// Ignore any other ADC commands for now
	template<typename T> void handle(T , const AdcCommand& ) { }

	int64_t getChunkSize() const noexcept;

	void updateChunkSize(int64_t leafSize, int64_t lastChunk, uint64_t ticks) noexcept;
	bool supportsTrees() const noexcept { return isSet(FLAG_SUPPORTS_TTHL); }
	
	GETSET(string, hubUrl, HubUrl);
	GETSET(string, token, Token);
	GETSET(string, lastBundle, LastBundle);
	IGETSET(int64_t, speed, Speed, 0);
	IGETSET(uint64_t, lastActivity, LastActivity, 0);
	GETSET(string, encoding, Encoding);
	IGETSET(States, state, State, STATE_UNCONNECTED);
	IGETSET(uint8_t, slotType, SlotType, NOSLOT);
	
	const BufferedSocket* getSocket() const noexcept { return socket; }

	void setThreadPriority(Thread::Priority aPriority) ;
private:
	int64_t chunkSize = 0;
	BufferedSocket* socket = nullptr;
	const bool secure;
	UserPtr user;

	static const string UPLOAD, DOWNLOAD;
	
	union {
		Download* download;
		Upload* upload;
	};

	// We only want ConnectionManager to create this...
	UserConnection(bool secure_) noexcept;

	virtual ~UserConnection() {
		BufferedSocket::putSocket(socket);
	}

	friend struct DeleteFunction;

	void setUser(const UserPtr& aUser) noexcept;
	
	void send(const string& aString);

	void on(Connected) noexcept;
	void on(Line, const string&) noexcept;
	void on(Data, uint8_t* data, size_t len) noexcept;
	void on(BytesSent, size_t bytes, size_t actual) noexcept ;
	void on(ModeChange) noexcept;
	void on(TransmitDone) noexcept;
	void on(Failed, const string&) noexcept;

};

inline bool operator==(UserConnection* ptr, const string& aToken) { return compare(ptr->getToken(), aToken) == 0; }

} // namespace dcpp

#endif // !defined(USER_CONNECTION_H)