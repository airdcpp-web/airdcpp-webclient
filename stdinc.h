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

#ifndef DCPLUSPLUS_WEBSERVER_STDINC_H
#define DCPLUSPLUS_WEBSERVER_STDINC_H

#ifndef _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CPP11_STL_
#endif

#if defined _MSC_VER

#ifndef _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
#define _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING // boost\bimap\detail\manage_additional_parameters.hpp(86): error C4996: 'std::allocator<void>': warning STL4009: std::allocator<void> is deprecated in C++17.
#endif

#ifndef _SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING 
#define _SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING // boost\detail\allocator_utilities.hpp(125): error C4996: 'std::allocator<_Other>::rebind<Type>::other': warning STL4010: Various members of std::allocator are deprecated in C++17.
#endif

#ifndef _SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING // TODO: fix when GCC 7 is required with support for std::not_fn
#define _SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING // api\common\serializer.cpp(235): error C4996: 'std::not1': warning STL4008: std::not1(), std::not2(), std::unary_negate, and std::binary_negate are deprecated in C++17. They are superseded by std::not_fn().
#endif

# pragma warning(disable: 4834) // boost\asio\buffer.hpp(710): warning C4834: discarding return value of function with 'nodiscard' attribute

#endif

#include <airdcpp/stdinc.h>
#include <airdcpp/StringTokenizer.h>

#include <nlohmann/json.hpp>

#include <web-server/Exception.h>

#include <websocketpp/http/constants.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

#include <boost/range/algorithm/copy.hpp>
#include <boost/algorithm/cxx11/copy_if.hpp>



#define CODE_UNPROCESSABLE_ENTITY 422

namespace webserver {
	// define types for two different server endpoints, one for each config we are
	// using
	typedef websocketpp::server<websocketpp::config::asio> server_plain;
	typedef websocketpp::server<websocketpp::config::asio_tls> server_tls;
	typedef websocketpp::http::status_code::value api_return;

	using namespace dcpp;

	using json = nlohmann::json;

	using ArgumentException = webserver::JsonException;


	class ApiRequest;

	typedef std::function<void()> CallBack;

	class Extension;
	typedef shared_ptr<Extension> ExtensionPtr;
	typedef vector<ExtensionPtr> ExtensionList;

	class Session;
	typedef std::shared_ptr<Session> SessionPtr;
	typedef std::vector<SessionPtr> SessionList;
	typedef uint32_t LocalSessionId;

	typedef map<string, json> SettingValueMap;

	class Timer;
	typedef shared_ptr<Timer> TimerPtr;

	class WebSocket;
	typedef std::shared_ptr<WebSocket> WebSocketPtr;

	class WebServerManager;
}

#endif // !defined(DCPLUSPLUS_WEBSERVER_STDINC_H)
