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

#ifndef DCPLUSPLUS_DCPP_WEBSERVER_H
#define DCPLUSPLUS_DCPP_WEBSERVER_H

#include "stdinc.h"

#include "ApiRouter.h"
#include "FileServer.h"
#include "ApiRequest.h"

#include "Timer.h"
#include "WebServerManagerListener.h"
//#include "WebServerSettings.h"
#include "WebSocket.h"
#include "WebUserManager.h"

#include <airdcpp/format.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Util.h>

#include <iostream>
#include <boost/thread/thread.hpp>


namespace webserver {
	class ServerSettingItem;
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
		// The shared on_message handler takes a template parameter so the function can
		// resolve any endpoint dependent types like message_ptr or connection_ptr
		template <typename EndpointType>
		void on_message(EndpointType* aServer, websocketpp::connection_hdl hdl,
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
		void on_open_socket(EndpointType* aServer, websocketpp::connection_hdl hdl, bool aIsSecure) {
			WLock l(cs);
			auto socket = make_shared<WebSocket>(aIsSecure, hdl, aServer, this);
			sockets.emplace(hdl, socket);
		}

		void on_close_socket(websocketpp::connection_hdl hdl);

		void onPongReceived(websocketpp::connection_hdl hdl, const string& aPayload);
		void onPongTimeout(websocketpp::connection_hdl hdl, const string& aPayload);

		void onData(const string& aData, TransportType aType, Direction aDirection, const string& aIP) noexcept;

		template <typename EndpointType>
		void on_http(EndpointType* s, websocketpp::connection_hdl hdl, bool aIsSecure) {
			// Blocking HTTP Handler
			auto con = s->get_con_from_hdl(hdl);
			websocketpp::http::status_code::value status;
			auto ip = con->get_remote_endpoint();

			if (con->get_resource().length() >= 4 && con->get_resource().compare(0, 4, "/api") == 0) {
				auto path = con->get_resource().substr(4);

				onData(path + ": " + con->get_request().get_body(), TransportType::TYPE_HTTP_API, Direction::INCOMING, ip);

				json output, error;
				status = api.handleHttpRequest(
					path,
					con->get_request(),
					output,
					error,
					aIsSecure,
					ip
					);

				auto data = status != websocketpp::http::status_code::ok ? error.dump() : output.dump();
				onData(path + " (" + Util::toString(status) + "): " + data, TransportType::TYPE_HTTP_API, Direction::OUTGOING, ip);

				con->set_body(data);
				con->append_header("Content-Type", "application/json");
				con->set_status(status);
			} else {
				onData(con->get_request().get_method() + " " + con->get_resource(), TransportType::TYPE_HTTP_FILE, Direction::INCOMING, ip);

				StringPairList headers;
				std::string output;
				status = fileServer.handleRequest(con->get_resource(), con->get_request(), output, headers);

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
		void logout(LocalSessionId aSessionId) noexcept;
		WebSocketPtr getSocket(LocalSessionId aSessionToken) noexcept;

		bool load(const ErrorF& aErrorF) noexcept;
		bool save(const ErrorF& aErrorF) noexcept;

		WebUserManager& getUserManager() noexcept {
			return *userManager.get();
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

		const CallBack getShutdownF() const noexcept {
			return shutdownF;
		}
	private:
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

		context_ptr on_tls_init(websocketpp::connection_hdl hdl);

		typedef vector<WebSocketPtr> WebSocketList;
		std::map<websocketpp::connection_hdl, WebSocketPtr, std::owner_less<websocketpp::connection_hdl>> sockets;

		ApiRouter api;
		FileServer fileServer;

		unique_ptr<WebUserManager> userManager;

		TimerPtr socketTimer;

		server_plain endpoint_plain;
		server_tls endpoint_tls;

		boost::thread_group worker_threads;

		CallBack shutdownF;
	};
}

#endif // DCPLUSPLUS_DCPP_WEBSERVER_H
