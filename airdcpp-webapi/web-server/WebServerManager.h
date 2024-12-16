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

#ifndef DCPLUSPLUS_WEBSERVER_WEBSERVERMANAGER_H
#define DCPLUSPLUS_WEBSERVER_WEBSERVERMANAGER_H

#include "stdinc.h"

#include "Timer.h"
#include "WebServerManagerListener.h"

#include <airdcpp/message/Message.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/Speaker.h>

#include <iostream>
#include <boost/thread/thread.hpp>


namespace webserver {
	class ServerSettingItem;

	class ContextMenuManager;
	class ExtensionManager;
	class WebServerSettings;
	class WebUserManager;
	class SocketManager;
	class HttpManager;

	struct ServerConfig {
		ServerConfig(ServerSettingItem& aPort, ServerSettingItem& aBindAddress) : port(aPort), bindAddress(aBindAddress) {

		}

		ServerSettingItem& port;
		ServerSettingItem& bindAddress;

		bool hasValidConfig() const noexcept;
	};

	// alias some of the bind related functions as they are a bit long
	using websocketpp::lib::placeholders::_1;
	using websocketpp::lib::placeholders::_2;
	using websocketpp::lib::bind;

	// type of the ssl context pointer is long so alias it
	using context_ptr = std::shared_ptr<boost::asio::ssl::context>;

	class WebServerManager : public dcpp::Singleton<WebServerManager>, public Speaker<WebServerManagerListener> {
	public:
		// Add a scheduled task
		// Note: the returned timer pointer must be kept alive by the caller while the timer is active
		TimerPtr addTimer(Callback&& aCallback, time_t aIntervalMillis, const Timer::CallbackWrapper& aCallbackWrapper = nullptr) noexcept;

		// Run a task in the task thread pool
		void addAsyncTask(Callback&& aCallback) noexcept;

		WebServerManager();
		~WebServerManager() override;

		// Leave the path empty to use the default resource path
		bool startup(const MessageCallback& errorF, const string& aWebResourcePath, const Callback& aShutdownF);

		// Start the server 
		// Throws Exception on errors
		bool start(const MessageCallback& errorF);
		void stop() noexcept;

		bool load(const MessageCallback& aErrorF) noexcept;
		bool save(const MessageCallback& aErrorF) noexcept;
		WebServerSettings& getSettingsManager() noexcept {
			return *settingsManager.get();
		}

		WebUserManager& getUserManager() noexcept {
			return *userManager.get();
		}

		ExtensionManager& getExtensionManager() noexcept {
			return *extManager.get();
		}

		ContextMenuManager& getContextMenuManager() noexcept {
			return *contextMenuManager.get();
		}

		SocketManager& getSocketManager() noexcept {
			return *socketManager.get();
		}

		HttpManager& getHttpManager() noexcept {
			return *httpManager.get();
		}

		bool hasValidServerConfig() const noexcept;
		bool hasUsers() const noexcept;
		bool waitExtensionsLoaded() const noexcept;

		const ServerConfig& getPlainServerConfig() const noexcept {
			return *plainServerConfig;
		}

		const ServerConfig& getTlsServerConfig() const noexcept {
			return *tlsServerConfig;
		}

		bool isRunning() const noexcept;

		bool isListeningPlain() const noexcept;
		bool isListeningTls() const noexcept;

		static boost::asio::ip::tcp getDefaultListenProtocol() noexcept;

		// Get the function for shutting down the application
		const Callback& getShutdownF() const noexcept {
			return shutdownF;
		}

		// Logging
		void log(const string& aMsg, LogMessage::Severity aSeverity) const noexcept;
		MessageCallback getDefaultErrorLogger() const noexcept;

		// Address utils
		string resolveAddress(const string& aHostname, const string& aPort) noexcept;
		string getLocalServerHttpUrl() noexcept;
		string getLocalServerAddress(const ServerConfig& aConfig) noexcept;
		static bool isAnyAddress(const string& aAddress) noexcept;

		// For command debugging
		void onData(const string& aData, TransportType aType, Direction aDirection, const string& aIP) noexcept;

		template <typename EndpointType>
		static void logDebugError(EndpointType* s, const string& aMessage, websocketpp::log::level aErrorLevel) noexcept {
			s->get_elog().write(aErrorLevel, aMessage);
		}

		WebServerManager(WebServerManager&) = delete;
		WebServerManager& operator=(WebServerManager&) = delete;

		IGETSET(bool, enableSocketLogging, EnableSocketLogging, false);
	private:
		context_ptr handleInitTls(websocketpp::connection_hdl hdl);

		bool listen(const MessageCallback& errorF);

		bool initialize(const MessageCallback& errorF);

		unique_ptr<ServerConfig> plainServerConfig;
		unique_ptr<ServerConfig> tlsServerConfig;

		mutable SharedMutex cs;

		// set up an external io_context to run both endpoints on. This is not
		// strictly necessary, but simplifies thread management a bit.
		boost::asio::io_context ios;
		bool hasIOContext = false;

		boost::asio::io_context tasks;
		boost::asio::executor_work_guard<decltype(tasks.get_executor())> wordGuardTasks;

		unique_ptr<WebUserManager> userManager;
		unique_ptr<ExtensionManager> extManager;
		unique_ptr<ContextMenuManager> contextMenuManager;
		unique_ptr<WebServerSettings> settingsManager;
		unique_ptr<SocketManager> socketManager;
		unique_ptr<HttpManager> httpManager;

		TimerPtr minuteTimer;

		server_plain endpoint_plain;
		server_tls endpoint_tls;

		// Web server threads
		unique_ptr<boost::thread_group> ios_threads;

		// Task threads (running of hooks, timers or other long running task, or just to avoid deadlocks)
		// 
		// IMPORTANT:
		// Calling hooks and handling the hook return data must be handled by separate thread pools to avoid the case when 
		// all task threads are waiting for a hook response (and there are no threads left to handle those)
		unique_ptr<boost::thread_group> task_threads;

		Callback shutdownF;
	};
}

#endif // DCPLUSPLUS_DCPP_WEBSERVER_H
