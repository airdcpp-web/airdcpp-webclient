/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include "ViewFileManager.h"
#include "QueueManager.h"

#include <boost/range/algorithm/copy.hpp>

namespace dcpp {

	//using boost::range::for_each;
	//using boost::range::find_if;


	ViewFileManager::ViewFileManager() noexcept {
		QueueManager::getInstance()->addListener(this);
	}

	ViewFileManager::~ViewFileManager() noexcept {
		QueueManager::getInstance()->removeListener(this);
	}

	ViewFileManager::ViewFileMap ViewFileManager::getFiles() const noexcept {
		RLock l(cs);
		return viewFiles;
	}

	void ViewFileManager::on(QueueManagerListener::Finished, const QueueItemPtr& aQI, const string&, const HintedUser&, int64_t /*aSpeed*/) noexcept {
		if (!isViewedItem(aQI))
			return;

		auto file = getFile(aQI->getTTH());
		if (file) {
			file->setTimeFinished(GET_TIME());

			file->onRemovedQueue(aQI->getTarget(), true);
			fire(ViewFileManagerListener::FileFinished(), file);
		}
	}

	bool ViewFileManager::isViewedItem(const QueueItemPtr& aQI) noexcept {
		return aQI->isSet(QueueItem::FLAG_CLIENT_VIEW) && !aQI->isSet(QueueItem::FLAG_USER_LIST) && !aQI->isSet(QueueItem::FLAG_OPEN);
	}

	void ViewFileManager::on(QueueManagerListener::Removed, const QueueItemPtr& aQI, bool finished) noexcept {
		if (finished || !isViewedItem(aQI)) {
			return;
		}

		removeFile(aQI->getTTH());
	}

	void ViewFileManager::on(QueueManagerListener::Added, QueueItemPtr& aQI) noexcept {
		if (!aQI->isSet(QueueItem::FLAG_CLIENT_VIEW) || aQI->isSet(QueueItem::FLAG_USER_LIST)) {
			return;
		}

		auto file = make_shared<ViewFile>(aQI->getTarget(), aQI->getTTH(), aQI->isSet(QueueItem::FLAG_TEXT), 
			std::bind(&ViewFileManager::onFileUpdated, this, std::placeholders::_1));

		{
			WLock l(cs);
			viewFiles[aQI->getTTH()] = file;
		}

		fire(ViewFileManagerListener::FileAdded(), file);
	}

	void ViewFileManager::onFileUpdated(const TTHValue& aTTH) noexcept {
		auto file = getFile(aTTH);
		if (file) {
			fire(ViewFileManagerListener::FileUpdated(), file);
		}
	}

	bool ViewFileManager::setRead(const TTHValue& aTTH) noexcept {
		auto file = getFile(aTTH);
		if (!file) {
			return false;
		}

		if (!file->getRead()) {
			file->setRead(true);
			fire(ViewFileManagerListener::FileRead(), file);
		}

		return true;
	}

	ViewFilePtr ViewFileManager::getFile(const TTHValue& aTTH) const noexcept {
		RLock l(cs);
		auto p = viewFiles.find(aTTH);
		if (p == viewFiles.end()) {
			return nullptr;
		}

		return p->second;
	}
	
	bool ViewFileManager::addFileThrow(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsText) throw(QueueException, FileException) {
		if (getFile(aTTH)) {
			return false;
		}

		QueueManager::getInstance()->addOpenedItem(aFileName, aSize, aTTH, aUser, true, aIsText);
		return true;
	}

	bool ViewFileManager::addFileNotify(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsText) noexcept {
		try {
			if (ViewFileManager::getInstance()->addFileThrow(aFileName, aSize, aTTH, aUser, true)) {
				return true;
			}

			LogManager::getInstance()->message(STRING_F(FILE_ALREADY_VIEWED, aFileName), LogMessage::SEV_NOTIFY);
		} catch (const Exception& e) {
			LogManager::getInstance()->message(STRING_F(ADD_FILE_ERROR, aFileName % ClientManager::getInstance()->getFormatedNicks(aUser) % e.getError()), LogMessage::SEV_NOTIFY);
		}

		return false;
	}

	bool ViewFileManager::removeFile(const TTHValue& aTTH) noexcept {
		ViewFilePtr f;

		auto file = getFile(aTTH);
		if (!file) {
			return false;
		}

		auto downloads = file->getDownloads();
		if (!downloads.empty()) {
			// It will come back here after being removed from the queue
			for (const auto& p : downloads) {
				QueueManager::getInstance()->removeFile(p);
			}
		} else {
			{
				WLock l(cs);
				viewFiles.erase(aTTH);
			}

			fire(ViewFileManagerListener::FileClosed(), file);
		}

		return true;
	}

} //dcpp