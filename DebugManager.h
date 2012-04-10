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
	virtual void on(DebugDetection, const string&, int, const string&, bool) noexcept { }
};

class DebugManager : public Singleton<DebugManager>, public Speaker<DebugManagerListener>
{
	friend class Singleton<DebugManager>;
	DebugManager() { };
public:
	void SendCommandMessage(const string& mess, int typeDir, const string& ip, bool isUDP=false) {
		fire(DebugManagerListener::DebugCommand(), mess, typeDir, ip, isUDP);
	}
	void SendDetectionMessage(const string& mess) {
		fire(DebugManagerListener::DebugDetection(), mess);
	}
	~DebugManager() { };
	enum {
		HUB_IN, HUB_OUT, CLIENT_IN, CLIENT_OUT
	};
};
#define COMMAND_DEBUG(a,b,c) DebugManager::getInstance()->SendCommandMessage(a,b,c);
#define COMMAND_DEBUGUDP(a,b,c) DebugManager::getInstance()->SendCommandMessage(a,b,c, true);
#define DETECTION_DEBUG(m) DebugManager::getInstance()->SendDetectionMessage(m);

} // namespace dcpp

#endif
