/*
* Copyright (C) 2011-2015 AirDC++ Project
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
#include "WebSocket.h"
#include "WebUserManager.h"

#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>

#include <iostream>

namespace webserver {
	struct ServerConfig {
		IGETSET(int, port, Port, -1);

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

			WebSocketPtr socket = nullptr;

			{
				WLock l(cs);
				auto s = sockets.find(hdl);
				if (s != sockets.end()) {
					socket = s->second;
				} else {
					dcassert(0);
					return;
				}
			}

			api.handleSocketRequest(msg->get_payload(), socket, aIsSecure);
		}

		template <typename EndpointType>
		void on_open_socket(EndpointType* aServer, websocketpp::connection_hdl hdl, bool aIsSecure) {

			WLock l(cs);
			auto socket = make_shared<WebSocket>(aIsSecure, hdl, aServer);
			sockets.emplace(hdl, socket);
		}

		void on_close_socket(websocketpp::connection_hdl hdl);

		template <typename EndpointType>
		void on_http(EndpointType* s, websocketpp::connection_hdl hdl, bool aIsSecure) {
			// Blocking HTTP Handler
			auto con = s->get_con_from_hdl(hdl);
			SessionPtr session = nullptr;
			auto token = con->get_request_header("Authorization");
			if (token != websocketpp::http::empty_header) {
				session = userManager->getSession(token);
			}

			websocketpp::http::status_code::value status;

			if (con->get_resource().length() >= 4 && con->get_resource().compare(0, 4, "/api") == 0) {
				json output, error;

				status = api.handleHttpRequest(
					con->get_resource().substr(4),
					session,
					con->get_request_body(),
					output,
					error,
					aIsSecure,
					con->get_request().get_method(),
					con->get_remote_endpoint()
					);

				if (status != websocketpp::http::status_code::ok) {
					con->set_body(error.dump(4));
				} else {
					con->set_body(output.dump());
				}

				con->append_header("Content-Type", "application/json");
				con->set_status(status);
			}
			else {
				std::string  contentType, output;
				status = fileServer.handleRequest(con->get_resource(), session, con->get_request_body(), output, contentType);
				con->append_header("Content-Type", contentType);
				con->set_status(status);
				con->set_body(output);
			}
		}

		TimerPtr addTimer(CallBack&& aCallBack, time_t aIntervalMillis) noexcept;
		void addAsyncTask(CallBack&& aCallBack) noexcept;

		WebServerManager();
		~WebServerManager();

		typedef std::function<void(const string&)> ErrorF;

		// Leave the path empty to use the default resource path
		bool start(ErrorF errorF, const string& aWebResourcePath = "");
		void stop();

		void disconnectSockets(const std::string& aMessage) noexcept;

		// Reset sessions for associated sockets
		void logout(const std::string& aSessionToken) noexcept;
		WebSocketPtr getSocket(const std::string& aSessionToken) noexcept;

		bool load() noexcept;
		bool save(std::function<void(const string&)> aCustomErrorF = nullptr) noexcept;

		WebUserManager& getUserManager() noexcept {
			return *userManager.get();
		}

		bool hasValidConfig() const noexcept;

		void join() {
			worker_threads.join_all();
		}

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
	private:
		bool listen(ErrorF& errorF);

		bool InitializeIO(ErrorF& errorF);

		ServerConfig plainServerConfig;
		ServerConfig tlsServerConfig;

		void loadServer(SimpleXML& xml_, const string& aTagName, ServerConfig& config_) noexcept;

		mutable SharedMutex cs;

		// set up an external io_service to run both endpoints on. This is not
		// strictly necessary, but simplifies thread management a bit.
		boost::asio::io_service ios;

		context_ptr on_tls_init(websocketpp::connection_hdl hdl);

		std::map<websocketpp::connection_hdl, WebSocketPtr, std::owner_less<websocketpp::connection_hdl>> sockets;

		ApiRouter api;
		FileServer fileServer;

		unique_ptr<WebUserManager> userManager;

		bool has_io_service;

		server_plain endpoint_plain;
		server_tls endpoint_tls;
		boost::thread_group worker_threads;
	};
}

#endif // DCPLUSPLUS_DCPP_WEBSERVER_H
