/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SEARCH_TYPES_H
#define DCPLUSPLUS_DCPP_SEARCH_TYPES_H

#include "ResourceManager.h"
#include "SettingsManagerListener.h"

#include "CriticalSection.h"
#include "GetSet.h"
#include "Search.h"


namespace dcpp {

#define SEARCH_TYPE_ANY "0"
#define SEARCH_TYPE_DIRECTORY "7"
#define SEARCH_TYPE_TTH "8"
#define SEARCH_TYPE_FILE "9"

class SocketException;

class SearchType {
public:
	SearchType(const string& aId, const string& aName, const StringList& aExtensions) :
		id(aId), name(aName), extensions(aExtensions) {

	}

	string getDisplayName() const noexcept;
	bool isDefault() const noexcept;
	Search::TypeModes getTypeMode() const noexcept;

	GETSET(string, id, Id);
	GETSET(string, name, Name);
	GETSET(StringList, extensions, Extensions);
};

class SearchTypes: private SettingsManagerListener
{
public:
	using SearchTypeMap = map<string, SearchTypePtr>;

	using SearchTypeChangeHandler = std::function<void ()>;

	static const string& getTypeStr(int aType) noexcept;
	static bool isDefaultTypeStr(const string& aType) noexcept;

	// Search types
	static void validateSearchTypeName(const string& aName);
	void setSearchTypeDefaults();
	SearchTypePtr addSearchType(const string& aName, const StringList& aExtensions);
	void delSearchType(const string& aId);
	SearchTypePtr modSearchType(const string& aId, const optional<string>& aName, const optional<StringList>& aExtensions);

	SearchTypeList getSearchTypes() const noexcept;

	void getSearchType(int aPos, Search::TypeModes& type_, StringList& extList_, string& typeId_);
	void getSearchType(const string& aId, Search::TypeModes& type_, StringList& extList_, string& name_) const;

	SearchTypePtr getSearchType(const string& aId) const;
	string getTypeIdByExtension(const string& aExtension, bool aDefaultsOnly = false) const noexcept;

	explicit SearchTypes(SearchTypeChangeHandler&& aSearchTypeChangeHandler);
	~SearchTypes() final;
private:
	static ResourceManager::Strings types[Search::TYPE_LAST];

	mutable SharedMutex cs;

	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept override;
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept override;

	const SearchTypeChangeHandler onSearchTypesChanged;

	// Search types
	SearchTypeMap searchTypes; // name, extlist
};

} // namespace dcpp

#endif // !defined(SEARCH_MANAGER_H)