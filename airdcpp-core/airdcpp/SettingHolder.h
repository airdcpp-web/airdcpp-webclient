/*
* Copyright (C) 2013-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SETTINGHOLDER_
#define DCPLUSPLUS_DCPP_SETTINGHOLDER_

#include <airdcpp/SettingsManager.h>

namespace dcpp {


class SettingHolder {
public:
	SettingHolder(MessageCallback&& errorF);
	~SettingHolder();

	void apply() const;
private:
	struct SettingValueListHolder {
		const SettingsManager::SettingChangeHandler& handler;
		SettingsManager::SettingValueList values;
	};

	typedef vector<SettingValueListHolder> ValueHolderList;

	ValueHolderList valueHolders;
	
	void showError(const string& aError) const noexcept;

	const MessageCallback errorF;
};

}

#endif