/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#include "Timer.h"
#include "WebServerManagerListener.h"
#include "WebUserManager.h"
#include "WebSocket.h"

#include <airdcpp/format.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Util.h>

#include <iostream>
#include <boost/thread/thread.hpp>


namespace webserver {
	class ServerSettingItem;

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
		TimerPtr addTimer(CallBack&& aCallBack, time_t aIntervalMillis, const Timer::CallbackWrapper& aCallbackWrapper = nullptr) noexcept;
		void addAsyncTask(CallBack&& aCallBack) noexcept;

		WebServerManager();
		~WebServerManager();

		typedef std::function<void(const string&)> ErrorF;

		// Leave the path empty to use the default resource path
		bool startup(const ErrorF& errorF, const string& aWebResourcePath, const CallBack& aShutdownF);

		bool start(const ErrorF& errorF);
		void stop();

		void disconnectSockets(const std::string& aMessage) noexcept;

		// Reset sessions for associated sockets
		WebSocketPtr getSocket(LocalSessionId aSessionToken) noexcept;

		bool load(const ErrorF& aErrorF) noexcept;
		bool save(const ErrorF& aErrorF) noexcept;

		WebUserManager& getUserManager() noexcept {
			return *userManager.get();
		}

		ExtensionManager& getExtensionManager() noexcept {
			return *extManager.get();
		}

		bool hasValidConfig() const noexcept;

		ServerConfig& getPlainServerConfig() noexcept {
			return plainServerConfig;
		}

		ServerConfig& getTlsServerConfig() noexcept {
			return tlsServerConfig;
		}

		string getConfigPath() const noexcept;
		string getResourcePath() const noexcept {
			return fileServer.getResourcePath();
		}

		bool isRunning() const noexcept;

		bool isListeningPlain() const noexcept;
		bool isListeningTls() const noexcept;

		static boost::asio::ip::tcp getDefaultListenProtocol() noexcept;

		const CallBack getShutdownF() const noexcept {
			return shutdownF;
		}

		// For command debugging
		void onData(const string& aData, TransportType aType, Direction aDirection, const string& aIP) noexcept;

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

			// Messages received from each socket will always use the same thread
			// This will also help with hooks getting timed out when they are being run and
			// resolved by the same socket
			// TODO: use different threads for handling requests that involve running of hooks
			addAsyncTask([=] {
				auto s = socket;
				api.handleSocketRequest(msg->get_payload(), s, aIsSecure); 
			});
		}

		template <typename EndpointType>
		void handleHttpRequest(EndpointType* s, websocketpp::connection_hdl hdl, bool aIsSecure) {
			// Blocking HTTP Handler
			auto con = s->get_con_from_hdl(hdl);
			websocketpp::http::status_code::value status;
			auto ip = con->get_remote_endpoint();

			string authError;
			auto session = userManager->parseHttpSession(con->get_request(), authError, ip);

			// Catch invalid authentication info
			if (!authError.empty()) {
				con->set_body(authError);
				con->set_status(websocketpp::http::status_code::unauthorized);
				return;
			}

			if (con->get_resource().length() >= 4 && con->get_resource().compare(0, 4, "/api") == 0) {
				onData(con->get_resource() + ": " + con->get_request().get_body(), TransportType::TYPE_HTTP_API, Direction::INCOMING, ip);

				json output, apiError;
				status = api.handleHttpRequest(
					con->get_resource(),
					con->get_request(),
					output,
					apiError,
					aIsSecure,
					ip,
					session
				);

				auto data = status != websocketpp::http::status_code::ok ? apiError.dump() : output.dump();
				onData(con->get_resource() + " (" + Util::toString(status) + "): " + data, TransportType::TYPE_HTTP_API, Direction::OUTGOING, ip);

				con->set_body(data);
				con->append_header("Content-Type", "application/json");
				con->set_status(status);
			} else {
				onData(con->get_request().get_method() + " " + con->get_resource(), TransportType::TYPE_HTTP_FILE, Direction::INCOMING, ip);

				StringPairList headers;
				std::string output;
				status = fileServer.handleRequest(con->get_resource(), con->get_request(), output, headers, session);

				for (const auto& p : headers) {
					con->append_header(p.first, p.second);
				}

				onData(
					con->get_request().get_method() + " " + con->get_resource() + ": " + Util::toString(status) + " (" + Util::formatBytes(output.length()) + ")",
					TransportType::TYPE_HTTP_FILE,
					Direction::OUTGOING,
					ip
				);

				con->set_status(status);
				con->set_body(output);
			}
		}
	private:
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
		bool has_io_service = false;

		typedef vector<WebSocketPtr> WebSocketList;
		std::map<websocketpp::connection_hdl, WebSocketPtr, std::owner_less<websocketpp::connection_hdl>> sockets;

		ApiRouter api;
		FileServer fileServer;

		unique_ptr<WebUserManager> userManager;
		unique_ptr<ExtensionManager> extManager;

		TimerPtr socketTimer;

		server_plain endpoint_plain;
		server_tls endpoint_tls;

		boost::thread_group worker_threads;

		CallBack shutdownF;
	};
}

#endif // DCPLUSPLUS_DCPP_WEBSERVER_H
