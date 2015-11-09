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

#include <web-server/stdinc.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/format.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/SimpleXML.h>

#define CONFIG_NAME "WebServer.xml"
#define CONFIG_DIR Util::PATH_USER_CONFIG

#define HANDSHAKE_TIMEOUT 0 // disabled, affects HTTP downloads

namespace webserver {
	using namespace dcpp;
	WebServerManager::WebServerManager() : has_io_service(false), ios(2) {
		userManager = unique_ptr<WebUserManager>(new WebUserManager(this));
	}

	WebServerManager::~WebServerManager() {
		// Let it remove the listener
		userManager.reset();
	}

	string WebServerManager::getConfigPath() const noexcept {
		return Util::getPath(CONFIG_DIR) + CONFIG_NAME;
	}

	bool WebServerManager::isRunning() const noexcept {
		return !ios.stopped();
	}

#if defined _MSC_VER && defined _DEBUG
	class DebugOutputStream : public std::ostream {
	public:
		DebugOutputStream() : std::ostream(&buf) {}
	private:
		class dbgview_buffer : public std::stringbuf {
		public:
			~dbgview_buffer() {
				sync(); // can be avoided
			}

			int sync() {
				//WLock l(cs);
				OutputDebugString(Text::toT(str()).c_str());
				str("");
				return 0;
			}

			//SharedMutex cs;
		};

		dbgview_buffer buf;
	};

	DebugOutputStream debugStreamPlain;
	DebugOutputStream debugStreamTls;
#else
#define debugStreamPlain std::cout
#define debugStreamTls std::cout
#endif

	template<class T>
	void setEndpointLogSettings(T& aEndpoint, std::ostream& aStream) {
		aEndpoint.set_access_channels(websocketpp::log::alevel::all);
		aEndpoint.clear_access_channels(websocketpp::log::alevel::frame_payload);

		aEndpoint.set_access_channels(websocketpp::log::elevel::all);
		aEndpoint.get_elog().set_ostream(&aStream);
	}

	bool WebServerManager::start(ErrorF errorF, const string& aWebResourcePath) {
		SettingsManager::getInstance()->setDefault(SettingsManager::PM_MESSAGE_CACHE, 200);
		SettingsManager::getInstance()->setDefault(SettingsManager::HUB_MESSAGE_CACHE, 200);
		{
			auto resourcePath = aWebResourcePath;
			if (resourcePath.empty()) {
				resourcePath = Util::getPath(Util::PATH_RESOURCES) + "web-resources" + PATH_SEPARATOR;
			}

			fileServer.setResourcePath(resourcePath);
		}

		ios.reset();
		if (!has_io_service) {
			has_io_service = InitializeIO(errorF);
		}
		// Logging
		setEndpointLogSettings(endpoint_plain, debugStreamPlain);
		setEndpointLogSettings(endpoint_tls, debugStreamTls);

		return listen(errorF);
	}

	bool WebServerManager::InitializeIO(ErrorF& errorF) {
		try {
			// initialize asio with our external io_service rather than an internal one
			endpoint_plain.init_asio(&ios);

			endpoint_plain.set_http_handler(
				std::bind(&WebServerManager::on_http<server_plain>, this, &endpoint_plain, _1, false));
			endpoint_plain.set_message_handler(
				std::bind(&WebServerManager::on_message<server_plain>, this, &endpoint_plain, _1, _2, false));
			endpoint_plain.set_close_handler(std::bind(&WebServerManager::on_close_socket, this, _1));
			endpoint_plain.set_open_handler(std::bind(&WebServerManager::on_open_socket<server_plain>, this, &endpoint_plain, _1, false));

			// Failures (plain)
			endpoint_plain.set_open_handshake_timeout(HANDSHAKE_TIMEOUT);

			// set up tls endpoint
			endpoint_tls.init_asio(&ios);
			endpoint_tls.set_message_handler(
				std::bind(&WebServerManager::on_message<server_tls>, this, &endpoint_tls, _1, _2, true));

			endpoint_tls.set_close_handler(std::bind(&WebServerManager::on_close_socket, this, _1));
			endpoint_tls.set_open_handler(std::bind(&WebServerManager::on_open_socket<server_tls>, this, &endpoint_tls, _1, true));
			endpoint_tls.set_http_handler(std::bind(&WebServerManager::on_http<server_tls>, this, &endpoint_tls, _1, true));

			// Failures (TLS)
			endpoint_tls.set_open_handshake_timeout(HANDSHAKE_TIMEOUT);

			// TLS endpoint has an extra handler for the tls init
			endpoint_tls.set_tls_init_handler(std::bind(&WebServerManager::on_tls_init, this, _1));

		}
		catch (const std::exception& e) {
			errorF(e.what());
			return false;
		}
		return true;
	}

	bool WebServerManager::listen(ErrorF& errorF) {
		bool hasServer = false;
		if (plainServerConfig.hasValidConfig()) {
			try {
				endpoint_plain.listen(plainServerConfig.getPort());
				endpoint_plain.start_accept();
				hasServer = true;
			} catch (const websocketpp::exception& e) {
				auto message = boost::format("Failed to set up plain server on port %1%: %2% (is the port in use by another application?)") % plainServerConfig.getPort() % string(e.what());
				errorF(message.str());
			}
		}

		if (tlsServerConfig.hasValidConfig()) {
			try {
				endpoint_tls.listen(tlsServerConfig.getPort());
				endpoint_tls.start_accept();
				hasServer = true;
			} catch (const websocketpp::exception& e) {
				auto message = boost::format("Failed to set up secure server on port %1%: %2% (is the port in use by another application?)") % tlsServerConfig.getPort() % string(e.what());
				errorF(message.str());
			}
		}

		if (hasServer) {
			// Start the ASIO io_service run loop running both endpoints
			for (int x = 0; x < 2; ++x) {
				worker_threads.create_thread(boost::bind(&boost::asio::io_service::run, &ios));
			}
		}

		fire(WebServerManagerListener::Started());
		return hasServer;
	}

	context_ptr WebServerManager::on_tls_init(websocketpp::connection_hdl hdl) {
		//std::cout << "on_tls_init called with hdl: " << hdl.lock().get() << std::endl;
		context_ptr ctx(new boost::asio::ssl::context(boost::asio::ssl::context::tlsv12));

		try {
			ctx->set_options(boost::asio::ssl::context::default_workarounds |
				boost::asio::ssl::context::no_sslv2 |
				boost::asio::ssl::context::no_sslv3 |
				boost::asio::ssl::context::single_dh_use);

			ctx->use_certificate_file(SETTING(TLS_CERTIFICATE_FILE), boost::asio::ssl::context::pem);
			ctx->use_private_key_file(SETTING(TLS_PRIVATE_KEY_FILE), boost::asio::ssl::context::pem);
		} catch (std::exception& e) {
			//std::cout << e.what() << std::endl;
			dcdebug("TLS init failed: %s", e.what());
		}

		return ctx;
	}

	void WebServerManager::disconnectSockets(const string& aMessage) noexcept {
		RLock l(cs);
		for (const auto& socket : sockets | map_values) {
			socket->close(websocketpp::close::status::going_away, aMessage);
		}
	}

	void WebServerManager::stop() {
		fire(WebServerManagerListener::Stopping());

		if(endpoint_plain.is_listening())
			endpoint_plain.stop_listening();
		if(endpoint_tls.is_listening())
			endpoint_tls.stop_listening();

		disconnectSockets("Shutting down");

		bool hasSockets = false;

		for (;;) {
			{
				RLock l(cs);
				hasSockets = !sockets.empty();
			}

			if (hasSockets) {
				Thread::sleep(50);
			} else {
				break;
			}
		}

		ios.stop();

		worker_threads.join_all();

		fire(WebServerManagerListener::Stopped());
	}

	void WebServerManager::logout(const string& aSessionToken) noexcept {
		vector<WebSocketPtr> sessionSockets;

		{
			RLock l(cs);
			boost::algorithm::copy_if(sockets | map_values, back_inserter(sessionSockets),
				[&](const WebSocketPtr& aSocket) {
				return aSocket->getSession() && aSocket->getSession()->getToken() == aSessionToken;
			}
			);
		}

		for (const auto& s : sessionSockets) {
			s->getSession()->onSocketDisconnected();
			s->setSession(nullptr);
		}
	}

	WebSocketPtr WebServerManager::getSocket(const std::string& aSessionToken) noexcept {
		RLock l(cs);
		auto i = find_if(sockets | map_values, [&](const WebSocketPtr& s) {
			return s->getSession() && s->getSession()->getToken() == aSessionToken;
		});

		return i.base() == sockets.end() ? nullptr : *i;
	}

	TimerPtr WebServerManager::addTimer(CallBack&& aCallBack, time_t aIntervalMillis) noexcept {
		return make_shared<Timer>(move(aCallBack), ios, aIntervalMillis);
	}

	void WebServerManager::addAsyncTask(CallBack&& aCallBack) noexcept {
		ios.post(aCallBack);
	}

	void WebServerManager::on_close_socket(websocketpp::connection_hdl hdl) {
		WebSocketPtr socket = nullptr;

		{
			WLock l(cs);
			auto s = sockets.find(hdl);
			dcassert(s != sockets.end());
			if (s == sockets.end()) {
				return;
			}

			socket = s->second;
			sockets.erase(s);
		}

		if (socket->getSession()) {
			socket->getSession()->onSocketDisconnected();
		}
	}

	bool WebServerManager::hasValidConfig() const noexcept {
		return (plainServerConfig.hasValidConfig() || tlsServerConfig.hasValidConfig()) && userManager->hasUsers();
	}

	bool WebServerManager::load() noexcept {
		try {
			SimpleXML xml;
			SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_NAME, true);
			if (xml.findChild("WebServer")) {
				xml.stepIn();

				if (xml.findChild("Config")) {
					xml.stepIn();
					loadServer(xml, "Server", plainServerConfig);
					loadServer(xml, "TLSServer", tlsServerConfig);
					xml.stepOut();
				}

				fire(WebServerManagerListener::LoadSettings(), xml);

				xml.stepOut();
			}
		} catch (const Exception& e) {
			LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_NAME % e.getError()), LogMessage::SEV_ERROR);
		}

		return hasValidConfig();
	}

	void WebServerManager::loadServer(SimpleXML& aXml, const string& aTagName, ServerConfig& config_) noexcept {
		if (aXml.findChild(aTagName)) {
			config_.setPort(aXml.getIntChildAttrib("Port"));
			aXml.resetCurrentChild();
		}
	}

	bool WebServerManager::save(std::function<void(const string&)> aCustomErrorF) noexcept {
		SimpleXML xml;

		xml.addTag("WebServer");
		xml.stepIn();

		{
			xml.addTag("Config");
			xml.stepIn();
			plainServerConfig.save(xml, "Server");
			tlsServerConfig.save(xml, "TLSServer");
			xml.stepOut();
		}

		fire(WebServerManagerListener::SaveSettings(), xml);

		xml.stepOut();

		auto errorF = aCustomErrorF;
		if (!errorF) {
			// Avoid crashes if the file is saved when core is not loaded
			errorF = [](const string&) {};
		}

		return SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME, errorF);
	}

	bool ServerConfig::hasValidConfig() const noexcept {
		return port > 0;
	}

	void ServerConfig::save(SimpleXML& xml_, const string& aTagName) noexcept {
		if (!hasValidConfig()) {
			return;
		}

		xml_.addTag(aTagName);
		xml_.addChildAttrib("Port", port);
	}
}