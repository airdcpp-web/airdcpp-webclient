/* 
 * Copyright (C) 2001-2018 Jacek Sieka, arnetheduck on gmail point com
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

#include "FinishedManager.h"

#include <airdcpp/ClientManager.h>
#include <airdcpp/Download.h>
#include <airdcpp/Upload.h>
#include <airdcpp/QueueManager.h>
#include <airdcpp/UploadManager.h>

#include <airdcpp/LogManager.h>
#include <airdcpp/ResourceManager.h>

namespace dcpp {

#ifdef _WIN32
const tstring FinishedItem::getText(uint8_t col) const {
	dcassert(col >= 0 && col < COLUMN_LAST);
	switch(col) {
		case COLUMN_FILE: return Text::toT(Util::getFileName(getTarget()));
		case COLUMN_DONE: return Text::toT(Util::formatTime("%Y-%m-%d %H:%M:%S", getTime()));
		case COLUMN_PATH: return Text::toT(Util::getFilePath(getTarget()));
		case COLUMN_NICK: return Text::toT(ClientManager::getInstance()->getFormatedNicks(getUser()));
		case COLUMN_HUB: {
			if (getUser().user->isOnline()) {
				return Text::toT(ClientManager::getInstance()->getFormatedHubNames(getUser()));
			} else {
				auto ofu = ClientManager::getInstance()->getOfflineUser(getUser().user->getCID());
				return TSTRING(OFFLINE) + (ofu ? _T(" ( ") + Text::toT(ofu->getUrl()) + _T(" ) ") : _T(""));
			}
		}
		case COLUMN_SIZE: return Util::formatBytesW(getSize());
		case COLUMN_SPEED: return Util::formatBytesW(getAvgSpeed()) + _T("/s");
		case COLUMN_TYPE: {
			tstring filetype = Text::toT(Util::getFileExt(Text::fromT(getText(COLUMN_FILE))));
			if(!filetype.empty() && filetype[0] == _T('.'))
				filetype.erase(0, 1);
			return filetype;
		}
		default: return Util::emptyStringT;
	}
}
#endif

FinishedManager::FinishedManager() { 
	QueueManager::getInstance()->addListener(this);
	UploadManager::getInstance()->addListener(this);
}
	
FinishedManager::~FinishedManager() {
	QueueManager::getInstance()->removeListener(this);
	UploadManager::getInstance()->removeListener(this);

	Lock l(cs);
	for_each(uploads.begin(), uploads.end(), DeleteFunction());
}

void FinishedManager::remove(FinishedItemPtr item) {
	{
		Lock l(cs);
		FinishedItemList *listptr = &uploads;
		FinishedItemList::iterator it = find(listptr->begin(), listptr->end(), item);

		if(it != listptr->end())
			listptr->erase(it);
		else
			return;
	}
}
	
void FinishedManager::removeAll() {
	{
		Lock l(cs);
		FinishedItemList *listptr = &uploads;
		for_each(listptr->begin(), listptr->end(), DeleteFunction());
		listptr->clear();
	}
}

void FinishedManager::on(UploadManagerListener::Complete, const Upload* u) noexcept
{
	if(u->getType() == Transfer::TYPE_FILE || (u->getType() == Transfer::TYPE_FULL_LIST && SETTING(LOG_FILELIST_TRANSFERS))) {
		FinishedItemPtr item = new FinishedItem(u->getPath(), u->getHintedUser(),	u->getFileSize(), static_cast<int64_t>(u->getAverageSpeed()), GET_TIME());
		{
			Lock l(cs);
			uploads.push_back(item);
		}

		fire(FinishedManagerListener::AddedUl(), item);
		if(SETTING(SYSTEM_SHOW_UPLOADS)) {
			LogManager::getInstance()->message(STRING_F(FINISHED_UPLOAD, u->getPath() % ClientManager::getInstance()->getFormatedNicks(u->getHintedUser())), LogMessage::SEV_INFO);		
		}
	}
}

} // namespace dcpp