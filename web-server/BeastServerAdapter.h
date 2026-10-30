// Boost.Beast based implementation of IServerEndpoint (phase 2 adapter)
#ifndef DCPLUSPLUS_WEBSERVER_BEAST_SERVER_ADAPTER_H
#define DCPLUSPLUS_WEBSERVER_BEAST_SERVER_ADAPTER_H

#include "stdinc.h"
#include "IServerEndpoint.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <atomic>
#include <array>

namespace webserver {
	namespace beast = boost::beast;
	namespace http = beast::http;
	namespace websocket = beast::websocket;
	using tcp = boost::asio::ip::tcp;

	// The two transports a session can run on. Everything below is written against
	// whichever of these it was given, so the plain and TLS paths share one implementation.
	using PlainStream = beast::tcp_stream;
	using SslStream = beast::ssl_stream<beast::tcp_stream>;

	class BeastServerAdapter : public IServerEndpoint, public std::enable_shared_from_this<BeastServerAdapter> {
	public:
		BeastServerAdapter() = default;
		~BeastServerAdapter() override { stopListening(); }

		// IServerEndpoint
		void initAsio(boost::asio::io_context* ios) override { ios_ = ios; }
		void setOpenHandshakeTimeout(int) override {}
		void setPongTimeout(int millis) override { pongTimeoutMs_ = millis; }
		void setMaxHttpBodySize(std::size_t bytes) override { maxHttpBodySize_.store(bytes, std::memory_order_relaxed); }

		// Setting a handler is what makes this endpoint a TLS endpoint: the context it returns
		// is built once when accepting starts, and every accepted connection then runs an
		// async_handshake before any HTTP is read. Without a handler the endpoint stays plain.
		void setTlsInitHandler(std::function<contextPtr()> handler) override { tlsInit_ = std::move(handler); }

		void configureLogging(std::ostream* access, std::ostream* error, bool enable) override {
			std::lock_guard<std::mutex> lk(logMutex_);
			accessLog_ = access;
			errorLog_ = error;
			loggingEnabled_ = enable && (access || error);
		}

		void setReuseAddr(bool enable) override { reuseAddr_ = enable; }
		void listen(const std::string& bindAddress, const std::string& port) override {
			ensureAcceptor();
			boost::system::error_code ec;
			auto addr = boost::asio::ip::make_address(bindAddress, ec);
			if (ec) {
				// An address we cannot parse used to fall back to 0.0.0.0, which is the opposite of
				// what the person who typed it asked for: a setting meant to confine the server to one
				// interface silently exposed it on all of them, with nothing said. Refuse instead, the
				// same way a failed bind does -- listenEndpoint() reports it as a setup failure.
				throw std::runtime_error("invalid bind address \"" + bindAddress + "\": " + ec.message());
			}
			uint16_t prt = static_cast<uint16_t>(dcpp::Util::toInt(port));
			tcp::endpoint ep(addr, prt);
			openAndBind(ep);
		}
		void listen(const boost::asio::ip::tcp& protocol, uint16_t port) override {
			ensureAcceptor();
			tcp::endpoint ep(protocol, port);
			openAndBind(ep);
		}
		void startAccept() override {
			if (!acceptor_ || !acceptor_->is_open() || listening_) return;

			// Build the TLS context once, here rather than per connection: a failure now is
			// worth refusing to listen over, since accepting on a TLS port we can't actually
			// secure would serve the connection in the clear. Throw rather than return -
			// the acceptor is already bound and listening by this point, so simply bailing
			// would leave the port open with nothing draining it (connections would hang
			// instead of being refused) while the caller was told the server had started.
			// listenEndpoint() turns this into a reported setup failure.
			if (tlsInit_ && !sslContext_) {
				sslContext_ = tlsInit_();
				if (!sslContext_) {
					boost::system::error_code ignored;
					acceptor_->close(ignored);
					throw std::runtime_error("TLS context initialization failed");
				}
			}

			listening_ = true;
			doAccept();
		}
		bool isListening() const noexcept override { return listening_; }
		void stopListening() override {
			listening_ = false;
			if (acceptor_) {
				boost::system::error_code _;
				acceptor_->close(_);
			}

			// Drop the context so restarting the web server picks up a corrected certificate
			// path or a renewed certificate. Keeping it would mean only an application
			// restart could ever fix a bad one.
			sslContext_.reset();
		}

		void setMessageHandler(std::function<void(ConnectionHdl, const std::string&)> f) override { onMessage_ = std::move(f); }
		void setHttpHandler(std::function<void(ConnectionHdl)> f) override { onHttp_ = std::move(f); }
		void setCloseHandler(std::function<void(ConnectionHdl)> f) override { onClose_ = std::move(f); }
		void setOpenHandler(std::function<void(ConnectionHdl)> f) override { onOpen_ = std::move(f); }
		void setPongTimeoutHandler(std::function<void(ConnectionHdl, const std::string&)> f) override { onPongTimeout_ = std::move(f); }

		std::string getRemoteIp(ConnectionHdl hdl) override {
			if (auto s = lockSession(hdl)) {
				return s->remoteIp;
			}
			return {};
		}
		std::string getHeader(ConnectionHdl hdl, const std::string& name) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				auto it = s->req.find(name);
				return it == s->req.end() ? std::string() : std::string(it->value());
			}
			return {};
		}
		std::string getBody(ConnectionHdl hdl) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				return std::string(s->req.body());
			}
			return {};
		}
		std::string getMethod(ConnectionHdl hdl) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				return std::string(s->req.method_string());
			}
			if (auto ws = lockWs(hdl)) return ws->methodStr; // usually GET
			return {};
		}
		std::string getUri(ConnectionHdl hdl) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				return std::string(s->req.target());
			}
			if (auto ws = lockWs(hdl)) return ws->target; // captured during upgrade
			return {};
		}
		std::string getResource(ConnectionHdl hdl) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				return std::string(s->req.target());
			}
			else if (auto ws = lockWs(hdl)) return ws->target;
			return "";
		}

		void httpAppendHeader(ConnectionHdl hdl, const std::string& name, const std::string& value) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				s->respHeaders.emplace_back(name, value);
			}
		}
		void httpSetBody(ConnectionHdl hdl, const std::string& body) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				s->respBody = body;
			}
		}
		void httpSetStatus(ConnectionHdl hdl, http::status status) override {
			if (auto s = lockHttp(hdl)) {
				std::lock_guard<std::mutex> lk(s->stateMutex);
				s->respStatus = status;
			}
		}
		void httpDeferResponse(ConnectionHdl hdl) override {
			if (auto s = lockHttp(hdl)) s->markDeferred();
		}
		void httpSendResponse(ConnectionHdl hdl) override {
			if (auto s = lockHttp(hdl)) s->sendAndRelease();
		}

		void wsSendText(ConnectionHdl hdl, const std::string& text) override {
			if (auto s = lockWs(hdl)) {
				s->sendText(text);
			}
		}
		void wsPing(ConnectionHdl hdl) override {
			if (auto s = lockWs(hdl)) {
				s->sendPing();
			}
		}
		void wsClose(ConnectionHdl hdl, uint16_t code, const std::string& reason) override {
			if (auto s = lockWs(hdl)) {
				s->sendClose(code, reason);
			}
		}

	private:
		// Logging helpers
		void logAccess(const std::string& msg) const {
			if (!loggingEnabled_) return;
			std::lock_guard<std::mutex> lk(logMutex_);
			if (accessLog_) (*accessLog_) << msg << std::endl;
		}
		void logError(const std::string& msg) const {
			if (!loggingEnabled_) return;
			std::lock_guard<std::mutex> lk(logMutex_);
			if (errorLog_) (*errorLog_) << msg << std::endl;
		}

		struct SessionBase {
			virtual ~SessionBase() = default;
			std::string remoteIp;
			ConnectionHdl hdl;
		};

		// The adapter's public accessors reach into a session by handle, so everything they
		// touch lives here, off the template: only the operations that need the concrete
		// stream type are virtual. That keeps lockHttp()/lockWs() returning one type each
		// however many transports there are.
		struct HttpSessionBase : SessionBase {
			virtual void markDeferred() = 0;
			virtual void sendAndRelease() = 0;

			http::request<http::string_body> req;
			http::status respStatus = http::status::ok;
			std::vector<std::pair<std::string, std::string>> respHeaders;
			std::string respBody;
			bool deferred = false;
			// Guards req / respStatus / respHeaders / respBody / deferred, which may be
			// touched both from the connection strand and from a deferred completion
			// running on the WebServerManager task io_context.
			std::mutex stateMutex;
		};

		struct WsSessionBase : SessionBase {
			virtual void sendText(const std::string& text) = 0;
			virtual void sendPing() = 0;
			virtual void sendClose(uint16_t code, const std::string& reason) = 0;

			std::string target;
			std::string methodStr = "GET";
		};

		template<class Stream>
		struct HttpSessionT : public HttpSessionBase, public std::enable_shared_from_this<HttpSessionT<Stream>> {
			static constexpr bool isTls = std::is_same_v<Stream, SslStream>;

			HttpSessionT(BeastServerAdapter& owner, Stream&& s, const std::string& ip)
				: adapter(owner), stream(std::move(s)), headTimer(beast::get_lowest_layer(stream).get_executor()) {
				remoteIp = ip;
			}

			// A TLS connection has to complete its handshake before there is any HTTP to read;
			// a plain one starts reading straight away.
			void start() {
				if constexpr (isTls) {
					auto self = this->shared_from_this();
					beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
					stream.async_handshake(boost::asio::ssl::stream_base::server,
						[self](beast::error_code ec) {
							if (ec) {
								self->adapter.logError("TLS handshake error: " + ec.message());
								return;
							}
							beast::get_lowest_layer(self->stream).expires_never();
							self->run();
						});
				} else {
					run();
				}
			}

			void run() {
				// Apply the configured request body size limit (0 == beast default/unlimited).
				auto limit = adapter.maxHttpBodySize_.load(std::memory_order_relaxed);
				if (limit > 0) {
					parser.body_limit(limit);
				}

				// Bound how long a peer may take to state its request, so one that connects
				// and then says nothing can't hold a session, a socket and (on TLS) an
				// OpenSSL context for as long as it likes.
				//
				// This is deliberately a timer of our own rather than the stream's own
				// timeout. beast::basic_stream::expires_never() is a no-op while a read is
				// pending (impl_type::reset() only touches the timer when !read.pending), so
				// an expiry armed for the head keeps running into the body and cuts off a
				// large or slow upload. Nothing here touches the body read.
				/*
				 * headDone, not just cancel().
				 *
				 * steady_timer::cancel() does NOT stop a handler that is already queued. Once the
				 * timer has expired, its completion is posted with ec == success and runs whatever
				 * cancel() does afterwards -- and this handler CLOSES THE SOCKET. So a request whose
				 * header happened to arrive in the same instant the deadline passed had its
				 * connection closed underneath it, at whatever point it had then reached: reading
				 * its body, or part-way through writing its response.
				 *
				 * That last case is the ugly one. A close on a socket with data still queued sends
				 * an RST rather than a FIN, so the response stops dead at whatever chunk the write
				 * had already handed to the kernel and the peer sees a connection reset rather than
				 * a short read. The observed truncations landed on exact 16 KiB boundaries of the
				 * response, which is precisely the shape of a write aborted between chunks.
				 *
				 * Thirty seconds sounds unreachable for a local request, and normally is -- but the
				 * io threads are a fixed pool (web_server_threads, default 4) and a request that
				 * waits behind them can take far longer than the wall-clock work it represents.
				 *
				 * Both this handler and onHeader run on the connection strand, so a plain bool is
				 * enough; there is no ordering question between them.
				 */
				auto self = this->shared_from_this();
				headTimer.expires_after(std::chrono::seconds(30));
				headTimer.async_wait([self](beast::error_code ec) {
					if (ec == boost::asio::error::operation_aborted || self->headDone) {
						return; // the head arrived in time
					}
					self->adapter.logError("Timed out waiting for a request, closing connection");
					beast::error_code ignored;
					beast::get_lowest_layer(self->stream).socket().close(ignored);
				});

				http::async_read_header(stream, buffer, parser,
					[self](beast::error_code ec, std::size_t) {
					self->onHeader(ec);
				});
			}

			void onHeader(beast::error_code ec) {
				// The request has been stated (or the attempt failed) - either way the clock
				// stops here, and the body is read with no time limit of its own.
				//
				// The flag is what actually stops it: see the note on the timer in run() for why
				// cancel() alone is not enough.
				headDone = true;
				headTimer.cancel();

				if (ec) {
					adapter.logError("HTTP header read error: " + ec.message());
					return;
				}

				auto self = this->shared_from_this();
				http::async_read(stream, buffer, parser,
					[self](beast::error_code ec2, std::size_t) {
					self->onRead(ec2);
				});
			}

			void onRead(beast::error_code ec) {
				if (ec) {
					adapter.logError("HTTP read error: " + ec.message());
					return;
				}

				// Move the parsed request into the session-visible member.
				{
					std::lock_guard<std::mutex> lk(stateMutex);
					req = parser.release();
				}

				adapter.logAccess("HTTP request " + std::string(req.method_string()) + " " + std::string(req.target()));

				if (websocket::is_upgrade(req)) {
					// Upgrade to websocket, handing the whole stream over so a TLS connection
					// keeps its encryption across the upgrade
					auto wsSession = std::make_shared<WsSessionT<Stream>>(adapter, std::move(stream), remoteIp);
					auto h = makeHandle(std::static_pointer_cast<SessionBase>(wsSession));
					wsSession->doAccept(std::move(req), h);
					return;
				}

				hdl = makeHandle(std::static_pointer_cast<SessionBase>(this->shared_from_this()));

				if (adapter.onHttp_) {
					adapter.onHttp_(hdl);
				}

				if (!deferred) {
					sendResponse();
				}
			}

			// keep the session alive while deferred
			void markDeferred() override {
				// Called on the connection strand (from onRead -> onHttp_).
				std::lock_guard<std::mutex> lk(stateMutex);
				if (!deferred) {
					deferred = true;
					selfKeep = this->shared_from_this();
					adapter.logAccess("HTTP deferred response");
				}
			}

			void sendAndRelease() override {
				// May be invoked from the WebServerManager task io_context (deferred
				// completion). Marshal the actual response onto the connection strand so
				// the stream and session state are only touched there.
				auto self = this->shared_from_this();
				boost::asio::post(stream.get_executor(), [self] {
					self->sendResponse();
					std::lock_guard<std::mutex> lk(self->stateMutex);
					self->deferred = false;
					self->selfKeep.reset();
				});
			}

			void sendResponse() {
				// Must run on the connection strand. Snapshot the response state under the
				// lock, then build/send the response without holding it.
				http::status statusCopy;
				unsigned version;
				std::vector<std::pair<std::string,std::string>> headersCopy;
				std::string bodyCopy;
				{
					std::lock_guard<std::mutex> lk(stateMutex);
					statusCopy = respStatus;
					version = req.version();
					headersCopy = respHeaders;
					bodyCopy = respBody;
				}

				auto beastStatus = static_cast<http::status>(static_cast<unsigned>(statusCopy));
				respPtr = std::make_shared<http::response<http::string_body>>(beastStatus, version);
				respPtr->set(http::field::server, "fuldcpp-beast");
				for (const auto& [k,v] : headersCopy) respPtr->set(k, v);
				respPtr->body() = bodyCopy;
				respPtr->prepare_payload();

				// This session serves exactly one request and then closes (see doClose after
				// the write), but an HTTP/1.1 response is keep-alive unless it says otherwise,
				// so browsers and API clients reused the connection for their next request and
				// got it reset: a web UI page load lost random assets, and every other call in
				// a sequence of API requests failed with "connection closed unexpectedly".
				respPtr->keep_alive(false);

				adapter.logAccess("HTTP response " + std::to_string(static_cast<unsigned>(statusCopy)) + " bytes=" + std::to_string(respPtr->body().size()));

				auto self = this->shared_from_this();
				http::async_write(stream, *respPtr, [self](beast::error_code ec, std::size_t /*bytes*/){
					if (ec) {
						self->adapter.logError("HTTP write error: " + ec.message());
					}
					self->respPtr.reset();
					self->doClose();
				});
			}

			void doClose() {
				if constexpr (isTls) {
					// Send close_notify so the peer sees a clean end rather than a truncation,
					// then let the socket go when it completes (or fails - we are closing either way).
					auto self = this->shared_from_this();
					beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
					stream.async_shutdown([self](beast::error_code) {
						beast::error_code ec;
						beast::get_lowest_layer(self->stream).socket().shutdown(tcp::socket::shutdown_both, ec);
					});
				} else {
					// Half-close our side (the peer sees a FIN after the response), then read and
					// discard until the peer closes too. A bare close while bytes are still in
					// flight, or while the peer's next pipelined request already sits unread in our
					// receive buffer, makes the OS send an RST instead of a clean FIN -- which is
					// exactly what a browser loading several assets at once, or a client making a
					// run of API calls, saw as random "connection reset" failures. The 5s stream
					// timeout below bounds the drain so a peer that never closes can't pin us.
					beast::error_code ec;
					beast::get_lowest_layer(stream).socket().shutdown(tcp::socket::shutdown_send, ec);
					beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
					drainAndClose(std::make_shared<std::array<char, 1024>>());
				}
			}

			void drainAndClose(std::shared_ptr<std::array<char, 1024>> drainBuf) {
				auto self = this->shared_from_this();
				beast::get_lowest_layer(stream).async_read_some(boost::asio::buffer(*drainBuf),
					[self, drainBuf](beast::error_code ec, std::size_t) {
						if (ec) {
							// EOF (the peer closed) or the timeout fired -- either way we are done
							beast::error_code ignored;
							beast::get_lowest_layer(self->stream).socket().close(ignored);
							return;
						}
						self->drainAndClose(drainBuf);
					});
			}

			static ConnectionHdl makeHandle(const std::shared_ptr<SessionBase>& s) {
				std::weak_ptr<void> w = std::static_pointer_cast<void>(s);
				return w;
			}

			BeastServerAdapter& adapter;
			Stream stream;
			// Runs on the connection strand, like every other operation on this session
			boost::asio::steady_timer headTimer;
			// Set when the header read completes, so an already-expired headTimer whose handler is
			// queued cannot close a connection that has since got on with its request.
			bool headDone = false;
			beast::flat_buffer buffer;
			http::request_parser<http::string_body> parser;
			std::shared_ptr<http::response<http::string_body>> respPtr;
			std::shared_ptr<HttpSessionT> selfKeep;
		};

		template<class Stream>
		struct WsSessionT : public WsSessionBase, public std::enable_shared_from_this<WsSessionT<Stream>> {
			WsSessionT(BeastServerAdapter& owner, Stream&& s, const std::string& ip)
				: adapter(owner), ws(std::move(s)), pongTimer(ws.get_executor()) {
				remoteIp = ip;
			}

			void doAccept(http::request<http::string_body>&& req, ConnectionHdl handle) {
				// Persist the HTTP request for the async_accept lifetime
				upgradeReq_ = std::move(req);
				// Capture essential request info before upgrading
				target = std::string(upgradeReq_.target());
				methodStr = std::string(upgradeReq_.method_string());
				hdl = handle;
				// Accept the websocket handshake. Turn the transport-level timeout off first:
				// beast's websocket stream runs its own timer, and leaving both armed makes
				// the two fight - any HTTP-phase expiry would otherwise be inherited here and
				// kill the long-lived socket the web UI keeps open.
				beast::get_lowest_layer(ws).expires_never();
				ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

				// The pong that answers our ping cancels the deadline armed in sendPing(). Beast
				// replies to incoming pings by itself; only the pong frame is of interest here.
				ws.control_callback([self = this->shared_from_this()](websocket::frame_type kind, beast::string_view) {
					if (kind == websocket::frame_type::pong) {
						self->pongPending = false;
						self->pongTimer.cancel();
					}
				});
				ws.async_accept(upgradeReq_, beast::bind_front_handler(&WsSessionT::onAccept, this->shared_from_this()));
			}

			void onAccept(beast::error_code ec) {
				// release the stored request
				upgradeReq_ = {};
				if (ec) {
					adapter.logError("WS accept error: " + ec.message());
					return;
				}
				adapter.logAccess("WS open " + target);
				if (adapter.onOpen_) adapter.onOpen_(hdl);
				readLoop();
			}

			void readLoop() {
				ws.async_read(buffer, beast::bind_front_handler(&WsSessionT::onRead, this->shared_from_this()));
			}

			void onRead(beast::error_code ec, std::size_t) {
				if (ec) {
					adapter.logAccess("WS close " + target + " reason=" + ec.message());
					notifyClose();
					return;
				}
				if (adapter.onMessage_) {
					auto data = beast::buffers_to_string(buffer.cdata());
					adapter.logAccess("WS message bytes=" + std::to_string(data.size()));
					adapter.onMessage_(hdl, data);
				}
				buffer.consume(buffer.size());
				readLoop();
			}

			// Maximum number of outbound messages allowed to queue before the peer is
			// considered too slow; the connection is dropped to bound memory use.
			static constexpr std::size_t MAX_OUT_QUEUE_DEPTH = 1024;

			// Thread-safe send helpers queued on the ws executor
			void sendText(const std::string& text) override {
				auto self = this->shared_from_this();
				boost::asio::post(ws.get_executor(), [self, text] {
					if (self->outQueue.size() >= MAX_OUT_QUEUE_DEPTH) {
						// Slow/stalled consumer: drop the connection rather than grow the
						// queue without bound.
						self->adapter.logError("WS output queue overflow (" + std::to_string(self->outQueue.size()) + "), closing connection");
						beast::error_code ec;
						beast::get_lowest_layer(self->ws).socket().close(ec);
						return;
					}
					self->outQueue.push_back(text);
					if (!self->writing) {
						self->startWrite();
					}
				});
			}

			void sendPing() override {
				auto self = this->shared_from_this();
				boost::asio::post(ws.get_executor(), [self] {
					self->ws.async_ping(websocket::ping_data{}, [self](beast::error_code ec){ if(ec) self->adapter.logError("WS ping error: "+ec.message()); });
					self->armPongTimer();
				});
			}

			// Starts the deadline for the pong that should answer the ping just sent. Re-arming
			// while one is already pending is harmless -- the ping driver only sends on an idle
			// connection -- and expiry reports the connection as timed out exactly once.
			void armPongTimer() {
				const auto timeoutMs = adapter.pongTimeoutMs_;
				if (timeoutMs <= 0) {
					return;
				}

				pongPending = true;
				pongTimer.expires_after(std::chrono::milliseconds(timeoutMs));

				auto self = this->shared_from_this();
				pongTimer.async_wait([self](beast::error_code ec) {
					// Cancelled by the pong, or the session is going away.
					if (ec || !self->pongPending) {
						return;
					}

					self->pongPending = false;
					if (self->adapter.onPongTimeout_) {
						self->adapter.onPongTimeout_(self->hdl, std::string());
					}
				});
			}

			void sendClose(uint16_t code, const std::string& /*reason*/) override {
				auto self = this->shared_from_this();
				boost::asio::post(ws.get_executor(), [self, code] {
					websocket::close_reason cr;
					cr.code = static_cast<websocket::close_code>(code);
					self->ws.async_close(cr, [self](beast::error_code ec){ if(ec) self->adapter.logError("WS close error: "+ec.message()); });
				});
			}

			// Deliver the close notification at most once per session. Both the read-error and
			// write-error completion paths (and the queue-overflow drop) can fire for the same
			// connection; without this guard onClose_ could run twice for one hdl. All callers
			// run on the ws executor, so a plain flag is safe.
			void notifyClose() {
				if (closeNotified) return;
				closeNotified = true;

				// A connection that is already going away must not also be reported as having
				// timed out waiting for a pong.
				pongPending = false;
				pongTimer.cancel();
				if (adapter.onClose_) adapter.onClose_(hdl);
			}

			void startWrite() {
				if (outQueue.empty()) return;
				writing = true;
				ws.text(true);
				auto self = this->shared_from_this();
				ws.async_write(boost::asio::buffer(outQueue.front()), [self](beast::error_code ec, std::size_t bytes) {
					if (ec) {
						self->adapter.logError("WS write error: " + ec.message());
						self->notifyClose();
						return;
					}
					self->adapter.logAccess("WS sent bytes=" + std::to_string(bytes));
					self->outQueue.pop_front();
					if (!self->outQueue.empty()) {
						self->startWrite();
					} else {
						self->writing = false;
					}
				});
			}

			BeastServerAdapter& adapter;
			websocket::stream<Stream> ws;
			beast::flat_buffer buffer;
			std::deque<std::string> outQueue;
			bool writing = false;
			bool closeNotified = false;

			// Armed when a ping goes out, cancelled by the matching pong. Both happen on the ws
			// executor, so it needs no synchronisation of its own.
			boost::asio::steady_timer pongTimer;
			bool pongPending = false;
			http::request<http::string_body> upgradeReq_;
		};

		// Helpers to lock specific session types
		std::shared_ptr<SessionBase> lockSession(ConnectionHdl hdl) const {
			auto p = hdl.lock();
			return std::static_pointer_cast<SessionBase>(p);
		}
		// Cast to the non-template bases, so a plain and a TLS session are the same type here
		std::shared_ptr<HttpSessionBase> lockHttp(ConnectionHdl hdl) const {
			auto p = hdl.lock();
			return std::dynamic_pointer_cast<HttpSessionBase>(std::static_pointer_cast<SessionBase>(p));
		}
		std::shared_ptr<WsSessionBase> lockWs(ConnectionHdl hdl) const {
			auto p = hdl.lock();
			return std::dynamic_pointer_cast<WsSessionBase>(std::static_pointer_cast<SessionBase>(p));
		}

		void ensureAcceptor() {
			if (!ios_) return;
			if (!acceptor_) acceptor_ = std::make_unique<tcp::acceptor>(*ios_);
		}

		void openAndBind(const tcp::endpoint& ep) {
			if (!acceptor_) return;

			// Check each step. Sharing one error_code across all four and testing it only at
			// the end hides a bind failure whenever listen() happens to succeed - the user is
			// then told the server started on a port it never took. Throw so listenEndpoint()
			// reports it: setOption is the one exception, since reuse_address failing is not
			// worth refusing to start over.
			// Close the acceptor again on any failure below. It stayed open after a failed bind, and
			// the next attempt on that endpoint then failed with "open: Already open" instead of
			// retrying -- so a port that was briefly taken stayed unusable until the client restarted.
			const auto closeOnFailure = [this]() {
				boost::system::error_code ignored;
				acceptor_->close(ignored);
			};

			boost::system::error_code ec;
			acceptor_->open(ep.protocol(), ec);
			if (ec) throw std::runtime_error("open: " + ec.message());

			if (reuseAddr_) {
				boost::system::error_code ignored;
				acceptor_->set_option(boost::asio::socket_base::reuse_address(true), ignored);
			}

			acceptor_->bind(ep, ec);
			if (ec) {
				closeOnFailure();
				throw std::runtime_error("bind: " + ec.message());
			}

			acceptor_->listen(boost::asio::socket_base::max_listen_connections, ec);
			if (ec) {
				closeOnFailure();
				throw std::runtime_error("listen: " + ec.message());
			}
		}

		void doAccept() {
			acceptor_->async_accept(
				boost::asio::make_strand(*ios_),
				[this](beast::error_code ec, tcp::socket socket) {
					if (!listening_) return;
					if (!ec) {
						auto ip = socket.remote_endpoint(ec).address().to_string();
						logAccess("ACCEPT " + ip);
						if (sslContext_) {
							std::make_shared<HttpSessionT<SslStream>>(
								*this, SslStream(beast::tcp_stream(std::move(socket)), *sslContext_), ip)->start();
						} else {
							std::make_shared<HttpSessionT<PlainStream>>(
								*this, beast::tcp_stream(std::move(socket)), ip)->start();
						}
						doAccept();
					} else {
						// Keep accepting. A per-connection failure - the peer vanishing before
						// the accept completes, or a momentary descriptor shortage - used to
						// end the accept loop for good while isListening() went on reporting
						// true, so the endpoint was dead but looked healthy. Only a closed
						// acceptor (stopListening) ends the loop.
						logError("Accept error: " + ec.message());
						if (ec != boost::asio::error::operation_aborted && acceptor_ && acceptor_->is_open()) {
							doAccept();
						}
					}
				}
			);
		}

		// members
		boost::asio::io_context* ios_ = nullptr;
		std::unique_ptr<tcp::acceptor> acceptor_;
		bool listening_ = false;
		bool reuseAddr_ = false;
		// 0 == no limit (beast default). Set via setMaxHttpBodySize().
		std::atomic<std::size_t> maxHttpBodySize_{ 0 };

		// Set only on a TLS endpoint: the handler comes from setTlsInitHandler() and the
		// context it builds is created once in startAccept(). Null means a plain endpoint.
		std::function<contextPtr()> tlsInit_;
		contextPtr sslContext_;

		std::function<void(ConnectionHdl, const std::string&)> onMessage_;
		std::function<void(ConnectionHdl)> onHttp_;
		std::function<void(ConnectionHdl)> onClose_;
		std::function<void(ConnectionHdl)> onOpen_;
		std::function<void(ConnectionHdl, const std::string&)> onPongTimeout_;

		// Milliseconds to wait for a pong after a ping; 0 disables the check. Set from the
		// ping_timeout setting via setPongTimeout().
		int pongTimeoutMs_ = 0;

		// logging state
		mutable std::mutex logMutex_;
		std::ostream* accessLog_ = nullptr;
		std::ostream* errorLog_ = nullptr;
		bool loggingEnabled_ = false;
	};
}

#endif // DCPLUSPLUS_WEBSERVER_BEAST_SERVER_ADAPTER_H
