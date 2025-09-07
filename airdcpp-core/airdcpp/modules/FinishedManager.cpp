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

#include "FinishedManager.h"

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/transfer/download/Download.h>
#include <airdcpp/transfer/upload/Upload.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/transfer/upload/UploadManager.h>

#include <airdcpp/events/LogManager.h>
#include <airdcpp/core/localization/ResourceManager.h>

namespace dcpp {


IncrementingIdCounter<FinishedItemToken> FinishedItem::idCounter;

FinishedItem::FinishedItem(string const& aTarget, const HintedUser& aUser, int64_t aSize, int64_t aSpeed, time_t aTime) :
	target(aTarget), user(aUser), size(aSize), avgSpeed(aSpeed), time(aTime), token(idCounter.next())
{
}

FinishedManager::FinishedManager() { 
	QueueManager::getInstance()->addListener(this);
	UploadManager::getInstance()->addListener(this);
}
	
FinishedManager::~FinishedManager() {
	QueueManager::getInstance()->removeListener(this);
	UploadManager::getInstance()->removeListener(this);
}

void FinishedManager::remove(const FinishedItemPtr& aItem) {
	{
		Lock l(cs);
		auto listptr = &uploads;
		auto it = find(listptr->begin(), listptr->end(), aItem);

		if(it != listptr->end())
			listptr->erase(it);
		else
			return;
	}
}
	
void FinishedManager::removeAll() {
	{
		Lock l(cs);
		auto listptr = &uploads;
		listptr->clear();
	}
}

void FinishedManager::on(UploadManagerListener::Complete, const Upload* u) noexcept
{
	if(u->getType() == Transfer::TYPE_FILE || (u->getType() == Transfer::TYPE_FULL_LIST && SETTING(LOG_FILELIST_TRANSFERS))) {
		auto item = std::make_shared<FinishedItem>(u->getPath(), u->getHintedUser(), u->getFileSize(), static_cast<int64_t>(u->getAverageSpeed()), GET_TIME());

		{
			Lock l(cs);
			uploads.push_back(item);
		}

		fire(FinishedManagerListener::AddedUl(), item);
		if(SETTING(SYSTEM_SHOW_UPLOADS)) {
			LogManager::getInstance()->message(
				STRING_F(FINISHED_UPLOAD, u->getPath() % ClientManager::getInstance()->getFormattedNicks(u->getHintedUser())), 
				LogMessage::SEV_INFO, 
				STRING(MENU_TRANSFERS)
			);		
		}
	}
}

} // namespace dcpp