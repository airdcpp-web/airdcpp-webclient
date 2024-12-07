/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_EXTENSIONINFO_H
#define DCPLUSPLUS_DCPP_EXTENSIONINFO_H

#include <web-server/ExtensionListener.h>

#include <api/base/HierarchicalApiModule.h>

#include <airdcpp/core/header/typedefs.h>

namespace webserver {
	class ExtensionManager;
	class ExtensionInfo : public SubApiModule<std::string, ExtensionInfo, std::string>, private ExtensionListener {
	public:
		typedef shared_ptr<ExtensionInfo> Ptr;

		ExtensionInfo(ParentType* aParentModule, const ExtensionPtr& aExtension);
		~ExtensionInfo();

		static const StringList subscriptionList;

		const ExtensionPtr& getExtension() const noexcept {
			return extension;
		}

		ExtensionPtr getExtension() noexcept {
			return extension;
		}

		string getId() const noexcept override;

		void init() noexcept override;

		static json serializeLogs(const ExtensionPtr& aExtension) noexcept;
		static json serializeExtension(const ExtensionPtr& aExtension) noexcept;
	private:
		void on(ExtensionListener::SettingValuesUpdated, const Extension*, const SettingValueMap& aUpdatedSettings) noexcept override;
		void on(ExtensionListener::SettingDefinitionsUpdated, const Extension*) noexcept override;

		void on(ExtensionListener::ExtensionStarted, const Extension*) noexcept override;
		void on(ExtensionListener::ExtensionStopped, const Extension*, bool aFailed) noexcept override;
		void on(ExtensionListener::PackageUpdated, const Extension*) noexcept override;
		void on(ExtensionListener::StateUpdated, const Extension*) noexcept override;

		api_return handleStartExtension(ApiRequest& aRequest);
		api_return handleStopExtension(ApiRequest& aRequest);
		api_return handleReady(ApiRequest& aRequest);
		api_return handleUpdateProperties(ApiRequest& aRequest);

		api_return handleGetSettingDefinitions(ApiRequest& aRequest);
		api_return handlePostSettingDefinitions(ApiRequest& aRequest);

		api_return handleGetSettings(ApiRequest& aRequest);
		api_return handlePostSettings(ApiRequest& aRequest);

		ExtensionPtr extension;

		void onUpdated(const JsonCallback& aDataCallback) noexcept;
	};
}

#endif