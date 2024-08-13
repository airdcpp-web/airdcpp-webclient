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

#include <api/AdcCommandApi.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/ConnectionManager.h>

namespace webserver {

boost::regex AdcCommandApi::paramReg(R"([A-Z][A-Z0-9])");
boost::regex AdcCommandApi::commandReg(R"([A-Z][A-Z0-9]{2})");
boost::regex AdcCommandApi::supportReg(R"([A-Z][A-Z0-9]{3})");

#define SUPPORT_PARAM_ID "support"
#define SUPPORT_PARAM (ApiModule::RequestHandler::Param(SUPPORT_PARAM_ID, regex(R"([A-Z][A-Z0-9]{3})")))

#define HOOK_OUTGOING_HUB_COMMAND "hub_outgoing_command_hook"
#define HOOK_OUTGOING_UDP_COMMAND "udp_outgoing_command_hook"

	StringList AdcCommandApi::subscriptionList = {
		"incoming_hub_command",
		"incoming_udp_command",
		"incoming_user_connection_command",

		"outgoing_hub_command",
		"outgoing_udp_command",
		"outgoing_user_connection_command"
	};

	ActionHookResult<AdcCommand::ParamMap> AdcCommandApi::outgoingHubMessageHook(const AdcCommand& aCmd, const Client& aClient, const ActionHookResultGetter<AdcCommand::ParamMap>& aResultGetter) {
		return HookCompletionData::toResult<AdcCommand::ParamMap>(
			maybeFireHook(HOOK_OUTGOING_HUB_COMMAND, aCmd.getCommand(), WEBCFG(OUTGOING_HUB_COMMAND_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "command", serializeCommand(aCmd) },
					{ "hub", Serializer::serializeClient(&aClient) },
				});
			}),
			aResultGetter,
			deserializeHookParams
		);
	};

	ActionHookResult<AdcCommand::ParamMap> AdcCommandApi::outgoingUdpMessageHook(const AdcCommand& aCmd, const OnlineUserPtr& aUser, const ActionHookResultGetter<AdcCommand::ParamMap>& aResultGetter) {
		return HookCompletionData::toResult<AdcCommand::ParamMap>(
			maybeFireHook(HOOK_OUTGOING_UDP_COMMAND, aCmd.getCommand(), WEBCFG(OUTGOING_UDP_COMMAND_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "command", serializeCommand(aCmd) },
					{ "hub", Serializer::serializeOnlineUser(aUser) },
				});
			}),
			aResultGetter,
			deserializeHookParams
		);
	}

	AdcCommandApi::AdcCommandApi(Session* aSession) :
		FilterableHookApiModule(
			aSession,
			Access::ADMIN,
			subscriptionList,
			Access::ADMIN,
			AdcCommandApi::deserializeCommandString
		), 
		hubSupports(ClientManager::getInstance()->hubSupports), 
		hubUserSupports(ClientManager::getInstance()->hubUserSupports), 
		userConnectionSupports(ConnectionManager::getInstance()->userConnectionSupports)
	{

		ProtocolCommandManager::getInstance()->addListener(this);


		METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("hub_command")), AdcCommandApi::handlePostHubCommand);
		METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("udp_command")), AdcCommandApi::handlePostUDPCommand);

		VARIABLE_METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("hub_supports"), SUPPORT_PARAM), SupportHandler::handleAddSupport, hubSupports);
		VARIABLE_METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("hub_user_supports"), SUPPORT_PARAM), SupportHandler::handleAddSupport, hubUserSupports);
		VARIABLE_METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("user_connection_supports"), SUPPORT_PARAM), SupportHandler::handleAddSupport, userConnectionSupports);

		VARIABLE_METHOD_HANDLER(Access::ADMIN, METHOD_DELETE, (EXACT_PARAM("hub_supports"), SUPPORT_PARAM), SupportHandler::handleRemoveSupport, hubSupports);
		VARIABLE_METHOD_HANDLER(Access::ADMIN, METHOD_DELETE, (EXACT_PARAM("hub_user_supports"), SUPPORT_PARAM), SupportHandler::handleRemoveSupport, hubUserSupports);
		VARIABLE_METHOD_HANDLER(Access::ADMIN, METHOD_DELETE, (EXACT_PARAM("user_connection_supports"), SUPPORT_PARAM), SupportHandler::handleRemoveSupport, userConnectionSupports);

		createHook(HOOK_OUTGOING_HUB_COMMAND, [this](ActionHookSubscriber&& aSubscriber) {
			return ClientManager::getInstance()->outgoingHubCommandHook.addSubscriber(std::move(aSubscriber), HOOK_HANDLER(AdcCommandApi::outgoingHubMessageHook));
		}, [this](const string& aId) {
			ClientManager::getInstance()->outgoingHubCommandHook.removeSubscriber(aId);
		}, [this] {
			return ClientManager::getInstance()->outgoingHubCommandHook.getSubscribers();
		});

		createHook(HOOK_OUTGOING_UDP_COMMAND, [this](ActionHookSubscriber&& aSubscriber) {
			return ClientManager::getInstance()->outgoingUdpCommandHook.addSubscriber(std::move(aSubscriber), HOOK_HANDLER(AdcCommandApi::outgoingUdpMessageHook));
		}, [this](const string& aId) {
			ClientManager::getInstance()->outgoingUdpCommandHook.removeSubscriber(aId);
		}, [this] {
			return ClientManager::getInstance()->outgoingUdpCommandHook.getSubscribers();
		});
	}

	AdcCommandApi::~AdcCommandApi() {
		ProtocolCommandManager::getInstance()->removeListener(this);
	}

	void AdcCommandApi::deserializeCommandFeatures(const json& aJson, AdcCommand& aCmd) {
		if (aCmd.getType() == AdcCommand::TYPE_FEATURE) {
			auto requiredFeatures = Deserializer::deserializeList<string>("required_features", aJson, deserializeSupportString, true);
			for (const auto& f : requiredFeatures) {
				aCmd.addFeature(f, AdcCommand::FeatureType::REQUIRED);
			}

			auto excludedFeatures = Deserializer::deserializeList<string>("excluded_features", aJson, deserializeSupportString, true);
			for (const auto& f : requiredFeatures) {
				aCmd.addFeature(f, AdcCommand::FeatureType::EXCLUDED);
			}

			if (aCmd.getFeatures().empty()) {
				JsonUtil::throwError("type", JsonUtil::ERROR_INVALID, "Features must be specified for this command type");
			}
		}
	}

	void AdcCommandApi::deserializeCommandRecipient(const json& aJson, AdcCommand& aCmd, const ClientPtr& aClient) {
		auto user = Deserializer::deserializeUser(aJson, false, true);
		if ((aCmd.getType() == AdcCommand::TYPE_DIRECT || aCmd.getType() == AdcCommand::TYPE_ECHO) && !user) {
			JsonUtil::throwError("user", JsonUtil::ERROR_MISSING, "Field is required for this command type");
		}

		if (user) {
			auto onlineUser = ClientManager::getInstance()->findOnlineUser(user->getCID(), aClient->getHubUrl(), false);
			if (!onlineUser) {
				JsonUtil::throwError("user", JsonUtil::ERROR_INVALID, "User not found");
			}

			aCmd.setTo(onlineUser->getIdentity().getSID());
		}
	}

	api_return AdcCommandApi::handlePostHubCommand(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto hub = Deserializer::deserializeClient(reqJson, false);

		auto cmd = deserializeCommand(reqJson);
		deserializeCommandRecipient(reqJson, cmd, hub);
		deserializeCommandFeatures(reqJson, cmd);

		addAsyncTask([
			cmd = std::move(cmd),
			hub,
			complete = aRequest.defer(),
			caller = aRequest.getOwnerPtr()
		]{
			// Send
			auto success = hub->sendHooked(cmd);
			if (!success) {
				complete(websocketpp::http::status_code::bad_request, nullptr, ApiRequest::toResponseErrorStr("Command rejected"));
				return;
			}

			complete(websocketpp::http::status_code::no_content, nullptr, nullptr);
		});

		return CODE_DEFERRED;
	}

	api_return AdcCommandApi::handlePostUDPCommand(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto cmd = deserializeCommand(reqJson);
		auto user = Deserializer::deserializeHintedUser(reqJson, false, false);
		auto passiveFallback = JsonUtil::getOptionalFieldDefault<bool>("hub_fallback", reqJson, false);

		if (cmd.getType() != AdcCommand::TYPE_UDP) {
			JsonUtil::throwError("type", JsonUtil::ERROR_INVALID, "Invalid type for an UDP command");
		}

		addAsyncTask([
			adcCmd = std::move(cmd),
			passiveFallback,
			user,
			complete = aRequest.defer(),
			caller = aRequest.getOwnerPtr()
		]{
			auto cmd = adcCmd;

			// Send
			auto success = ClientManager::getInstance()->sendUDPHooked(cmd, user.user->getCID(), false, !passiveFallback, Util::emptyString, user.hint);
			if (!success) {
				complete(websocketpp::http::status_code::bad_request, nullptr, ApiRequest::toResponseErrorStr("Command rejected"));
				return;
			}

			complete(websocketpp::http::status_code::no_content, nullptr, nullptr);
		});

		return CODE_DEFERRED;
	}
	api_return AdcCommandApi::SupportHandler::handleAddSupport(ApiRequest& aRequest) {
		auto support = aRequest.getStringParam(SUPPORT_PARAM_ID);
		auto success = supportStore.add(support);
		return websocketpp::http::status_code::no_content;
	}

	api_return AdcCommandApi::SupportHandler::handleRemoveSupport(ApiRequest& aRequest) {
		auto support = aRequest.getStringParam(SUPPORT_PARAM_ID);
		auto success = supportStore.remove(support);
		if (!success) {
			aRequest.setResponseErrorStr("Support " + support + " was not found");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::no_content;
	}

	void AdcCommandApi::on(ProtocolCommandManagerListener::IncomingHubCommand, const AdcCommand& aCmd, const Client& aClient) noexcept {
		maybeSend("incoming_hub_command", aCmd.getCommand(), [&] {
			return json({
				{ "command", serializeCommand(aCmd) },
				{ "hub", Serializer::serializeClient(&aClient) },
				{ "user", serializeFrom(aCmd, aClient) },
			});
		});
	}
	void AdcCommandApi::on(ProtocolCommandManagerListener::IncomingUDPCommand, const AdcCommand& aCmd, const string& aRemoteIp) noexcept {
		maybeSend("incoming_udp_command", aCmd.getCommand(), [&] {
			return json({
				{ "command", serializeCommand(aCmd) },
				{ "ip", aRemoteIp },
			});
		});
	}

	void AdcCommandApi::on(ProtocolCommandManagerListener::IncomingTCPCommand, const AdcCommand& aCmd, const string& aRemoteIp, const UserPtr& aUser) noexcept {
		maybeSend("incoming_user_connection_command", aCmd.getCommand(), [&] {
			return json({
				{ "command", serializeCommand(aCmd) },
				{ "ip", aRemoteIp },
				{ "user", aUser ? Serializer::serializeUser(aUser) : json()},
			});
		});
	}

	void AdcCommandApi::on(ProtocolCommandManagerListener::OutgoingUDPCommand, const AdcCommand& aCmd, const string& aRemoteIp, const OnlineUserPtr& aUser) noexcept {
		maybeSend("outgoing_udp_command", aCmd.getCommand(), [&] {
			return json({
				{ "command", serializeCommand(aCmd) },
				{ "ip", aRemoteIp },
				{ "user", Serializer::serializeOnlineUser(aUser) },
			});
		});
	}

	void AdcCommandApi::on(ProtocolCommandManagerListener::OutgoingTCPCommand, const AdcCommand& aCmd, const string& aRemoteIp, const UserPtr& aUser) noexcept {
		maybeSend("outgoing_user_connection_command", aCmd.getCommand(), [&] {
			return json({
				{ "command", serializeCommand(aCmd) },
				{ "ip", aRemoteIp },
				{ "user", aUser ? Serializer::serializeUser(aUser) : json()},
			});
		});
	}

	json AdcCommandApi::serializeTo(const AdcCommand& aCmd, const Client& aClient) noexcept {
		if (aCmd.getTo()) {
			return serializeUser(aCmd.getTo(), aClient);
		}

		return nullptr;
	}

	json AdcCommandApi::serializeFrom(const AdcCommand& aCmd, const Client& aClient) noexcept {
		if (aCmd.getFrom()) {
			return serializeUser(aCmd.getFrom(), aClient);
		}

		return nullptr;
	}

	json AdcCommandApi::serializeUser(uint32_t aSID, const Client& aClient) noexcept {
		auto user = aClient.findUser(aSID);
		if (user) {
			return Serializer::serializeOnlineUser(user);
		}

		return nullptr;
	}


	string AdcCommandApi::deserializeSupportString(const json& aCmd, const string& aFieldName) {
		auto support = JsonUtil::parseValue<string>(aFieldName, aCmd, false);
		if (!boost::regex_match(support, supportReg)) {
			JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid support " + support);
		}

		return support;
	}

	AdcCommand::CommandType AdcCommandApi::deserializeCommandString(const json& aCmd, const string& aFieldName) {
		auto cmd = JsonUtil::parseValue<string>(aFieldName, aCmd, false);
		if (!boost::regex_match(cmd, commandReg)) {
			JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid command " + cmd);
		}

		auto commandInt = AdcCommand::toCommand(cmd);
		return commandInt;
	}

	json AdcCommandApi::serializeCommand(const AdcCommand& aCmd) noexcept {
		auto code = aCmd.getFourCC();
		auto tmp = code[0];
		return {
			{ "command", code.substr(1) },
			{ "type", string(1, code[0]) },
			{ "params", aCmd.getParameters() },
		};
	}

	AdcCommand::ParamMap AdcCommandApi::deserializeHookParams(const json& aJson, const ActionHookResultGetter<AdcCommand::ParamMap>&) {
		return deserializeParams(aJson, true);
	}

	AdcCommandApi::AdcParam AdcCommandApi::deserializeParam(const json& aJson, const string& aFieldName) {
		auto name = JsonUtil::getField<string>("name", aJson, false);
		if (!boost::regex_match(name, paramReg)) {
			JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid param name " + name);
		}

		auto value = JsonUtil::getField<string>("value", aJson, false);

		return AdcParam({ name, value });
	}

	AdcCommand::ParamMap AdcCommandApi::deserializeParams(const json& aJson, bool aAllowEmpty) {
		auto paramList = Deserializer::deserializeList<AdcParam>("params", aJson, deserializeParam, aAllowEmpty);

		AdcCommand::ParamMap paramMap;
		for (const auto& param : paramList) {
			paramMap.emplace(param.name, param.value);
		}

		return paramMap;
	}

	AdcCommand AdcCommandApi::deserializeCommand(const json& aJson) {
		auto commandJson = JsonUtil::getRawField("command", aJson);

		auto type = JsonUtil::getField<string>("type", commandJson, false)[0];
		if (!AdcCommand::isValidType(type)) {
			JsonUtil::throwError("type", JsonUtil::ERROR_INVALID, "Invalid type " + string(1, type));
		}

		auto command = deserializeCommandString(JsonUtil::getRawField("command", commandJson), "command");
		auto params = deserializeParams(commandJson, false);

		auto cmd = AdcCommand(command, type).addParams(params);
		return cmd;
	}

	void AdcCommandApi::on(ProtocolCommandManagerListener::OutgoingHubCommand, const AdcCommand& aCmd, const Client& aClient) noexcept {
		maybeSend("outgoing_hub_command", aCmd.getCommand(), [&] {
			return json({
				{ "command", serializeCommand(aCmd) },
				{ "hub", Serializer::serializeClient(&aClient) },
				{ "user", serializeTo(aCmd, aClient) },
			});
		});
	}
}