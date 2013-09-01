/* 
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include "ClientManager.h"
#include "FinishedManagerListener.h"
#include "Download.h"
#include "Upload.h"
#include "QueueManager.h"
#include "UploadManager.h"

#include "LogManager.h"
#include "ResourceManager.h"

namespace dcpp {

#ifdef _WIN32
const tstring FinishedItem::getText(uint8_t col) const {
	dcassert(col >= 0 && col < COLUMN_LAST);
	switch(col) {
		case COLUMN_FILE: return Text::toT(Util::getFileName(getTarget()));
		case COLUMN_DONE: return Text::toT(Util::formatTime("%Y-%m-%d %H:%M:%S", getTime()));
		case COLUMN_PATH: return Text::toT(Util::getFilePath(getTarget()));
		case COLUMN_NICK: return Text::toT(ClientManager::getInstance()->getFormatedNicks(getUser()));
		case COLUMN_HUB: return Text::toT(ClientManager::getInstance()->getFormatedHubNames(getUser()));
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
	for_each(downloads.begin(), downloads.end(), DeleteFunction());
	for_each(uploads.begin(), uploads.end(), DeleteFunction());
}

void FinishedManager::remove(FinishedItemPtr item, bool upload /* = false */) {
	{
		Lock l(cs);
		FinishedItemList *listptr = upload ? &uploads : &downloads;
		FinishedItemList::iterator it = find(listptr->begin(), listptr->end(), item);

		if(it != listptr->end())
			listptr->erase(it);
		else
			return;
	}
}
	
void FinishedManager::removeAll(bool upload /* = false */) {
	{
		Lock l(cs);
		FinishedItemList *listptr = upload ? &uploads : &downloads;
		for_each(listptr->begin(), listptr->end(), DeleteFunction());
		listptr->clear();
	}
}

void FinishedManager::on(QueueManagerListener::Finished, const QueueItemPtr& qi, const string&, const HintedUser& aUser, int64_t aSpeed) noexcept {
		
	if(!qi->isSet(QueueItem::FLAG_USER_LIST) || SETTING(LOG_FILELIST_TRANSFERS)) {
		
		FinishedItemPtr item = new FinishedItem(qi->getTarget(), aUser, qi->getSize(), static_cast<int64_t>(aSpeed), GET_TIME(), qi->getTTH().toBase32());
		{
			Lock l(cs);
			downloads.push_back(item);
		}
			
		fire(FinishedManagerListener::AddedDl(), item);
		if(SETTING(SYSTEM_SHOW_DOWNLOADS)) {
			LogManager::getInstance()->message(STRING_F(FINISHED_DOWNLOAD, qi->getTarget() % ClientManager::getInstance()->getFormatedNicks(aUser)), LogManager::LOG_INFO);
		}
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
			LogManager::getInstance()->message(STRING_F(FINISHED_UPLOAD, u->getPath() % ClientManager::getInstance()->getFormatedNicks(u->getHintedUser())), LogManager::LOG_INFO);		
		}
	}
}

bool FinishedManager::getTarget(const string& aTTH, string& target) {
	if(aTTH.empty()) 
		return false;

	{
		Lock l(cs);
		for(auto fi: downloads) {
			if(fi->getTTH() == aTTH) {
				target = fi->getTarget();
				return true;
			}
		}
	}

	return false;
}

bool FinishedManager::handlePartialRequest(const TTHValue& tth, vector<uint16_t>& outPartialInfo)
{
	string target;
	if(!getTarget(tth.toBase32(), target))
		return false;

	int64_t fileSize = File::getSize(target);

	if(fileSize < PARTIAL_SHARE_MIN_SIZE)
		return false;

	uint16_t len = TigerTree::calcBlocks(fileSize);
	outPartialInfo.push_back(0);
	outPartialInfo.push_back(len);

	return true;
}

} // namespace dcpp