/*
 * Copyright (C) 2012-2014 AirDC++ Project
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

#include "AirUtil.h"
#include "File.h"
#include "Localization.h"
#include "ResourceManager.h"
#include "SimpleXML.h"
#include "Util.h"

#include <boost/algorithm/string/replace.hpp>

static const char* countryNames[] = { "ANDORRA", "UNITED ARAB EMIRATES", "AFGHANISTAN", "ANTIGUA AND BARBUDA", 
"ANGUILLA", "ALBANIA", "ARMENIA", "NETHERLANDS ANTILLES", "ANGOLA", "ANTARCTICA", "ARGENTINA", "AMERICAN SAMOA", 
"AUSTRIA", "AUSTRALIA", "ARUBA", "ALAND", "AZERBAIJAN", "BOSNIA AND HERZEGOVINA", "BARBADOS", "BANGLADESH", 
"BELGIUM", "BURKINA FASO", "BULGARIA", "BAHRAIN", "BURUNDI", "BENIN", "BERMUDA", "BRUNEI DARUSSALAM", "BOLIVIA", 
"BRAZIL", "BAHAMAS", "BHUTAN", "BOUVET ISLAND", "BOTSWANA", "BELARUS", "BELIZE", "CANADA", "COCOS ISLANDS", 
"THE DEMOCRATIC REPUBLIC OF THE CONGO", "CENTRAL AFRICAN REPUBLIC", "CONGO", "SWITZERLAND", "COTE D'IVOIRE", "COOK ISLANDS", 
"CHILE", "CAMEROON", "CHINA", "COLOMBIA", "COSTA RICA", "SERBIA AND MONTENEGRO", "CUBA", "CAPE VERDE", 
"CHRISTMAS ISLAND", "CYPRUS", "CZECH REPUBLIC", "GERMANY", "DJIBOUTI", "DENMARK", "DOMINICA", "DOMINICAN REPUBLIC", 
"ALGERIA", "ECUADOR", "ESTONIA", "EGYPT", "WESTERN SAHARA", "ERITREA", "SPAIN", "ETHIOPIA", "EUROPEAN UNION", "FINLAND", "FIJI", 
"FALKLAND ISLANDS", "MICRONESIA", "FAROE ISLANDS", "FRANCE", "GABON", "UNITED KINGDOM", "GRENADA", "GEORGIA", 
"FRENCH GUIANA", "GUERNSEY", "GHANA", "GIBRALTAR", "GREENLAND", "GAMBIA", "GUINEA", "GUADELOUPE", "EQUATORIAL GUINEA", 
"GREECE", "SOUTH GEORGIA AND THE SOUTH SANDWICH ISLANDS", "GUATEMALA", "GUAM", "GUINEA-BISSAU", "GUYANA", 
"HONG KONG", "HEARD ISLAND AND MCDONALD ISLANDS", "HONDURAS", "CROATIA", "HAITI", "HUNGARY", 
"INDONESIA", "IRELAND", "ISRAEL",  "ISLE OF MAN", "INDIA", "BRITISH INDIAN OCEAN TERRITORY", "IRAQ", "IRAN", "ICELAND", 
"ITALY","JERSEY", "JAMAICA", "JORDAN", "JAPAN", "KENYA", "KYRGYZSTAN", "CAMBODIA", "KIRIBATI", "COMOROS", 
"SAINT KITTS AND NEVIS", "DEMOCRATIC PEOPLE'S REPUBLIC OF KOREA", "SOUTH KOREA", "KUWAIT", "CAYMAN ISLANDS", 
"KAZAKHSTAN", "LAO PEOPLE'S DEMOCRATIC REPUBLIC", "LEBANON", "SAINT LUCIA", "LIECHTENSTEIN", "SRI LANKA", 
"LIBERIA", "LESOTHO", "LITHUANIA", "LUXEMBOURG", "LATVIA", "LIBYAN ARAB JAMAHIRIYA", "MOROCCO", "MONACO", 
"MOLDOVA", "MONTENEGRO", "MADAGASCAR", "MARSHALL ISLANDS", "MACEDONIA", "MALI", "MYANMAR", "MONGOLIA", "MACAO", 
"NORTHERN MARIANA ISLANDS", "MARTINIQUE", "MAURITANIA", "MONTSERRAT", "MALTA", "MAURITIUS", "MALDIVES", 
"MALAWI", "MEXICO", "MALAYSIA", "MOZAMBIQUE", "NAMIBIA", "NEW CALEDONIA", "NIGER", "NORFOLK ISLAND", 
"NIGERIA", "NICARAGUA", "NETHERLANDS", "NORWAY", "NEPAL", "NAURU", "NIUE", "NEW ZEALAND", "OMAN", "PANAMA", 
"PERU", "FRENCH POLYNESIA", "PAPUA NEW GUINEA", "PHILIPPINES", "PAKISTAN", "POLAND", "SAINT PIERRE AND MIQUELON", 
"PITCAIRN", "PUERTO RICO", "PALESTINIAN TERRITORY", "PORTUGAL", "PALAU", "PARAGUAY", "QATAR", "REUNION", 
"ROMANIA", "SERBIA", "RUSSIAN FEDERATION", "RWANDA", "SAUDI ARABIA", "SOLOMON ISLANDS", "SEYCHELLES", "SUDAN", 
"SWEDEN", "SINGAPORE", "SAINT HELENA", "SLOVENIA", "SVALBARD AND JAN MAYEN", "SLOVAKIA", "SIERRA LEONE", 
"SAN MARINO", "SENEGAL", "SOMALIA", "SURINAME", "SAO TOME AND PRINCIPE", "EL SALVADOR", "SYRIAN ARAB REPUBLIC", 
"SWAZILAND", "TURKS AND CAICOS ISLANDS", "CHAD", "FRENCH SOUTHERN TERRITORIES", "TOGO", "THAILAND", "TAJIKISTAN", 
"TOKELAU", "TIMOR-LESTE", "TURKMENISTAN", "TUNISIA", "TONGA", "TURKEY", "TRINIDAD AND TOBAGO", "TUVALU", "TAIWAN", 
"TANZANIA", "UKRAINE", "UGANDA", "UNITED STATES MINOR OUTLYING ISLANDS", "UNITED STATES", "URUGUAY", "UZBEKISTAN", 
"VATICAN", "SAINT VINCENT AND THE GRENADINES", "VENEZUELA", "BRITISH VIRGIN ISLANDS", "U.S. VIRGIN ISLANDS", 
"VIET NAM", "VANUATU", "WALLIS AND FUTUNA", "SAMOA", "YEMEN", "MAYOTTE", "YUGOSLAVIA", "SOUTH AFRICA", "ZAMBIA", 
"ZIMBABWE" };

static const char* countryCodes[] = { 
 "AD", "AE", "AF", "AG", "AI", "AL", "AM", "AN", "AO", "AQ", "AR", "AS", "AT", "AU", "AW", "AX", "AZ", "BA", "BB", 
 "BD", "BE", "BF", "BG", "BH", "BI", "BJ", "BM", "BN", "BO", "BR", "BS", "BT", "BV", "BW", "BY", "BZ", "CA", "CC", 
 "CD", "CF", "CG", "CH", "CI", "CK", "CL", "CM", "CN", "CO", "CR", "CS", "CU", "CV", "CX", "CY", "CZ", "DE", "DJ", 
 "DK", "DM", "DO", "DZ", "EC", "EE", "EG", "EH", "ER", "ES", "ET", "EU", "FI", "FJ", "FK", "FM", "FO", "FR", "GA", 
 "GB", "GD", "GE", "GF", "GG", "GH", "GI", "GL", "GM", "GN", "GP", "GQ", "GR", "GS", "GT", "GU", "GW", "GY", "HK", 
 "HM", "HN", "HR", "HT", "HU", "ID", "IE", "IL", "IM", "IN", "IO", "IQ", "IR", "IS", "IT", "JE", "JM", "JO", "JP", 
 "KE", "KG", "KH", "KI", "KM", "KN", "KP", "KR", "KW", "KY", "KZ", "LA", "LB", "LC", "LI", "LK", "LR", "LS", "LT", 
 "LU", "LV", "LY", "MA", "MC", "MD", "ME", "MG", "MH", "MK", "ML", "MM", "MN", "MO", "MP", "MQ", "MR", "MS", "MT", 
 "MU", "MV", "MW", "MX", "MY", "MZ", "NA", "NC", "NE", "NF", "NG", "NI", "NL", "NO", "NP", "NR", "NU", "NZ", "OM", 
 "PA", "PE", "PF", "PG", "PH", "PK", "PL", "PM", "PN", "PR", "PS", "PT", "PW", "PY", "QA", "RE", "RO", "RS", "RU", 
 "RW", "SA", "SB", "SC", "SD", "SE", "SG", "SH", "SI", "SJ", "SK", "SL", "SM", "SN", "SO", "SR", "ST", "SV", "SY", 
 "SZ", "TC", "TD", "TF", "TG", "TH", "TJ", "TK", "TL", "TM", "TN", "TO", "TR", "TT", "TV", "TW", "TZ", "UA", "UG", 
 "UM", "US", "UY", "UZ", "VA", "VC", "VE", "VG", "VI", "VN", "VU", "WF", "WS", "YE", "YT", "YU", "ZA", "ZM", "ZW" };

namespace dcpp {

void Localization::Language::setLanguageFile() {
	SettingsManager::getInstance()->set(SettingsManager::LANGUAGE_FILE, getLanguageFilePath());
}

string Localization::Language::getLanguageFilePath() const {
	return isDefault() ? Util::emptyString : Util::getPath(Util::PATH_LOCALE) + locale + ".xml";
}

double Localization::Language::getLanguageVersion() {
	if (!Util::fileExists(getLanguageFilePath()))
		return 0;

	try {
		SimpleXML xml;
		xml.fromXML(File(getLanguageFilePath(), File::READ, File::OPEN).read());
		if (xml.findChild("Language")) {
			//xml.stepIn();
			auto version = xml.getIntChildAttrib(LANGVER_TAG);
			return version;
		}
	} catch(...) { }

	return 999;
}

vector<Localization::Language> Localization::languageList;
int Localization::curLanguage; //position of the current language in the list

void Localization::init() {
	// remove the file names at some point
	languageList.emplace_back("English", "GB", "en-US", Util::emptyString);
	languageList.emplace_back("Danish", "DK", "da-DK", "Danish_for_AirDC");
	languageList.emplace_back("Dutch", "NL", "nl-NL", "Dutch_for_AirDC");
	languageList.emplace_back("Finnish", "FI", "fi-FI", "Finnish_for_AirDC");
	languageList.emplace_back("French", "FR", "fr-FR", "French_for_AirDC");
	languageList.emplace_back("German", "DE", "de-DE", "German_for_AirDC");
	languageList.emplace_back("Hungarian", "HU", "hu-HU", "Hungarian_for_AirDC");
	languageList.emplace_back("Italian", "IT", "it-IT", "Italian_for_AirDC");
	languageList.emplace_back("Norwegian", "NO", "no-NO", "Norwegian_for_AirDC");
	languageList.emplace_back("Polish", "PL", "pl-PL", "Polish_for_AirDC");
	languageList.emplace_back("Portuguese", "PT", "pt-BR", "Port_Br_for_AirDC");
	languageList.emplace_back("Romanian", "RO", "ro-RO", "Romanian_for_AirDC");
	languageList.emplace_back("Russian", "RU", "ru-RU", "Russian_for_AirDC");
	languageList.emplace_back("Spanish", "ES", "es-ES", "Spanish_for_AirDC");
	languageList.emplace_back("Swedish", "SE", "sv-SE", "Swedish_for_AirDC");

	//sort(languageList.begin()+1, languageList.end(), Language::NameSort());

	curLanguage = 0;
	string langFile = SETTING(LANGUAGE_FILE);
	if (!langFile.empty()) {
		langFile = Util::getFileName(langFile);
		if (langFile.length() > 4) {
			langFile.erase(langFile.length()-4, 4);
			auto s = find_if(languageList.begin(), languageList.end(), [&langFile](const Language& aLang) { return aLang.locale == langFile || aLang.languageFile == langFile; });
			if (s != languageList.end()) {
				curLanguage = distance(languageList.begin(), s);
				if (curLanguage > 0 && !Util::fileExists(SETTING(LANGUAGE_FILE))) {
					//paths changed? reset the default path
					SettingsManager::getInstance()->set(SettingsManager::LANGUAGE_FILE, (*s).getLanguageFilePath());
				}
			} else {
				/* Not one of the predefined language files, add a custom list item */
				languageList.emplace_back("(Custom: " + langFile + ")", "", getSystemLocale(), langFile);
				curLanguage = languageList.size() - 1;
			}
		}
	}

	languageList.shrink_to_fit();
}

string Localization::getSystemLocale() {
#ifdef _WIN32
	TCHAR buf[512];
	GetUserDefaultLocaleName(buf, 512);
	return Text::fromT(buf);
#else
	return "en-US";
#endif
}

bool Localization::Language::isDefault() const {
	return locale == "en-US";
}

bool Localization::usingDefaultLanguage() {
	return languageList[curLanguage].isDefault();
}

double Localization::getCurLanguageVersion() {
	return languageList[curLanguage].getLanguageVersion();
}

string Localization::getCurLanguageFilePath() {
	return languageList[curLanguage].getLanguageFilePath();
}

string Localization::getCurLanguageFileName() {
	return languageList[curLanguage].locale + ".xml";
}

void Localization::setLanguage(int languageIndex) {
	if (languageIndex >= 0 && languageIndex < (int)languageList.size() && languageList[languageIndex].languageName != languageList[curLanguage].languageName) {
		curLanguage = languageIndex;
		languageList[curLanguage].setLanguageFile(); //update the language file in the settings
	}
}

void Localization::loadLanguage(int languageIndex) {
	if (languageIndex >= 0 && languageIndex < (int)languageList.size()) {
		ResourceManager::getInstance()->loadLanguage(languageList[languageIndex].getLanguageFilePath());
	}
}

string Localization::getCurrentLocale() {
	if (curLanguage > 0)
		return languageList[curLanguage].locale;
	else
		return getSystemLocale();
}

string Localization::getLanguageStr() {
	return languageList[curLanguage].languageName;
}

uint8_t Localization::getFlagIndexByName(const char* countryName) {
	// country codes are not sorted, use linear searching (it is not used so often)
	const char** first = countryNames;
	const char** last = countryNames + (sizeof(countryNames) / sizeof(countryNames[0]));
	const char** i = find_if(first, last, [&](const char* cn) { return Util::stricmp(countryName, cn) == 0; });
	if(i != last)
		return i - countryNames + 1;

	return 0;
}

uint8_t Localization::getFlagIndexByCode(const char* countryCode) {
	// country codes are sorted, use binary search for better performance
	int begin = 0;
	int end = (sizeof(countryCodes) / sizeof(countryCodes[0])) - 1;
		
	while(begin <= end) {
		int mid = (begin + end) / 2;
		int cmp = memcmp(countryCode, countryCodes[mid], 2);

		if(cmp > 0)
			begin = mid + 1;
		else if(cmp < 0)
			end = mid - 1;
		else
			return mid + 1;
	}
	return 0;
}

}