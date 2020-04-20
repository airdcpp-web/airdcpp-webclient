/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include "stdinc.h"
#include <api/ApiSettingItem.h>

#include <web-server/ExtensionManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebUserManager.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/CryptoManager.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/TimerManager.h>

#define CONFIG_NAME "WebServer.xml"
#define CONFIG_DIR Util::PATH_USER_CONFIG

#define AUTHENTICATION_TIMEOUT 60 // seconds

#define HANDSHAKE_TIMEOUT 0 // disabled, affects HTTP downloads

namespace webserver {
	using namespace dcpp;
	WebServerManager::WebServerManager() : 
		ios(settings.getValue(WebServerSettings::SERVER_THREADS).getDefaultValue()),
		tasks(settings.getValue(WebServerSettings::SERVER_THREADS).getDefaultValue()),
		work(tasks),
		plainServerConfig(settings.getValue(WebServerSettings::PLAIN_PORT), settings.getValue(WebServerSettings::PLAIN_BIND)),
		tlsServerConfig(settings.getValue(WebServerSettings::TLS_PORT), settings.getValue(WebServerSettings::TLS_BIND))
	{

		fileServer.setResourcePath(Util::getPath(Util::PATH_RESOURCES) + "web-resources" + PATH_SEPARATOR);

		extManager = make_unique<ExtensionManager>(this);
		userManager = make_unique<WebUserManager>(this);

		// Prevent io service from running until we load
		ios.stop();
		tasks.stop();
	}

	WebServerManager::~WebServerManager() {
		// Let them remove the listeners
		extManager.reset();
		userManager.reset();
	}

	string WebServerManager::getConfigPath() const noexcept {
		return Util::getPath(CONFIG_DIR) + CONFIG_NAME;
	}

	bool WebServerManager::isRunning() const noexcept {
		return !ios.stopped() || !tasks.stopped();
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
				OutputDebugString(Text::toT(str()).c_str());
				str("");
				return 0;
			}
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
		// Access
		aEndpoint.set_access_channels(websocketpp::log::alevel::all);
		aEndpoint.clear_access_channels(websocketpp::log::alevel::frame_payload | websocketpp::log::alevel::frame_header | websocketpp::log::alevel::control);
		aEndpoint.get_alog().set_ostream(&aStream);

		// Errors
		aEndpoint.set_error_channels(websocketpp::log::elevel::all);
		aEndpoint.get_elog().set_ostream(&aStream);
	}

	template<class T>
	void setEndpointHandlers(T& aEndpoint, bool aIsSecure, WebServerManager* aServer) {
		aEndpoint.set_http_handler(
			std::bind(&WebServerManager::handleHttpRequest<T>, aServer, &aEndpoint, _1, aIsSecure));
		aEndpoint.set_message_handler(
			std::bind(&WebServerManager::handleSocketMessage<T>, aServer, &aEndpoint, _1, _2, aIsSecure));

		aEndpoint.set_close_handler(std::bind(&WebServerManager::handleSocketDisconnected, aServer, _1));
		aEndpoint.set_open_handler(std::bind(&WebServerManager::handleSocketConnected<T>, aServer, &aEndpoint, _1, aIsSecure));

		aEndpoint.set_open_handshake_timeout(HANDSHAKE_TIMEOUT);

		aEndpoint.set_pong_timeout(WEBCFG(PING_TIMEOUT).num() * 1000);
		aEndpoint.set_pong_timeout_handler(std::bind(&WebServerManager::handlePongTimeout, aServer, _1, _2));

		// Workaround for https://github.com/zaphoyd/websocketpp/issues/549
		aEndpoint.set_listen_backlog(boost::asio::socket_base::max_connections);
	}

	bool WebServerManager::startup(const ErrorF& errorF, const string& aWebResourcePath, const CallBack& aShutdownF) {
		if (!aWebResourcePath.empty()) {
			fileServer.setResourcePath(aWebResourcePath);
		}

		shutdownF = aShutdownF;
		return start(errorF);
	}

	bool WebServerManager::start(const ErrorF& errorF) {
		if (!hasValidConfig()) {
			return false;
		}

		ios.reset();
		tasks.reset();
		if (!has_io_service) {
			has_io_service = initialize(errorF);
		}

		return listen(errorF);
	}

	bool WebServerManager::initialize(const ErrorF& errorF) {
		SettingsManager::getInstance()->setDefault(SettingsManager::PM_MESSAGE_CACHE, 100);
		SettingsManager::getInstance()->setDefault(SettingsManager::HUB_MESSAGE_CACHE, 100);

		try {
			// initialize asio with our external io_service rather than an internal one
			endpoint_plain.init_asio(&ios);
			endpoint_tls.init_asio(&ios);

			//endpoint_plain.set_pong_handler(std::bind(&WebServerManager::onPongReceived, this, _1, _2));
		} catch (const std::exception& e) {
			if (errorF) {
				errorF(e.what());
			}

			return false;
		}

		// Handlers
		setEndpointHandlers(endpoint_plain, false, this);
		setEndpointHandlers(endpoint_tls, true, this);

		// TLS endpoint has an extra handler for the tls init
		endpoint_tls.set_tls_init_handler(std::bind(&WebServerManager::handleInitTls, this, _1));

		// Logging
		setEndpointLogSettings(endpoint_plain, debugStreamPlain);
		setEndpointLogSettings(endpoint_tls, debugStreamTls);

		return true;
	}

	boost::asio::ip::tcp WebServerManager::getDefaultListenProtocol() noexcept {
		auto v6Supported = !AirUtil::getLocalIp(true).empty();
		return v6Supported ? boost::asio::ip::tcp::v6() : boost::asio::ip::tcp::v4();
	}

	bool WebServerManager::isListeningPlain() const noexcept {
		return endpoint_plain.is_listening();
	}

	bool WebServerManager::isListeningTls() const noexcept {
		return endpoint_tls.is_listening();
	}

	template <typename EndpointType>
	bool listenEndpoint(EndpointType& aEndpoint, const ServerConfig& aConfig, const string& aProtocol, const WebServerManager::ErrorF& errorF) noexcept {
		if (!aConfig.hasValidConfig()) {
			return false;
		}

		aEndpoint.set_reuse_addr(true);
		try {
			const auto bindAddress = aConfig.bindAddress.str();
			if (!bindAddress.empty()) {
				aEndpoint.listen(bindAddress, aConfig.port.str());
			} else {
				// IPv6 and IPv4-mapped IPv6 addresses are used by default (given that IPv6 is supported by the OS)
				aEndpoint.listen(WebServerManager::getDefaultListenProtocol(), static_cast<uint16_t>(aConfig.port.num()));
			}

			aEndpoint.start_accept();
			return true;
		} catch (const std::exception& e) {
			auto message = STRING_F(WEB_SERVER_SETUP_FAILED, aProtocol % aConfig.port.num() % string(e.what()));
			if (errorF) {
				errorF(message);
			}
		}

		return false;
	}

	bool WebServerManager::listen(const ErrorF& errorF) {
		bool hasServer = false;

		if (listenEndpoint(endpoint_plain, plainServerConfig, "HTTP", errorF)) {
			hasServer = true;
		}

		if (listenEndpoint(endpoint_tls, tlsServerConfig, "HTTPS", errorF)) {
			hasServer = true;
		}

		if (!hasServer) {
			return false;
		}

		ios_threads = make_unique<boost::thread_group>();
		task_threads = make_unique<boost::thread_group>();

		// Start the ASIO io_service run loop running both endpoints
		for (int x = 0; x < WEBCFG(SERVER_THREADS).num(); ++x) {
			ios_threads->create_thread(boost::bind(&boost::asio::io_service::run, &ios));
		}

		for (int x = 0; x < std::max(WEBCFG(SERVER_THREADS).num() / 2, 1); ++x) {
			task_threads->create_thread(boost::bind(&boost::asio::io_service::run, &tasks));
		}

		// Add timers
		{
			const auto logger = getDefaultErrorLogger();
			minuteTimer = addTimer(
				[this, logger] {
					save(logger);
				},
				30 * 1000
			);

			socketPingTimer = addTimer(
				[this] {
					pingTimer();
				},
				WEBCFG(PING_INTERVAL).num() * 1000
			);

			minuteTimer->start(false);
			socketPingTimer->start(false);
		}

		fire(WebServerManagerListener::Started());
		return true;
	}

	WebSocketPtr WebServerManager::getSocket(websocketpp::connection_hdl hdl) const noexcept {
		RLock l(cs);
		auto s = sockets.find(hdl);
		if (s != sockets.end()) {
			return s->second;
		}

		return nullptr;
	}

	void WebServerManager::onData(const string& aData, TransportType aType, Direction aDirection, const string& aIP) noexcept {
		// Avoid possible deadlocks due to possible simultaneous disconnected/server state listener events
		addAsyncTask([=] {
			fire(WebServerManagerListener::Data(), aData, aType, aDirection, aIP);
		});
	}

	// For debugging only
	void WebServerManager::handlePongReceived(websocketpp::connection_hdl hdl, const string& /*aPayload*/) {
		auto socket = getSocket(hdl);
		if (!socket) {
			return;
		}

		socket->debugMessage("PONG succeed");
	}

	void WebServerManager::handlePongTimeout(websocketpp::connection_hdl hdl, const string&) {
		auto socket = getSocket(hdl);
		if (!socket) {
			return;
		}

		socket->debugMessage("PONG timed out");

		socket->close(websocketpp::close::status::internal_endpoint_error, "PONG timed out");
	}

	void WebServerManager::pingTimer() noexcept {
		vector<WebSocketPtr> inactiveSockets;
		auto tick = GET_TICK();

		{
			RLock l(cs);
			for (const auto& socket : sockets | map_values) {
				//socket->debugMessage("PING");
				socket->ping();

				// Disconnect sockets without a session after one minute
				if (!socket->getSession() && socket->getTimeCreated() + AUTHENTICATION_TIMEOUT * 1000ULL < tick) {
					inactiveSockets.push_back(socket);
				}
			}
		}

		for (const auto& s : inactiveSockets) {
			s->close(websocketpp::close::status::policy_violation, "Authentication timeout");
		}
	}

	context_ptr WebServerManager::handleInitTls(websocketpp::connection_hdl hdl) {
		//std::cout << "on_tls_init called with hdl: " << hdl.lock().get() << std::endl;
		context_ptr ctx(new boost::asio::ssl::context(boost::asio::ssl::context::tls));

		try {
			ctx->set_options(boost::asio::ssl::context::default_workarounds |
				boost::asio::ssl::context::no_sslv2 |
				boost::asio::ssl::context::no_sslv3 |
				boost::asio::ssl::context::no_tlsv1 |
				boost::asio::ssl::context::no_tlsv1_1 |
				boost::asio::ssl::context::single_dh_use |
				boost::asio::ssl::context::no_compression
			);

			const auto customCert = WEBCFG(TLS_CERT_PATH).str();
			const auto customKey = WEBCFG(TLS_CERT_KEY_PATH).str();

			bool useCustom = !customCert.empty() && !customKey.empty();

			ctx->use_certificate_file(useCustom ? customCert : SETTING(TLS_CERTIFICATE_FILE), boost::asio::ssl::context::pem);
			ctx->use_private_key_file(useCustom ? customKey : SETTING(TLS_PRIVATE_KEY_FILE), boost::asio::ssl::context::pem);

			CryptoManager::setContextOptions(ctx->native_handle(), true);
		} catch (std::exception& e) {
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
		fileServer.stop();

		if (minuteTimer)
			minuteTimer->stop(true);
		if (socketPingTimer)
			socketPingTimer->stop(true);

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
		tasks.stop();

		if (task_threads)
			task_threads->join_all();

		if (ios_threads)
			ios_threads->join_all();

		task_threads.reset();
		ios_threads.reset();

		fire(WebServerManagerListener::Stopped());
	}

	WebSocketPtr WebServerManager::getSocket(LocalSessionId aSessionToken) noexcept {
		RLock l(cs);
		auto i = find_if(sockets | map_values, [&](const WebSocketPtr& s) {
			return s->getSession() && s->getSession()->getId() == aSessionToken;
		});

		return i.base() == sockets.end() ? nullptr : *i;
	}

	TimerPtr WebServerManager::addTimer(CallBack&& aCallBack, time_t aIntervalMillis, const Timer::CallbackWrapper& aCallbackWrapper) noexcept {
		return make_shared<Timer>(move(aCallBack), tasks, aIntervalMillis, aCallbackWrapper);
	}

	void WebServerManager::addAsyncTask(CallBack&& aCallBack) noexcept {
		tasks.post(aCallBack);
	}

	void WebServerManager::setDirty() noexcept {
		isDirty = true;
	}

	void WebServerManager::addSocket(websocketpp::connection_hdl hdl, const WebSocketPtr& aSocket) noexcept {
		{
			WLock l(cs);
			sockets.emplace(hdl, aSocket);
		}

		fire(WebServerManagerListener::SocketConnected(), aSocket);
	}

	void WebServerManager::handleSocketDisconnected(websocketpp::connection_hdl hdl) {
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

		dcdebug("Socket disconnected: %s\n", socket->getSession() ? socket->getSession()->getAuthToken().c_str() : "(no session)");
		fire(WebServerManagerListener::SocketDisconnected(), socket);
	}

	void WebServerManager::log(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
		if (!LogManager::getInstance()) {
			// Core is not initialized yet
			return;
		}

		LogManager::getInstance()->message(aMsg, aSeverity);
	}

	WebServerManager::ErrorF WebServerManager::getDefaultErrorLogger() const noexcept {
		return [this](const string& aMessage) {
			log(aMessage, LogMessage::SEV_ERROR);
		};
	}

	string WebServerManager::resolveAddress(const string& aHostname, const string& aPort) noexcept {
		auto ret = aHostname;

		boost::asio::ip::tcp::resolver resolver(ios);
		boost::asio::ip::tcp::resolver::query query(aHostname, aPort);

		try {
			boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
			ret = iter->endpoint().address().to_string();

			if (iter->endpoint().protocol() == boost::asio::ip::tcp::v6()) {
				ret = "[" + ret + "]";
			}
		} catch (const std::exception& e) {
			log(e.what(), LogMessage::SEV_ERROR);
		}

		return ret;
	}

	bool WebServerManager::hasValidConfig() const noexcept {
		return (plainServerConfig.hasValidConfig() || tlsServerConfig.hasValidConfig()) && userManager->hasUsers();
	}

	bool WebServerManager::load(const ErrorF& aErrorF) noexcept {
		SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_NAME, [this](SimpleXML& xml) {
			if (xml.findChild("WebServer")) {
				xml.stepIn();

				if (xml.findChild("Config")) {
					xml.stepIn();
					loadServer(xml, "Server", plainServerConfig, false);
					loadServer(xml, "TLSServer", tlsServerConfig, true);

					if (xml.findChild("Threads")) {
						xml.stepIn();
						WEBCFG(SERVER_THREADS).setValue(max(Util::toInt(xml.getData()), 1));
						xml.stepOut();
					}
					xml.resetCurrentChild();

					if (xml.findChild("ExtensionsDebugMode")) {
						xml.stepIn();
						WEBCFG(EXTENSIONS_DEBUG_MODE).setValue(Util::toInt(xml.getData()) > 0 ? true : false);
						xml.stepOut();
					}
					xml.resetCurrentChild();

					xml.stepOut();
				}

				fire(WebServerManagerListener::LoadSettings(), xml);

				xml.stepOut();
			}
		}, aErrorF);

		return hasValidConfig();
	}

	void WebServerManager::loadServer(SimpleXML& aXml, const string& aTagName, ServerConfig& config_, bool aTls) noexcept {
		if (aXml.findChild(aTagName)) {
			config_.port.setValue(aXml.getIntChildAttrib("Port"));
			config_.bindAddress.setValue(aXml.getChildAttrib("BindAddress"));

			if (aTls) {
				WEBCFG(TLS_CERT_PATH).setValue(aXml.getChildAttrib("Certificate"));
				WEBCFG(TLS_CERT_KEY_PATH).setValue(aXml.getChildAttrib("CertificateKey"));
			}
		}

		aXml.resetCurrentChild();
	}

	bool WebServerManager::save(const ErrorF& aCustomErrorF) noexcept {
		{
			if (!isDirty) {
				return false;
			}

			isDirty = false;
		}

		SimpleXML xml;

		xml.addTag("WebServer");
		xml.stepIn();

		{
			xml.addTag("Config");
			xml.stepIn();

			plainServerConfig.save(xml, "Server");

			tlsServerConfig.save(xml, "TLSServer");
			xml.addChildAttrib("Certificate", WEBCFG(TLS_CERT_PATH).str());
			xml.addChildAttrib("CertificateKey", WEBCFG(TLS_CERT_KEY_PATH).str());

			if (!WEBCFG(SERVER_THREADS).isDefault()) {
				xml.addTag("Threads");
				xml.stepIn();

				xml.setData(Util::toString(WEBCFG(SERVER_THREADS).num()));

				xml.stepOut();
			}

			if (!WEBCFG(EXTENSIONS_DEBUG_MODE).isDefault()) {
				xml.addTag("ExtensionsDebugMode");
				xml.stepIn();
				xml.setData(Util::toString(WEBCFG(EXTENSIONS_DEBUG_MODE).boolean()));
				xml.stepOut();
			}

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
		return port.num() > 0;
	}

	void ServerConfig::save(SimpleXML& xml_, const string& aTagName) noexcept {
		xml_.addTag(aTagName);
		xml_.addChildAttrib("Port", port.num());

		if (!bindAddress.str().empty()) {
			xml_.addChildAttrib("BindAddress", bindAddress.str());
		}
	}
}