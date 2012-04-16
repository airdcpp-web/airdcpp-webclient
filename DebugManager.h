#if !defined __DEBUGMANAGER_H
#define __DEBUGMANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "DCPlusPlus.h"
#include "Singleton.h"
#include "TimerManager.h"

namespace dcpp {

class DebugManagerListener {
public:
template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> DebugCommand;
	typedef X<0> DebugDetection;	

	virtual void on(DebugCommand, const string&) noexcept { }
	virtual void on(DebugDetection, const string&, uint8_t, uint8_t, const string&) noexcept { }
};

class DebugManager : public Singleton<DebugManager>, public Speaker<DebugManagerListener>
{
	friend class Singleton<DebugManager>;
	DebugManager() { };
public:
	void SendCommandMessage(const string& aMess, uint8_t aType, uint8_t aDirection, const string& aIP) {
		fire(DebugManagerListener::DebugCommand(), aMess, aType, aDirection, aIP);
	}
	void SendDetectionMessage(const string& mess) {
		fire(DebugManagerListener::DebugDetection(), mess);
	}
	~DebugManager() { };
	enum Type {
		TYPE_HUB, TYPE_CLIENT, TYPE_CLIENT_UDP
	};

	enum Direction {
		INCOMING, OUTGOING
	};
};
#define COMMAND_DEBUG(a,b,c,d) DebugManager::getInstance()->SendCommandMessage(a,b,c,d);

} // namespace dcpp

#endif
