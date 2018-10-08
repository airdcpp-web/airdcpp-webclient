/*
 * Copyright (C) 2012-2018 AirDC++ Project
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
		class Language {
		public:
			Language() { }
			explicit Language(const string& aLanguage, const char* aCountryFlagCode, const string& aLocale) : languageName(aLanguage), 
				locale(aLocale), countryFlagCode(aCountryFlagCode) {
			}

			string getLanguageFilePath() const noexcept;
			string getLanguageSettingValue() const noexcept;
			double getLanguageVersion() const noexcept;
			bool isDefault() const noexcept;

			struct NameSort { 
				bool operator()(const Language& l1, const Language& l2) const noexcept;
			};

			const string& getLanguageName() const noexcept {
				return languageName;
			}

			const string& getLocale() const noexcept {
				return locale;
			}

			const char* getCountryFlagCode() const noexcept {
				return countryFlagCode;
			}
		private:
			string languageName, locale;
			const char* countryFlagCode;
		};

		typedef vector<Language> LanguageList;

		static string getSystemLocale() noexcept;
		static string getCurLanguageFilePath() noexcept;
		static double getCurLanguageVersion() noexcept;

		static string getLocale() noexcept;
		static string getCurLanguageLocale() noexcept;
		static string getCurLanguageName() noexcept;
		static optional<int> getLanguageIndex(const LanguageList& aLanguages) noexcept;
		static void init() noexcept;

		static uint8_t getFlagIndexByCode(const char* countryCode) noexcept;
		static uint8_t getFlagIndexByName(const char* countryName) noexcept;

		static bool usingDefaultLanguage() noexcept;
		static const LanguageList& getDefaultLanguages() noexcept;
		static LanguageList getLanguages() noexcept;

		static Language* getCurrentLanguage() noexcept;
	private:
		static vector<Language> languageList;
	};
}
#endif