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

#ifndef DCPLUSPLUS_DCPP_IGNORE_MANAGER_H_
#define DCPLUSPLUS_DCPP_IGNORE_MANAGER_H_

#include <airdcpp/forward.h>

#include <airdcpp/user/ignore/IgnoreManagerListener.h>
#include <airdcpp/settings/SettingsManagerListener.h>

#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/Speaker.h>
#include <airdcpp/util/text/StringMatch.h>
#include <airdcpp/user/User.h>


namespace dcpp {
	class ChatFilterItem {
	public:
		ChatFilterItem(const string& aNickMatch, const string& aTextMatch, StringMatch::Method aNickMethod,
			StringMatch::Method aTextMethod, bool aMainchat, bool aPM, bool aEnabled = true) : enabled(aEnabled), matchPM(aPM), matchMainchat(aMainchat)
		{
			nickMatcher.setMethod(aNickMethod);
			nickMatcher.pattern = aNickMatch;
			nickMatcher.prepare();

			textMatcher.setMethod(aTextMethod);
			textMatcher.pattern = aTextMatch;
			textMatcher.prepare();
		}
		~ChatFilterItem() = default;

		enum Context {
			PM, // Private chat
			MC, // Main chat
			ALL // Both
		};

		const string& getNickPattern() const { return nickMatcher.pattern; }
		const string&  getTextPattern() const { return textMatcher.pattern; }
		StringMatch::Method getNickMethod() const { return nickMatcher.getMethod(); }
		StringMatch::Method getTextMethod() const { return textMatcher.getMethod(); }

		bool match(const string& aNick, const string& aText, Context aContext) const noexcept {
			if (!getEnabled())
				return false;

			if ((aContext == PM && !matchPM) || (aContext == MC && !matchMainchat))
				return false;

			if (!nickMatcher.pattern.empty() && nickMatcher.match(aNick)) {
				//nick matched, match the text in case we just want to ignore some messages of the user
				return (textMatcher.pattern.empty() || textMatcher.match(aText));
			}
			//General text match ignore type, no nick pattern just match the text
			if (nickMatcher.pattern.empty() && !textMatcher.pattern.empty() && textMatcher.match(aText))
				return true;

			return false;
		}
		void updateItem(const string& aNickMatch, const string& aTextMatch, StringMatch::Method aNickMethod, StringMatch::Method aTextMethod) {
			nickMatcher.setMethod(aNickMethod);
			nickMatcher.pattern = aNickMatch;
			nickMatcher.prepare();

			textMatcher.setMethod(aTextMethod);
			textMatcher.pattern = aTextMatch;
			textMatcher.prepare();
		}

		GETSET(bool, enabled, Enabled)

		bool matchPM;
		bool matchMainchat;
	private:
		StringMatch nickMatcher;
		StringMatch textMatcher;
	};



	class IgnoreManager : public Speaker<IgnoreManagerListener>, public Singleton<IgnoreManager>, private SettingsManagerListener {

	public:
		using IgnoreMap = unordered_map<UserPtr, int, User::Hash>;

		IgnoreManager() noexcept;
		~IgnoreManager() noexcept;

		using UserSet = unordered_set<UserPtr, User::Hash>;
		using ChatFilterItemList = vector<ChatFilterItem>;

		IgnoreMap getIgnoredUsers() const noexcept;
		bool storeIgnore(const UserPtr& aUser) noexcept;
		bool removeIgnore(const UserPtr& aUser) noexcept;

		ChatFilterItemList& getIgnoreList() { return ChatFilterItems; }
		void replaceList(const ChatFilterItemList& newList) {
			ChatFilterItems = newList;
		}

		// save & load
		void save();
		void load();

	private:
		ActionHookResult<MessageHighlightList> onPrivateMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept;
		ActionHookResult<MessageHighlightList> onHubMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept;

		mutable SharedMutex cs;

		IgnoreMap ignoredUsers;
		bool checkIgnored(const OnlineUserPtr& aUser, bool aPM) noexcept;

		bool dirty = false;
		// contains the ignored nicks and patterns 
		ChatFilterItemList ChatFilterItems;

		ActionHookResult<MessageHighlightList> isIgnoredOrFiltered(const ChatMessagePtr& msg, const ActionHookResultGetter<MessageHighlightList>& aResultGetter, bool aPM) noexcept;

		// chat filter
		bool isChatFiltered(const string& aNick, const string& aText, ChatFilterItem::Context aContext = ChatFilterItem::ALL) const noexcept;

		// SettingsManagerListener
		void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept override;
		void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept override;
	};

}
#endif