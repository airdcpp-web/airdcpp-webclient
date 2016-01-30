/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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
#include "TimerManager.h"

namespace dcpp {

LogManager::LogManager() : tasks(true), cache(SettingsManager::LOG_MESSAGE_CACHE) {

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

void LogManager::ensureParam(const string& aParam, string& fileName) {
	if (fileName.find(aParam) != string::npos) {
		return;
	}

	auto slash = fileName.find_last_of("\\/");
	auto ext = fileName.rfind('.');

	//check that the dot is part of the file name (not in the directory name)
	auto appendPos = (ext == string::npos || (slash != string::npos && ext < slash)) ? fileName.size() : ext;

	fileName.insert(appendPos, "." + aParam);
}

void LogManager::log(const UserPtr& aUser, ParamMap& params) {
	if (aUser->isNMDC() || !SETTING(PM_LOG_GROUP_CID)) {
		log(PM, params);
		return;
	}

	auto path = getPath(aUser, params, true);
	log(path, Util::formatParams(getSetting(PM, FORMAT), params));
}

void LogManager::setRead() noexcept {
	auto updated = cache.setRead();
	if (updated > 0) {
		fire(LogManagerListener::MessagesRead());
	}
}

void LogManager::clearCache() noexcept {
	auto cleared = cache.clear();
	if (cleared > 0) {
		fire(LogManagerListener::Cleared());
	}
}

void LogManager::removePmCache(const UserPtr& aUser) {
	pmPaths.erase(aUser->getCID());
}

string LogManager::getPath(const UserPtr& aUser, ParamMap& params, bool addCache /*false*/) {
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

void LogManager::message(const string& msg, LogMessage::Severity severity) {
	auto messageData = std::make_shared<LogMessage>(msg, severity);
	if (severity != LogMessage::SEV_NOTIFY) {
		if (SETTING(LOG_SYSTEM)) {
			ParamMap params;
			params["message"] = msg;
			log(SYSTEM, params);
		}

		cache.addMessage(messageData);
	}

	fire(LogManagerListener::Message(), messageData);
}

string LogManager::getPath(Area area, ParamMap& params) const {
	return Util::validatePath(SETTING(LOG_DIRECTORY) + 
		Util::formatParams(getSetting(area, FILE), params, Util::cleanPathSeparators));
}

string LogManager::getPath(Area area) const {
	ParamMap params;
	return getPath(area, params);
}

const string& LogManager::getSetting(int area, int sel) const {
	return SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(options[area][sel]), true);
}

void LogManager::saveSetting(int area, int sel, const string& setting) {
	SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(options[area][sel]), setting);
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
			message(STRING_F(WRITE_FAILED_X, aArea % e.what()), LogMessage::SEV_NOTIFY);
		}
	});
}

} // namespace dcpp
