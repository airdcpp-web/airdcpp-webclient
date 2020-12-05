/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_WEBSERVER_H
#define DCPLUSPLUS_DCPP_WEBSERVER_H

#include "stdinc.h"

#include "ApiRouter.h"
#include "FileServer.h"
#include "ApiRequest.h"

#include "HttpUtil.h"
#include "SystemUtil.h"
#include "Timer.h"
#include "WebServerManagerListener.h"
#include "WebUserManager.h"
#include "WebSocket.h"
#include "WebServerSettings.h"

#include <airdcpp/format.h>
#include <airdcpp/Message.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Util.h>

#include <iostream>
#include <boost/thread/thread.hpp>


namespace webserver {
	class ServerSettingItem;

	class ContextMenuManager;
	class ExtensionManager;
	class WebUserManager;

	struct ServerConfig {
		ServerConfig(ServerSettingItem& aPort, ServerSettingItem& aBindAddress) : port(aPort), bindAddress(aBindAddress) {

		}

		ServerSettingItem& port;
		ServerSettingItem& bindAddress;

		bool hasValidConfig() const noexcept;
		void save(SimpleXML& aXml, const string& aTagName) noexcept;
	};

	// alias some of the bind related functions as they are a bit long
	using websocketpp::lib::placeholders::_1;
	using websocketpp::lib::placeholders::_2;
	using websocketpp::lib::bind;

	// type of the ssl context pointer is long so alias it
	typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;

	class WebServerManager : public dcpp::Singleton<WebServerManager>, public Speaker<WebServerManagerListener> {
	public:
		// Add a scheduled task
		// Note: the returned timer pointer must be kept alive by the caller while the timer is active
		TimerPtr addTimer(CallBack&& aCallBack, time_t aIntervalMillis, const Timer::CallbackWrapper& aCallbackWrapper = nullptr) noexcept;

		// Run a task in the task thread pool
		void addAsyncTask(CallBack&& aCallBack) noexcept;

		WebServerManager();
		~WebServerManager();

		typedef std::function<void(const string&)> ErrorF;

		// Leave the path empty to use the default resource path
		bool startup(const ErrorF& errorF, const string& aWebResourcePath, const CallBack& aShutdownF);

		// Start the server 
		// Throws Exception on errors
		bool start(const ErrorF& errorF);
		void stop() noexcept;

		// Disconnect all sockets
		void disconnectSockets(const std::string& aMessage) noexcept;

		// Reset sessions for associated sockets
		WebSocketPtr getSocket(LocalSessionId aSessionToken) noexcept;

		void setDirty() noexcept;
		bool load(const ErrorF& aErrorF) noexcept;
		bool save(const ErrorF& aErrorF) noexcept;
		string getConfigFilePath() const noexcept;
		WebServerSettings& getSettings() noexcept {
			return settings;
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

		bool hasValidServerConfig() const noexcept;
		bool hasUsers() const noexcept;
		bool waitExtensionsLoaded() const noexcept;

		ServerConfig& getPlainServerConfig() noexcept {
			return plainServerConfig;
		}

		ServerConfig& getTlsServerConfig() noexcept {
			return tlsServerConfig;
		}

		// Get location of the file server root directory (Web UI files)
		string getResourcePath() const noexcept {
			return fileServer.getResourcePath();
		}

		const FileServer& getFileServer() const noexcept {
			return fileServer;
		}

		bool isRunning() const noexcept;

		bool isListeningPlain() const noexcept;
		bool isListeningTls() const noexcept;

		static boost::asio::ip::tcp getDefaultListenProtocol() noexcept;

		// Get the function for shutting down the application
		const CallBack getShutdownF() const noexcept {
			return shutdownF;
		}

		// Logging
		void log(const string& aMsg, LogMessage::Severity aSeverity) const noexcept;
		ErrorF getDefaultErrorLogger() const noexcept;

		// Address utils
		string resolveAddress(const string& aHostname, const string& aPort) noexcept;
		string getLocalServerHttpUrl() noexcept;
		string getLocalServerAddress(const ServerConfig& aConfig) noexcept;
		static bool isAnyAddress(const string& aAddress) noexcept;

		// For command debugging
		void onData(const string& aData, TransportType aType, Direction aDirection, const string& aIP) noexcept;

		template <typename EndpointType>
		void logDebugError(EndpointType* s, const string& aMessage, websocketpp::log::level aErrorLevel) {
			s->get_elog().write(aErrorLevel, aMessage);
		}
	private:
		// Websocketpp event handlers
		template <typename EndpointType>
		void handleSocketConnected(EndpointType* aServer, websocketpp::connection_hdl hdl, bool aIsSecure) {
			auto con = aServer->get_con_from_hdl(hdl);
			auto socket = make_shared<WebSocket>(aIsSecure, hdl, con->get_request(), aServer, this);

			addSocket(hdl, socket);
		}

		void handleSocketDisconnected(websocketpp::connection_hdl hdl);

		void handlePongReceived(websocketpp::connection_hdl hdl, const string& aPayload);
		void handlePongTimeout(websocketpp::connection_hdl hdl, const string& aPayload);

		// The shared on_message handler takes a template parameter so the function can
		// resolve any endpoint dependent types like message_ptr or connection_ptr
		template <typename EndpointType>
		void handleSocketMessage(EndpointType*, websocketpp::connection_hdl hdl,
			typename EndpointType::message_ptr msg, bool aIsSecure) {

			auto socket = getSocket(hdl);
			if (!socket) {
				dcassert(0);
				return;
			}

			onData(msg->get_payload(), TransportType::TYPE_SOCKET, Direction::INCOMING, socket->getIp());
			api.handleSocketRequest(msg->get_payload(), socket, aIsSecure);
		}

		template <typename EndpointType>
		void handleHttpRequest(EndpointType* s, websocketpp::connection_hdl hdl, bool aIsSecure) {
			// Blocking HTTP Handler
			auto con = s->get_con_from_hdl(hdl);
			auto ip = con->get_raw_socket().remote_endpoint().address().to_string();

			SessionPtr session = nullptr;

			auto authToken = HttpUtil::parseAuthToken(con->get_request());
			if (authToken != websocketpp::http::empty_header) {
				try {
					session = userManager->parseHttpSession(authToken, ip);
				} catch (const std::exception& e) {
					con->set_body(e.what());
					con->set_status(websocketpp::http::status_code::unauthorized);
					return;
				}
			}

			if (con->get_resource().length() >= 4 && con->get_resource().compare(0, 4, "/api") == 0) {
				onData(con->get_resource() + ": " + con->get_request().get_body(), TransportType::TYPE_HTTP_API, Direction::INCOMING, ip);


				const auto responseF = [this, s, con, ip](websocketpp::http::status_code::value aStatus, const json& aResponseJsonData, const json& aResponseErrorJson) {
					string data;
					const auto& responseJson = !aResponseErrorJson.is_null() ? aResponseErrorJson : aResponseJsonData;
					if (!responseJson.is_null()) {
						try {
							data = responseJson.dump();
						} catch (const std::exception& e) {
							logDebugError(s, "Failed to convert data to JSON: " + string(e.what()), websocketpp::log::elevel::fatal);

							con->set_body("Failed to convert data to JSON: " + string(e.what()));
							con->set_status(websocketpp::http::status_code::internal_server_error);
							return;
						}
					}

					onData(con->get_resource() + " (" + Util::toString(aStatus) + "): " + data, TransportType::TYPE_HTTP_API, Direction::OUTGOING, ip);

					con->set_body(data);
					con->append_header("Content-Type", "application/json");
					con->append_header("Connection", "close"); // Workaround for https://github.com/zaphoyd/websocketpp/issues/890
					con->set_status(aStatus);
				};


				bool isDeferred = false;
				const auto deferredF = [&]() {
					con->defer_http_response();
					isDeferred = true;

					return [=](websocketpp::http::status_code::value aStatus, const json& aResponseJsonData, const json& aResponseErrorJson) {
						responseF(aStatus, aResponseJsonData, aResponseErrorJson);
						con->send_http_response();
					};
				};

				json output, apiError;
				auto status = api.handleHttpRequest(
					con->get_resource(),
					con->get_request(),
					output,
					apiError,
					aIsSecure,
					ip,
					session,
					deferredF
				);

				if (!isDeferred) {
					responseF(status, output, apiError);
				}
			} else {
				onData(con->get_request().get_method() + " " + con->get_resource(), TransportType::TYPE_HTTP_FILE, Direction::INCOMING, ip);

				StringPairList headers;
				std::string output;


				const auto responseF = [this, con, ip](websocketpp::http::status_code::value aStatus, const string& aOutput, const StringPairList& aHeaders = StringPairList()) {
					onData(
						con->get_request().get_method() + " " + con->get_resource() + ": " + Util::toString(aStatus) + " (" + Util::formatBytes(aOutput.length()) + ")",
						TransportType::TYPE_HTTP_FILE,
						Direction::OUTGOING,
						ip
					);

					con->append_header("Connection", "close"); // Workaround for https://github.com/zaphoyd/websocketpp/issues/890

					if (HttpUtil::isStatusOk(aStatus)) {
						// Don't set any incomplete/invalid headers in case of errors...
						for (const auto& p : aHeaders) {
							con->append_header(p.first, p.second);
						}

						con->set_status(aStatus);
						con->set_body(aOutput);
					} else {
						con->set_status(aStatus, aOutput);
						con->set_body(aOutput);
					}
				};

				bool isDeferred = false;
				const auto deferredF = [&]() {
					con->defer_http_response();
					isDeferred = true;

					return [=](websocketpp::http::status_code::value aStatus, const string& aOutput, const StringPairList& aHeaders) {
						responseF(aStatus, aOutput, aHeaders);
						con->send_http_response();
					};
				};

				auto status = fileServer.handleRequest(con->get_request(), output, headers, session, deferredF);
				if (!isDeferred) {
					responseF(status, output, headers);
				}
			}
		}

		template<class T>
		void setEndpointHandlers(T& aEndpoint, bool aIsSecure, WebServerManager* aServer) {
			aEndpoint.set_http_handler(
				std::bind(&WebServerManager::handleHttpRequest<T>, aServer, &aEndpoint, _1, aIsSecure));
			aEndpoint.set_message_handler(
				std::bind(&WebServerManager::handleSocketMessage<T>, aServer, &aEndpoint, _1, _2, aIsSecure));

			aEndpoint.set_close_handler(std::bind(&WebServerManager::handleSocketDisconnected, aServer, _1));
			aEndpoint.set_open_handler(std::bind(&WebServerManager::handleSocketConnected<T>, aServer, &aEndpoint, _1, aIsSecure));

			aEndpoint.set_pong_timeout_handler(std::bind(&WebServerManager::handlePongTimeout, aServer, _1, _2));
		}

		WebServerSettings settings;

		context_ptr handleInitTls(websocketpp::connection_hdl hdl);

		void addSocket(websocketpp::connection_hdl hdl, const WebSocketPtr& aSocket) noexcept;
		WebSocketPtr getSocket(websocketpp::connection_hdl hdl) const noexcept;
		bool listen(const ErrorF& errorF);

		bool initialize(const ErrorF& errorF);

		ServerConfig plainServerConfig;
		ServerConfig tlsServerConfig;

		void loadServer(SimpleXML& xml_, const string& aTagName, ServerConfig& config_, bool aTls) noexcept;
		void pingTimer() noexcept;

		mutable SharedMutex cs;

		// set up an external io_service to run both endpoints on. This is not
		// strictly necessary, but simplifies thread management a bit.
		boost::asio::io_service ios;
		boost::asio::io_service tasks;
		boost::asio::io_service::work work;
		bool has_io_service = false;

		typedef vector<WebSocketPtr> WebSocketList;
		std::map<websocketpp::connection_hdl, WebSocketPtr, std::owner_less<websocketpp::connection_hdl>> sockets;

		ApiRouter api;
		FileServer fileServer;

		unique_ptr<WebUserManager> userManager;
		unique_ptr<ExtensionManager> extManager;
		unique_ptr<ContextMenuManager> contextMenuManager;

		TimerPtr minuteTimer;
		TimerPtr socketPingTimer;

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

		CallBack shutdownF;
		bool isDirty = false;
	};
}

#endif // DCPLUSPLUS_DCPP_WEBSERVER_H
