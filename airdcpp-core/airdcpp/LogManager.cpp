/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "LogManager.h"

#include "File.h"
#include "StringTokenizer.h"
#include "TimerManager.h"
#include "User.h"

namespace dcpp {

LogManager::LogManager() : tasks(true, Thread::IDLE), cache(SettingsManager::LOG_MESSAGE_CACHE) {

	options[UPLOAD][FILE] = SettingsManager::LOG_FILE_UPLOAD;
	options[UPLOAD][FORMAT] = SettingsManager::LOG_FORMAT_POST_UPLOAD;
	options[DOWNLOAD][FILE] = SettingsManager::LOG_FILE_DOWNLOAD;
	options[DOWNLOAD][FORMAT] = SettingsManager::LOG_FORMAT_POST_DOWNLOAD;
	options[CHAT][FILE] = SettingsManager::LOG_FILE_MAIN_CHAT;
	options[CHAT][FORMAT] = SettingsManager::LOG_FORMAT_MAIN_CHAT;
	options[PM][FILE] = SettingsManager::LOG_FILE_PRIVATE_CHAT;
	options[PM][FORMAT] = SettingsManager::LOG_FORMAT_PRIVATE_CHAT;
	options[SYSTEM][FILE] = SettingsManager::LOG_FILE_SYSTEM;
	options[SYSTEM][FORMAT] = SettingsManager::LOG_FORMAT_SYSTEM;
	options[STATUS][FILE] = SettingsManager::LOG_FILE_STATUS;
	options[STATUS][FORMAT] = SettingsManager::LOG_FORMAT_STATUS;
}

LogManager::~LogManager() {
}

void LogManager::log(Area area, ParamMap& params) noexcept {
	log(getPath(area, params), Util::formatParams(getSetting(area, FORMAT), params));
}

void LogManager::ensureParam(const string& aParam, string& fileName) noexcept {
	if (fileName.find(aParam) != string::npos) {
		return;
	}

	auto slash = fileName.find_last_of("\\/");
	auto ext = fileName.rfind('.');

	//check that the dot is part of the file name (not in the directory name)
	auto appendPos = (ext == string::npos || (slash != string::npos && ext < slash)) ? fileName.size() : ext;

	fileName.insert(appendPos, "." + aParam);
}

void LogManager::log(const UserPtr& aUser, ParamMap& params) noexcept {
	if (aUser->isNMDC() || !SETTING(PM_LOG_GROUP_CID)) {
		log(PM, params);
		return;
	}

	auto path = getPath(aUser, params, true);
	log(path, Util::formatParams(getSetting(PM, FORMAT), params));
}

void LogManager::setRead() noexcept {
	auto unreadInfo = cache.setRead();
	if (unreadInfo.hasMessages()) {
		fire(LogManagerListener::MessagesRead());
	}
}

void LogManager::clearCache() noexcept {
	auto cleared = cache.clear();
	if (cleared > 0) {
		fire(LogManagerListener::Cleared());
	}
}

void LogManager::removePmCache(const UserPtr& aUser) noexcept {
	pmPaths.erase(aUser->getCID());
}

string LogManager::getPath(const UserPtr& aUser, ParamMap& params, bool addCache /*false*/) noexcept {
	if (aUser->isNMDC() || !SETTING(PM_LOG_GROUP_CID)) {
		return getPath(PM, params);
	}


	//is it cached? NOTE: must only be called from the GUI thread (no locking)
	auto p = pmPaths.find(aUser->getCID());
	if (p != pmPaths.end()) {
		//can we still use the same dir?
		if (Util::getFilePath(getPath(PM, params)) == Util::getFilePath(p->second))
			return p->second;
	}

	//check the directory
	auto fileName = getSetting(PM, FILE);
	ensureParam("%[userCID]", fileName);
	auto path = Util::validatePath(SETTING(LOG_DIRECTORY) + 
		Util::formatParams(fileName, params, Util::cleanPathSeparators));

	auto files = File::findFiles(Util::getFilePath(path), "*" + aUser->getCID().toBase32() + "*", File::TYPE_FILE);
	if (!files.empty()) {
		path = files.front();
	}

	if (addCache)
		pmPaths.emplace(aUser->getCID(), path);
	return path;
}

void LogManager::message(const string& aMsg, LogMessage::Severity aSeverity, const string& aLabel) noexcept {
	auto messageData = std::make_shared<LogMessage>(aMsg, aSeverity, LogMessage::Type::SYSTEM, aLabel);
	if (aSeverity != LogMessage::SEV_NOTIFY) {
		if (SETTING(LOG_SYSTEM)) {
			ParamMap params;
			params["message"] = aMsg;
			log(SYSTEM, params);
		}

		cache.addMessage(messageData);
	}

	fire(LogManagerListener::Message(), messageData);
}

string LogManager::getPath(Area area, ParamMap& params) const noexcept {
	return Util::validatePath(SETTING(LOG_DIRECTORY) + 
		Util::formatParams(getSetting(area, FILE), params, Util::cleanPathSeparators));
}

string LogManager::getPath(Area area) const noexcept {
	ParamMap params;
	return getPath(area, params);
}

const string& LogManager::getSetting(int area, int sel) const noexcept {
	return SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(options[area][sel]), true);
}

void LogManager::saveSetting(int area, int sel, const string& setting) const noexcept {
	SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(options[area][sel]), setting);
}

string LogManager::readFromEnd(const string& aPath, int aMaxLines, int64_t aBufferSize) noexcept {
	if (aMaxLines == 0) {
		return Util::emptyString;
	}

	string ret;
	try {
		File f(aPath, File::READ, File::OPEN);

		auto buf = f.readFromEnd(static_cast<size_t>(aBufferSize));

		StringList lines;
		if (Util::strnicmp(buf.c_str(), "\xef\xbb\xbf", 3) == 0) {
			// Remove UTF-8 BOM
			lines = StringTokenizer<string>(buf.substr(3), "\r\n", true).getTokens();
		} else {
			lines = StringTokenizer<string>(buf, "\r\n", true).getTokens();
		}

		int linesCount = lines.size();

		int i = max(linesCount - aMaxLines - 1, 0); // The last line will always be empty
		for (; i < linesCount; ++i) {
			ret += lines[i] + "\r\n";
		}
	} catch (const FileException&) {
	}

	return ret;
}

void LogManager::log(const string& area, const string& msg) noexcept {
	tasks.addTask([=] {
		auto aArea = Util::validatePath(area);
		try {
			File::ensureDirectory(aArea);
			File f(aArea, File::WRITE, File::OPEN | File::CREATE);
			f.setEndPos(0);
			f.write(msg + "\r\n");
		} catch (const FileException& e) {
			// Just don't try to write the error into a file...
			message(STRING_F(WRITE_FAILED_X, aArea % e.what()), LogMessage::SEV_NOTIFY, STRING(APPLICATION));
		}
	});
}

} // namespace dcpp
