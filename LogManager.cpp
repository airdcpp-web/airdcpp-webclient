/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

void LogManager::log(Area area, ParamMap& params) noexcept {
	log(SETTING(LOG_DIRECTORY) + Util::formatParams(getSetting(area, FILE), params), Util::formatParams(getSetting(area, FORMAT), params));
}

void LogManager::ensureParam(const string& aParam, string& fileName) {
	if (fileName.find(aParam) == string::npos) {
		auto slash = fileName.find_last_of("\\/");

		auto ext = fileName.rfind('.');


		//check that the dot is part of the file name (not in the directory name)
		auto appendPos = (ext == string::npos || (slash != string::npos && ext < slash)) ? fileName.size() : ext;

		fileName.insert(appendPos, "." + aParam);
		//fileName.append("." + aParam, appendPos, string::npos);
	}
}

void LogManager::log(const UserPtr& aUser, ParamMap& params) {
	if (aUser->isNMDC() || !SETTING(PM_LOG_GROUP_CID)) {
		log(PM, params);
		return;
	}

	auto path = getPath(aUser, params, true);
	log(path, Util::formatParams(getSetting(PM, FORMAT), params));
}

void LogManager::removePmCache(const UserPtr& aUser) {
	pmPaths.erase(aUser->getCID());
}

string LogManager::getPath(const UserPtr& aUser, ParamMap& params, bool addCache /*false*/) {
	if (aUser->isNMDC() || !SETTING(PM_LOG_GROUP_CID)) {
		return getPath(PM, params);
	}


	//is it cached?
	auto p = pmPaths.find(aUser->getCID());
	if (p != pmPaths.end()) {
		//can we still use the same dir?
		if (Util::getFilePath(getPath(PM, params)) == Util::getFilePath(p->second))
			return p->second;
	}

	//check the directory
	string fileName = getSetting(PM, FILE);
	ensureParam("%[userCID]", fileName);
	string path = SETTING(LOG_DIRECTORY) + Util::formatParams(fileName, params);

	auto files = File::findFiles(Util::getFilePath(path), "*" + aUser->getCID().toBase32() + "*", File::TYPE_FILE);
	if (!files.empty()) {
		path = files.front();
	}

	if (addCache)
		pmPaths.emplace(aUser->getCID(), path);
	return path;
}

void LogManager::message(const string& msg, Severity severity) {
	if(SETTING(LOG_SYSTEM)) {
		ParamMap params;
		params["message"] = msg;
		log(SYSTEM, params);
	}
	time_t t = GET_TIME();
	{
		Lock l(cs);
		// Keep the last 100 messages (completely arbitrary number...)
		while(lastLogs.size() > 100)
			lastLogs.pop_front();

		lastLogs.emplace_back(msg, MessageData(t, severity));
	}
	fire(LogManagerListener::Message(), t, msg, severity);
}

LogManager::List LogManager::getLastLogs() {
	Lock l(cs);
	return lastLogs;
}
void LogManager::clearLastLogs() {
	Lock l(cs);
	return lastLogs.clear();
}
string LogManager::getPath(Area area, ParamMap& params) const {
	return Util::validateFileName(SETTING(LOG_DIRECTORY) + Util::formatParams(getSetting(area, FILE), params));
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
	Lock l(cs);
	try {
		string aArea = Util::validateFileName(area);
		File::ensureDirectory(aArea);
		File f(aArea, File::WRITE, File::OPEN | File::CREATE);
		f.setEndPos(0);
		f.write(msg + "\r\n");
	} catch (const FileException&) {
		// ...
	}
}

LogManager::LogManager() {
	options[UPLOAD][FILE]		= SettingsManager::LOG_FILE_UPLOAD;
	options[UPLOAD][FORMAT]		= SettingsManager::LOG_FORMAT_POST_UPLOAD;
	options[DOWNLOAD][FILE]		= SettingsManager::LOG_FILE_DOWNLOAD;
	options[DOWNLOAD][FORMAT]	= SettingsManager::LOG_FORMAT_POST_DOWNLOAD;
	options[CHAT][FILE]		= SettingsManager::LOG_FILE_MAIN_CHAT;
	options[CHAT][FORMAT]		= SettingsManager::LOG_FORMAT_MAIN_CHAT;
	options[PM][FILE]		= SettingsManager::LOG_FILE_PRIVATE_CHAT;
	options[PM][FORMAT]		= SettingsManager::LOG_FORMAT_PRIVATE_CHAT;
	options[SYSTEM][FILE]		= SettingsManager::LOG_FILE_SYSTEM;
	options[SYSTEM][FORMAT]		= SettingsManager::LOG_FORMAT_SYSTEM;
	options[STATUS][FILE]		= SettingsManager::LOG_FILE_STATUS;
	options[STATUS][FORMAT]		= SettingsManager::LOG_FORMAT_STATUS;
}

LogManager::~LogManager() {
}

} // namespace dcpp
