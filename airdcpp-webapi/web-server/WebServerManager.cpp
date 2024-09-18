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

#include "stdinc.h"

#include <web-server/ApiSettingItem.h>
#include <web-server/ContextMenuManager.h>
#include <web-server/ExtensionManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebUserManager.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/CryptoManager.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/NetworkUtil.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/SystemUtil.h>
#include <airdcpp/TimerManager.h>

#define LEGACY_CONFIG_NAME_XML "WebServer.xml"
#define CONFIG_DIR AppUtil::PATH_USER_CONFIG

#define AUTHENTICATION_TIMEOUT 60 // seconds

#define HANDSHAKE_TIMEOUT 0 // disabled, affects HTTP downloads

namespace webserver {
	using namespace dcpp;
	WebServerManager::WebServerManager() : 
		ios(4),
		tasks(4),
		work(tasks)
	{

		fileServer.setResourcePath(AppUtil::getPath(AppUtil::PATH_RESOURCES) + "web-resources" + PATH_SEPARATOR);

		settingsManager = make_unique<WebServerSettings>(this);
		extManager = make_unique<ExtensionManager>(this);
		userManager = make_unique<WebUserManager>(this);
		contextMenuManager = make_unique<ContextMenuManager>();

		plainServerConfig = make_unique<ServerConfig>(settingsManager->getSettingItem(WebServerSettings::PLAIN_PORT), settingsManager->getSettingItem(WebServerSettings::PLAIN_BIND));
		tlsServerConfig = make_unique<ServerConfig>(settingsManager->getSettingItem(WebServerSettings::TLS_PORT), settingsManager->getSettingItem(WebServerSettings::TLS_BIND));

		// Prevent io service from running until we load
		ios.stop();
		tasks.stop();
	}

	WebServerManager::~WebServerManager() {
		// Let them remove the listeners
		extManager.reset();
		userManager.reset();
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
			~dbgview_buffer() override {
				sync(); // can be avoided
			}

			int sync() override {
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
	void disableEndpointLogging(T& aEndpoint) {
		aEndpoint.clear_access_channels(websocketpp::log::alevel::all);
		aEndpoint.clear_error_channels(websocketpp::log::elevel::all);
	}


	template<class T>
	void setEndpointOptions(T& aEndpoint) {
		aEndpoint.set_open_handshake_timeout(HANDSHAKE_TIMEOUT);
		aEndpoint.set_pong_timeout(WEBCFG(PING_TIMEOUT).num() * 1000);

		// Workaround for https://github.com/zaphoyd/websocketpp/issues/549
		aEndpoint.set_listen_backlog(boost::asio::socket_base::max_connections);
	}

	bool WebServerManager::startup(const MessageCallback& errorF, const string& aWebResourcePath, const Callback& aShutdownF) {
		if (!aWebResourcePath.empty()) {
			fileServer.setResourcePath(aWebResourcePath);
		}

		shutdownF = aShutdownF;
		return start(errorF);
	}

	bool WebServerManager::start(const MessageCallback& errorF) {
		if (!hasValidServerConfig()) {
			return false;
		}

		ios.reset();
		tasks.reset();
		if (!has_io_service) {
			has_io_service = initialize(errorF);
		}

		if (!listen(errorF)) {
			// Stop possible running ios services
			stop();
			return false;
		}

		return true;
	}

	bool WebServerManager::initialize(const MessageCallback& errorF) {
		SettingsManager::getInstance()->setDefault(SettingsManager::PM_MESSAGE_CACHE, 100);
		SettingsManager::getInstance()->setDefault(SettingsManager::HUB_MESSAGE_CACHE, 100);

		try {
			// initialize asio with our external io_service rather than an internal one
			endpoint_plain.init_asio(&ios);
			endpoint_tls.init_asio(&ios);

			//endpoint_plain.set_pong_handler(std::bind(&WebServerManager::onPongReceived, this, _1, _2));
		} catch (const websocketpp::exception& e) {
			if (errorF) {
				errorF(e.what());
			}

			return false;
		}

		// Handlers
		setEndpointHandlers(endpoint_plain, false, this);
		setEndpointHandlers(endpoint_tls, true, this);

		// Misc options
		setEndpointOptions(endpoint_plain);
		setEndpointOptions(endpoint_tls);

		// TLS endpoint has an extra handler for the tls init
		endpoint_tls.set_tls_init_handler(std::bind_front(&WebServerManager::handleInitTls, this));

		// Logging
		if (enableSocketLogging) {
			setEndpointLogSettings(endpoint_plain, debugStreamPlain);
			setEndpointLogSettings(endpoint_tls, debugStreamTls);
		} else {
			disableEndpointLogging(endpoint_plain);
			disableEndpointLogging(endpoint_tls);
		}

		return true;
	}

	boost::asio::ip::tcp WebServerManager::getDefaultListenProtocol() noexcept {
		auto v6Supported = !NetworkUtil::getLocalIp(true).empty();
		return v6Supported ? boost::asio::ip::tcp::v6() : boost::asio::ip::tcp::v4();
	}

	bool WebServerManager::isListeningPlain() const noexcept {
		return endpoint_plain.is_listening();
	}

	bool WebServerManager::isListeningTls() const noexcept {
		return endpoint_tls.is_listening();
	}

	template <typename EndpointType>
	bool listenEndpoint(EndpointType& aEndpoint, const ServerConfig& aConfig, const string& aProtocol, const MessageCallback& errorF) noexcept {
		if (!aConfig.hasValidConfig()) {
			return false;
		}

		// Keep reuse disabled on Windows to avoid hiding errors when multiple instances are being run with the same ports
#ifndef _WIN32
		// https://github.com/airdcpp-web/airdcpp-webclient/issues/39
		aEndpoint.set_reuse_addr(true);
#endif
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

	bool WebServerManager::listen(const MessageCallback& errorF) {
		bool hasServer = false;

		if (listenEndpoint(endpoint_plain, *plainServerConfig, "HTTP", errorF)) {
			hasServer = true;
		}

		if (listenEndpoint(endpoint_tls, *tlsServerConfig, "HTTPS", errorF)) {
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
		addAsyncTask([=, this] {
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

		if (socket->getSession() && socket->getSession()->getSessionType() == Session::SessionType::TYPE_EXTENSION && WEBCFG(EXTENSIONS_DEBUG_MODE).boolean()) {
			log("Disconnecting extension " + socket->getSession()->getUser()->getUserName() + " because of ping timeout", LogMessage::SEV_INFO);
		}

		socket->debugMessage("PONG timed out");

		socket->close(websocketpp::close::status::internal_endpoint_error, "PONG timed out");
	}

	void WebServerManager::pingTimer() noexcept {
		vector<WebSocketPtr> inactiveSockets;
		auto tick = GET_TICK();

		{
			RLock l(cs);
			for (const auto& socket : sockets | views::values) {
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
		auto ctx = make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls);

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
		for (const auto& socket : sockets | views::values) {
			socket->close(websocketpp::close::status::going_away, aMessage);
		}
	}

	void WebServerManager::stop() noexcept {
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

		disconnectSockets(STRING(WEB_SERVER_SHUTTING_DOWN));

		for (;;) {
			{
				RLock l(cs);
				if (sockets.empty()) {
					break;
				}
			}

			Thread::sleep(50);
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
		auto i = ranges::find_if(sockets | views::values, [&](const WebSocketPtr& s) {
			return s->getSession() && s->getSession()->getId() == aSessionToken;
		});

		return i.base() == sockets.end() ? nullptr : *i;
	}

	TimerPtr WebServerManager::addTimer(Callback&& aCallback, time_t aIntervalMillis, const Timer::CallbackWrapper& aCallbackWrapper) noexcept {
		return make_shared<Timer>(std::move(aCallback), tasks, aIntervalMillis, aCallbackWrapper);
	}

	void WebServerManager::addAsyncTask(Callback&& aCallback) noexcept {
		tasks.post(std::move(aCallback));
	}

	void WebServerManager::addSocket(websocketpp::connection_hdl hdl, const WebSocketPtr& aSocket) noexcept {
		{
			WLock l(cs);
			sockets.emplace(hdl, aSocket);
		}

		fire(WebServerManagerListener::SocketConnected(), aSocket);
	}

	void WebServerManager::handleSocketDisconnected(websocketpp::connection_hdl hdl) {
		WebSocketPtr socket = getSocket(hdl);
		if (!socket) {
			dcassert(0);
			return;
		}

		// Process all listener events before removing the socket from the list to avoid issues on shutdown
		dcdebug("Socket disconnected: %s\n", socket->getSession() ? socket->getSession()->getAuthToken().c_str() : "(no session)");
		fire(WebServerManagerListener::SocketDisconnected(), socket);

		{
			WLock l(cs);
			auto s = sockets.find(hdl);
			dcassert(s != sockets.end());
			if (s == sockets.end()) {
				return;
			}

			sockets.erase(s);
		}

		dcassert(socket.use_count() == 1);
	}

	void WebServerManager::log(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
		if (!LogManager::getInstance()) {
			// Core is not initialized yet
			return;
		}

		LogManager::getInstance()->message(aMsg, aSeverity, STRING(WEB_SERVER));
	}

	MessageCallback WebServerManager::getDefaultErrorLogger() const noexcept {
		return [this](const string& aMessage) {
			log(aMessage, LogMessage::SEV_ERROR);
		};
	}

	string WebServerManager::getLocalServerHttpUrl() noexcept {
		bool isPlain = isListeningPlain();
		const auto& config = isPlain ? plainServerConfig : tlsServerConfig;
		return (isPlain ? "http://" : "https://") + getLocalServerAddress(*config);
	}

	bool WebServerManager::isAnyAddress(const string& aAddress) noexcept {
		if (aAddress.empty()) {
			return true;
		}

		try {
#if BOOST_VERSION >= 106600
			auto ip = boost::asio::ip::make_address(aAddress);
#else
			auto ip = boost::asio::ip::address::from_string(aAddress);
#endif
			if (ip == boost::asio::ip::address_v4::any() || ip == boost::asio::ip::address_v6::any()) {
				return true;
			}
		} catch (...) {
			return true;
		}

		return false;
	}

	string WebServerManager::getLocalServerAddress(const ServerConfig& aConfig) noexcept {
		auto bindAddress = aConfig.bindAddress.str();
		if (isAnyAddress(bindAddress)) {
			websocketpp::lib::asio::error_code ec;
			auto isV6 = endpoint_plain.get_local_endpoint(ec).protocol().family() == AF_INET6;
			if (ec) {
				dcassert(0);
			}

			bindAddress = isV6 ? "[::1]" : "127.0.0.1";
		} else {
			bindAddress = resolveAddress(bindAddress, aConfig.port.str());
		}

		return bindAddress + ":" + Util::toString(aConfig.port.num());
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

	bool WebServerManager::hasValidServerConfig() const noexcept {
		return plainServerConfig->hasValidConfig() || tlsServerConfig->hasValidConfig();
	}

	bool WebServerManager::hasUsers() const noexcept {
		return userManager->hasUsers();
	}

	bool WebServerManager::waitExtensionsLoaded() const noexcept {
		return extManager->waitLoaded();
	}

	bool WebServerManager::load(const MessageCallback& aErrorF) noexcept {
		const auto legacyXmlPath = AppUtil::getPath(CONFIG_DIR) + LEGACY_CONFIG_NAME_XML;
		if (PathUtil::fileExists(legacyXmlPath)) {
			SettingsManager::loadSettingFile(CONFIG_DIR, LEGACY_CONFIG_NAME_XML, [this](SimpleXML& xml) {
				if (xml.findChild("WebServer")) {
					xml.stepIn();

					fire(WebServerManagerListener::LoadLegacySettings(), xml);
					xml.stepOut();
				}
			}, aErrorF);

			File::deleteFile(legacyXmlPath);
		}

		fire(WebServerManagerListener::LoadSettings(), aErrorF);
		return hasValidServerConfig();
	}

	bool WebServerManager::save(const MessageCallback& aCustomErrorF) noexcept {
		auto errorF = aCustomErrorF;
		if (!errorF) {
			// Avoid crashes if the file is saved when core is not loaded
			errorF = [](const string&) {};
		}

		fire(WebServerManagerListener::SaveSettings(), errorF);
		return true;
	}

	bool ServerConfig::hasValidConfig() const noexcept {
		return port.num() > 0;
	}
}