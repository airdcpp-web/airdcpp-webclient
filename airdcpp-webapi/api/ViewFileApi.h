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

#ifndef DCPLUSPLUS_DCPP_FILEVIEW_API_H
#define DCPLUSPLUS_DCPP_FILEVIEW_API_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/ViewFileManagerListener.h>

#include <api/ApiModule.h>

namespace webserver {
	class ViewFileApi : public SubscribableApiModule, private ViewFileManagerListener {
	public:

		ViewFileApi(Session* aSession);
		~ViewFileApi();

		int getVersion() const noexcept override {
			return 0;
		}
	private:
		api_return handleGetFiles(ApiRequest& aRequest);

		api_return handleAddFile(ApiRequest& aRequest);
		api_return handleRemoveFile(ApiRequest& aRequest);

		api_return handleGetText(ApiRequest& aRequest);
		api_return handleSetRead(ApiRequest& aRequest);

		static json serializeFile(const ViewFilePtr& aFile) noexcept;
		void onViewFileUpdated(const ViewFilePtr& aFile) noexcept;

		void on(ViewFileManagerListener::FileAdded, const ViewFilePtr& aFile) noexcept override;
		void on(ViewFileManagerListener::FileClosed, const ViewFilePtr& aFile) noexcept override;
		void on(ViewFileManagerListener::FileStateUpdated, const ViewFilePtr& aFile) noexcept override;
		void on(ViewFileManagerListener::FileFinished, const ViewFilePtr& aFile) noexcept override;
		void on(ViewFileManagerListener::FileRead, const ViewFilePtr& aFile) noexcept override;
	};
}

#endif