/*
 * Copyright (C) 2011 AirDC++ Project
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

#ifndef LOCALIZATION_H
#define LOCALIZATION_H

#include "Util.h"
#include "SettingsManager.h"

namespace dcpp {


class Localization {
	
	public:
		struct Language {
			Language() { }
			explicit Language(const string& aLanguage, const char* aCountryFlagCode, const string& aLocale, const string& aLanguageFile) : languageName(aLanguage), 
				locale(aLocale), countryFlagCode(aCountryFlagCode), languageFile(aLanguageFile) { }


			const char* countryFlagCode;
			string languageName, locale, languageFile;

			void setLanguageFile() {
				if (languageName != "English")
					SettingsManager::getInstance()->set(SettingsManager::LANGUAGE_FILE, (Util::getPath(Util::PATH_GLOBAL_CONFIG) + "Language\\" + languageFile));
				else
					SettingsManager::getInstance()->set(SettingsManager::LANGUAGE_FILE, Util::emptyString);
			}
		};

		static int curLanguage;
		static vector<Language> languageList;

		static void setLanguage(int languageIndex);
		static int getLangIndex();

		static string getLocale();
		static void init();

		static uint8_t getFlagIndexByCode(const char* countryCode);
		static uint8_t getFlagIndexByName(const char* countryName);

		//static vector<Language> languageList;
	private:
		//static Language curLanguage;
	};
}
#endif