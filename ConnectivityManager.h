/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_CONNECTIVITY_MANAGER_H
#define DCPLUSPLUS_DCPP_CONNECTIVITY_MANAGER_H

#include "noexcept.h"
#include "SettingsManager.h"
#include "Singleton.h"
#include "Speaker.h"

#include <string>
#include <unordered_map>
#include <boost/variant.hpp>

namespace dcpp {

using std::string;
using std::unordered_map;

class ConnectivityManagerListener {
public:
	virtual ~ConnectivityManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Message;
	typedef X<1> Started;
	typedef X<2> Finished;
	typedef X<3> SettingChanged; // auto-detection has been enabled / disabled

	virtual void on(Message, const string&) noexcept { }
	virtual void on(Started) noexcept { }
	virtual void on(Finished) noexcept { }
	virtual void on(SettingChanged) noexcept { }
};

class ConnectivityManager : public Singleton<ConnectivityManager>, public Speaker<ConnectivityManagerListener>
{
public:
	const string& get(SettingsManager::StrSetting setting) const;
	int get(SettingsManager::IntSetting setting) const;
	void set(SettingsManager::StrSetting setting, const string& str);

	void detectConnection();
	void setup(bool settingsChanged);
	void editAutoSettings();
	bool ok() const { return autoDetected; }
	bool isRunning() const { return running; }
	const string& getStatus() const { return status; }
	string getInformation() const;

private:
	friend class Singleton<ConnectivityManager>;
	friend class MappingManager;
	
	ConnectivityManager();
	virtual ~ConnectivityManager() { }

	void startMapping();
	void mappingFinished(const string& mapper);
	void log(string&& message);

	void startSocket();
	void listen();
	void disconnect();

	bool autoDetected;
	bool running;

	string status;

	/* contains auto-detected settings. they are stored separately from manual connectivity
	settings (stored in SettingsManager) in case the user wants to keep the manually set ones for
	future use. */
	unordered_map<int, boost::variant<int, string>> autoSettings;
};

#define CONNSETTING(k) ConnectivityManager::getInstance()->get(SettingsManager::k)

} // namespace dcpp

#endif // !defined(CONNECTIVITY_MANAGER_H)
