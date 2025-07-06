/*
 * Copyright (C) 2012-2021 AirDC++ Project
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

#ifndef AIRDCPPD_CLIENT_H
#define AIRDCPPD_CLIENT_H

#include "CDMDebug.h"

#include <airdcpp/hub/ClientManagerListener.h>
#include <airdcpp/filelist/DirectoryListingManagerListener.h>
#include <airdcpp/core/thread/Semaphore.h>
#include <airdcpp/core/classes/StartupParams.h>


namespace airdcppd {

class CDMDebug;

class Client : private ClientManagerListener, private DirectoryListingManagerListener {

public:
	Client(bool aAsDaemon);
	void run(const dcpp::StartupParams& aStartupParams);
	void stop();
private:
	bool startup(const dcpp::StartupParams& aStartupParams);
	void shutdown();

	void on(DirectoryListingManagerListener::OpenListing, const DirectoryListingPtr& aList, const string& aDir, const string& aXML) noexcept;

	void on(ClientManagerListener::ClientCreated, const ClientPtr&) noexcept;
	void on(ClientManagerListener::ClientRedirected, const ClientPtr& aOldClient, const ClientPtr& aNewClient) noexcept;

	bool running = false;
	bool asDaemon = false;

	unique_ptr<CDMDebug> cdmDebug;
	Semaphore shutdownSemaphore;
};

} // namespace airdcppd

#endif //
