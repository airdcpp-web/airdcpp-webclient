/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SEARCHAPI_H
#define DCPLUSPLUS_DCPP_SEARCHAPI_H

#include <web-server/stdinc.h>

#include <api/SearchEntity.h>

#include <api/HierarchicalApiModule.h>

#include <airdcpp/typedefs.h>


namespace webserver {
	class SearchApi : public ParentApiModule<SearchInstanceToken, SearchEntity> {
	public:
		static StringList subscriptionList;

		SearchApi(Session* aSession);
		~SearchApi();
	private:
		static json serializeSearchInstance(const SearchEntity& aSearch) noexcept;
		SearchEntity::Ptr createInstance(uint64_t aExpirationTick);

		api_return handleCreateInstance(ApiRequest& aRequest);
		api_return handleDeleteInstance(ApiRequest& aRequest);

		api_return handleGetTypes(ApiRequest& aRequest);

		void onTimer() noexcept;

		SearchInstanceToken instanceIdCounter = 0;
		TimerPtr timer;
	};
}

#endif