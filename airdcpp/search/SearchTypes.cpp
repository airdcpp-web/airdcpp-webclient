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

#include "stdinc.h"
#include <airdcpp/search/SearchTypes.h>

#include <airdcpp/hub/AdcHub.h>
#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/search/Search.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/util/text/StringTokenizer.h>
#include <airdcpp/util/ValueGenerator.h>

#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

ResourceManager::Strings SearchTypes::types[Search::TYPE_LAST] = {
	ResourceManager::ANY,
	ResourceManager::AUDIO,
	ResourceManager::COMPRESSED,
	ResourceManager::DOCUMENT,
	ResourceManager::EXECUTABLE,
	ResourceManager::PICTURE,
	ResourceManager::VIDEO,
	ResourceManager::DIRECTORY,
	ResourceManager::TTH_ROOT,
	ResourceManager::FILE
};

SearchType::SearchType(const string& aId, const string& aName, const StringList& aExtensions) :
	id(aId), name(aName) {

	setExtensions(aExtensions);
}

void SearchType::setExtensions(const StringList& aExtensions) noexcept {
	OrderedStringSet unique;
	for (const auto& ext : aExtensions) {
		unique.emplace(boost::trim_copy(ext));
	}

	extensions = StringList(unique.begin(), unique.end());
}

string SearchType::getDisplayName() const noexcept {
	return isDefault() ? SearchTypes::getTypeStr(id[0] - '0') : name;
}

bool SearchType::isDefault() const noexcept {
	return SearchTypes::isDefaultTypeStr(id);
}

Search::TypeModes SearchType::getTypeMode() const noexcept {
	if (!isDefault()) {
		// Custom search type
		return Search::TYPE_ANY;
	}

	return static_cast<Search::TypeModes>(id[0] - '0');
}

SearchTypes::SearchTypes(SearchTypeChangeHandler&& aSearchTypeChangeHandler) : onSearchTypesChanged(std::move(aSearchTypeChangeHandler)) {
	setSearchTypeDefaults();
	SettingsManager::getInstance()->addListener(this);
}

SearchTypes::~SearchTypes() {
	SettingsManager::getInstance()->removeListener(this);
}

const string& SearchTypes::getTypeStr(int aType) noexcept {
	return STRING_I(types[aType]);
}

bool SearchTypes::isDefaultTypeStr(const string& aType) noexcept {
	return aType.size() == 1 && aType[0] >= '0' && aType[0] <= '9';
}

void SearchTypes::validateSearchTypeName(const string& aName) {
	if (aName.empty() || isDefaultTypeStr(aName)) {
		throw SearchTypeException("Invalid search type name"); // TODO: localize
	}

	for (int type = Search::TYPE_ANY; type != Search::TYPE_LAST; ++type) {
		if (getTypeStr(type) == aName) {
			throw SearchTypeException("This search type already exists"); // TODO: localize
		}
	}
}

SearchTypeList SearchTypes::getSearchTypes() const noexcept {
	SearchTypeList ret;

	{
		RLock l(cs);
		ranges::copy(searchTypes | views::values, back_inserter(ret));
	}

	return ret;
}

void SearchTypes::setSearchTypeDefaults() {
	{
		WLock l(cs);
		searchTypes.clear();

		// for conveniency, the default search exts will be the same as the ones defined by SEGA.
		const auto& searchExts = AdcHub::getSearchExts();
		for (size_t i = 0, n = searchExts.size(); i < n; ++i) {
			const auto id = string(1, '1' + static_cast<char>(i));
			searchTypes[id] = make_shared<SearchType>(id, id, searchExts[i]);
		}
	}

	onSearchTypesChanged();
}

SearchTypePtr SearchTypes::addSearchType(const string& aName, const StringList& aExtensions) {
	validateSearchTypeName(aName);

	auto searchType = make_shared<SearchType>(Util::toString(ValueGenerator::rand()), aName, aExtensions);

	{
		WLock l(cs);
		searchTypes[searchType->getId()] = searchType;
	}

	onSearchTypesChanged();
	return searchType;
}

void SearchTypes::delSearchType(const string& aId) {
	validateSearchTypeName(aId);
	{
		WLock l(cs);
		searchTypes.erase(aId);
	}

	onSearchTypesChanged();
}

SearchTypePtr SearchTypes::modSearchType(const string& aId, const optional<string>& aName, const optional<StringList>& aExtensions) {
	auto type = getSearchType(aId);

	if (aName && !type->isDefault()) {
		type->setName(*aName);
	}

	if (aExtensions) {
		type->setExtensions(*aExtensions);
	}

	onSearchTypesChanged();
	return type;
}

SearchTypePtr SearchTypes::getSearchType(const string& aId) const {
	RLock l(cs);
	auto ret = searchTypes.find(aId);
	if(ret == searchTypes.end()) {
		throw SearchTypeException("No such search type"); // TODO: localize
	}
	return ret->second;
}

void SearchTypes::getSearchType(int aPos, Search::TypeModes& type_, StringList& extList_, string& typeId_) {
	// Any, directory or TTH
	if (aPos < 4) {
		if (aPos == 0) {
			typeId_ = SEARCH_TYPE_ANY;
			type_ = Search::TYPE_ANY;
		} else if (aPos == 1) {
			typeId_ = SEARCH_TYPE_DIRECTORY;
			type_ = Search::TYPE_DIRECTORY;
		} else if (aPos == 2) {
			typeId_ = SEARCH_TYPE_TTH;
			type_ = Search::TYPE_TTH;
		} else if (aPos == 3) {
			typeId_ = SEARCH_TYPE_FILE;
			type_ = Search::TYPE_FILE;
		}
		return;
	}

	{
		auto typeIndex = aPos - 4;
		int counter = 0;

		RLock l(cs);
		for (const auto& [_, type] : searchTypes) {
			if (counter++ == typeIndex) {
				type_ = type->getTypeMode();
				typeId_ = type->getId();
				extList_ = type->getExtensions();
				return;
			}
		}
	}

	throw SearchTypeException("No such search type"); 
}

void SearchTypes::getSearchType(const string& aId, Search::TypeModes& type_, StringList& extList_, string& name_) const {
	if (aId.empty())
		throw SearchTypeException("No such search type"); 

	// Any, directory or TTH
	if (aId[0] == SEARCH_TYPE_ANY[0] || aId[0] == SEARCH_TYPE_DIRECTORY[0] || aId[0] == SEARCH_TYPE_TTH[0]  || aId[0] == SEARCH_TYPE_FILE[0]) {
		type_ = static_cast<Search::TypeModes>(aId[0] - '0');
		name_ = getTypeStr(aId[0] - '0');
		return;
	}

	auto type = getSearchType(aId);
	extList_ = type->getExtensions();
	type_ = type->getTypeMode();
	name_ = type->getDisplayName();
}

string SearchTypes::getTypeIdByExtension(const string& aExtension, bool aDefaultsOnly) const noexcept {
	auto extensionLower = Text::toLower(aExtension);

	RLock l(cs);
	for (const auto& type : searchTypes | views::values) {
		if (aDefaultsOnly && !type->isDefault()) {
			continue;
		}

		auto i = ranges::find(type->getExtensions(), extensionLower);
		if (i != type->getExtensions().end()) {
			return type->getId();
		}
	}

	return Util::emptyString;
}


void SearchTypes::on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
	xml.addTag("SearchTypes");
	xml.stepIn();
	{
		RLock l(cs);
		for (const auto& t: searchTypes | views::values) {
			xml.addTag("SearchType", Util::toString(";", t->getExtensions()));
			xml.addChildAttrib("Id", t->getName());
			if (!t->isDefault()) {
				xml.addChildAttrib("UniqueId", t->getId());
			}
		}
	}
	xml.stepOut();
}

void SearchTypes::on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
	xml.resetCurrentChild();
	if(xml.findChild("SearchTypes")) {
		searchTypes.clear();
		xml.stepIn();
		while(xml.findChild("SearchType")) {
			const string& extensions = xml.getChildData();
			if(extensions.empty()) {
				continue;
			}
			const string& name = xml.getChildAttrib("Id");
			if(name.empty()) {
				continue;
			}

			auto id = xml.getChildAttrib("UniqueId");
			if (id.empty()) {
				// Legacy/default type
				id = name;
			}

			searchTypes[id] = make_shared<SearchType>(id, name, StringTokenizer<string>(extensions, ';').getTokens());
		}
		xml.stepOut();
	}
}

} // namespace dcpp
