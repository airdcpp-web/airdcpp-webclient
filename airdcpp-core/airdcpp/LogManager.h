/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_LOG_MANAGER_H
#define DCPLUSPLUS_DCPP_LOG_MANAGER_H

#include <deque>
#include <utility>

#include "typedefs.h"

#include "CID.h"
#include "DispatcherQueue.h"
#include "LogManagerListener.h"
#include "Message.h"
#include "MessageCache.h"
#include "Singleton.h"
#include "Speaker.h"

namespace dcpp {

class LogManager : public Singleton<LogManager>, public Speaker<LogManagerListener>
{
public:
	enum Area: uint8_t { CHAT, PM, DOWNLOAD, UPLOAD, SYSTEM, STATUS, LAST };
	enum: uint8_t { FILE, FORMAT };

	void log(Area area, ParamMap& params) noexcept;
	void message(const string& aMsg, LogMessage::Severity aSeverity, const string& aLabel) noexcept;

	string getPath(Area area, ParamMap& params) const noexcept;
	string getPath(Area area) const noexcept;

	// PM functions
	string getPath(const UserPtr& aUser, ParamMap& params, bool addCache = false) noexcept;
	void log(const UserPtr& aUser, ParamMap& params) noexcept;
	void removePmCache(const UserPtr& aUser) noexcept;

	const string& getSetting(int area, int sel) const noexcept;
	void saveSetting(int area, int sel, const string& setting) const noexcept;

	const MessageCache& getCache() const noexcept {
		return cache;
	}

	void clearCache() noexcept;
	void setRead() noexcept;

	static string readFromEnd(const string& aPath, int aMaxLines, int64_t aBufferSize) noexcept;
private:
	MessageCache cache;

	void log(const string& area, const string& msg) noexcept;

	friend class Singleton<LogManager>;

	int options[LAST][2];

	LogManager();
	virtual ~LogManager();

	unordered_map<CID, string> pmPaths;
	static void ensureParam(const string& aParam, string& aFile) noexcept;

	DispatcherQueue tasks;
};

#define LOG(area, msg) LogManager::getInstance()->log(area, msg)

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_LOG_MANAGER_H)
