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

#include "PreviewAppManager.h"

#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/core/io/xml/SimpleXML.h>


namespace dcpp {

PreviewAppManager::PreviewAppManager() {
	SettingsManager::getInstance()->addListener(this);
}

PreviewAppManager::~PreviewAppManager() {
	SettingsManager::getInstance()->removeListener(this);
	ranges::for_each(previewApplications, std::default_delete<PreviewApplication>());
}

void PreviewAppManager::loadPreview(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if (aXml.findChild("PreviewApps")) {
		aXml.stepIn();
		while (aXml.findChild("Application")) {
			addPreviewApp(aXml.getChildAttrib("Name"), aXml.getChildAttrib("Application"),
				aXml.getChildAttrib("Arguments"), aXml.getChildAttrib("Extension"));
		}
		aXml.stepOut();
	}
}

void PreviewAppManager::savePreview(SimpleXML& aXml) const noexcept {
	aXml.addTag("PreviewApps");
	aXml.stepIn();
	for (const auto& pa : previewApplications) {
		aXml.addTag("Application");
		aXml.addChildAttrib("Name", pa->getName());
		aXml.addChildAttrib("Application", pa->getApplication());
		aXml.addChildAttrib("Arguments", pa->getArguments());
		aXml.addChildAttrib("Extension", pa->getExtension());
	}
	aXml.stepOut();
}
void PreviewAppManager::on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
	try {
		loadPreview(xml);
	} catch (...) {

	}
}

void PreviewAppManager::on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
	savePreview(xml);
}

} // namespace dcpp
