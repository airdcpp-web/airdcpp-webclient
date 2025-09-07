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

#ifndef DCPLUSPLUS_DCPP_CONNECTIVITY_MANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_CONNECTIVITY_MANAGER_LISTENER_H


#include <airdcpp/forward.h>

#include <string>

namespace dcpp {

using std::string;

class ConnectivityManagerListener {
public:
	virtual ~ConnectivityManagerListener() {}
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Message;
	typedef X<1> Started;
	typedef X<2> Finished;
	typedef X<3> SettingChanged; // auto-detection has been enabled / disabled

	virtual void on(Message, const LogMessagePtr&) noexcept {}
	virtual void on(Started, bool /*v6*/) noexcept {}
	virtual void on(Finished, bool /*v6*/, bool /*failed*/) noexcept {}
	virtual void on(SettingChanged) noexcept {}
};

#define CONNSETTING(k) ConnectivityManager::getInstance()->get(SettingsManager::k)

} // namespace dcpp

#endif // !defined(CONNECTIVITY_MANAGER_H)
