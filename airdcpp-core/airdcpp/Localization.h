/*
 * Copyright (C) 2012-2017 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_LOCALIZATION_H
#define DCPLUSPLUS_DCPP_LOCALIZATION_H

#define LANGVER_TAG "Revision"

namespace dcpp {


class Localization {
	
	public:
		struct Language {
			Language() { }
			explicit Language(const string& aLanguage, const char* aCountryFlagCode, const string& aLocale, const string& aLanguageFile) : languageName(aLanguage), 
				locale(aLocale), languageFile(aLanguageFile), countryFlagCode(aCountryFlagCode) {
			}

			string languageName, locale, languageFile;
			const char* countryFlagCode;

			void setLanguageFile() noexcept;
			string getLanguageFilePath() const noexcept;
			double getLanguageVersion() const noexcept;
			bool isDefault() const noexcept;

			struct NameSort { 
				bool operator()(const Language& l1, const Language& l2) const noexcept;
			};
		};

		static string getSystemLocale() noexcept;
		static string getCurLanguageFilePath() noexcept;
		static string getCurLanguageFileName() noexcept;
		static double getCurLanguageVersion() noexcept;

		static vector<Language> languageList;

		static void setLanguage(int languageIndex) noexcept;
		static void loadLanguage(int languageIndex) noexcept;

		static string getLocale() noexcept;
		static string getCurLanguageLocale() noexcept;
		static string getCurLanguageName() noexcept;
		static int getCurLanguageIndex() noexcept;
		static void init() noexcept;

		static uint8_t getFlagIndexByCode(const char* countryCode) noexcept;
		static uint8_t getFlagIndexByName(const char* countryName) noexcept;

		static bool usingDefaultLanguage() noexcept;
	private:
		static int curLanguage;
	};
}
#endif