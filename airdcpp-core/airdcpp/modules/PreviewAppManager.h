/*
* Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_PREVIEWAPP_MANAGER_H
#define DCPLUSPLUS_DCPP_PREVIEWAPP_MANAGER_H

#include <airdcpp/forward.h>
#include <airdcpp/GetSet.h>

#include <airdcpp/SettingsManagerListener.h>
#include <airdcpp/Singleton.h>

namespace dcpp {

	class PreviewApplication {
	public:
		typedef PreviewApplication* Ptr;
		typedef vector<Ptr> List;
		typedef List::const_iterator Iter;

		PreviewApplication() noexcept {}
		PreviewApplication(string n, string a, string r, string e) : name(n), application(a), arguments(r), extension(e) {};
		~PreviewApplication() noexcept { }

		GETSET(string, name, Name);
		GETSET(string, application, Application);
		GETSET(string, arguments, Arguments);
		GETSET(string, extension, Extension);
	};

	/**
	* Assumed to be called only by UI thread.
	*/
	class PreviewAppManager : public Singleton<PreviewAppManager>,
		private SettingsManagerListener
	{
	public:
		PreviewAppManager();
		~PreviewAppManager();

		PreviewApplication::List& getPreviewApps() noexcept { return previewApplications; }

		PreviewApplication* addPreviewApp(string name, string application, string arguments, string extension) {
			PreviewApplication* pa = new PreviewApplication(name, application, arguments, extension);
			previewApplications.push_back(pa);
			return pa;
		}

		PreviewApplication* removePreviewApp(unsigned int index) {
			if (previewApplications.size() > index)
				previewApplications.erase(previewApplications.begin() + index);
			return NULL;
		}

		PreviewApplication* getPreviewApp(unsigned int index, PreviewApplication &pa) {
			if (previewApplications.size() > index)
				pa = *previewApplications[index];
			return NULL;
		}

		PreviewApplication* updatePreviewApp(int index, PreviewApplication &pa) {
			*previewApplications[index] = pa;
			return NULL;
		}
	private:
		PreviewApplication::List previewApplications;

		// SettingsManagerListener
		void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
		void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;

		void loadPreview(SimpleXML& aXml);
		void savePreview(SimpleXML& aXml) const noexcept;
	};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)