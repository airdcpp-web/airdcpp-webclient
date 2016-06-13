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

#include "ClientManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ShareManager.h"

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

	void ViewFileManager::on(QueueManagerListener::ItemFinished, const QueueItemPtr& aQI, const string&, const HintedUser&, int64_t /*aSpeed*/) noexcept {
		if (!isViewedItem(aQI))
			return;

		auto file = getFile(aQI->getTTH());
		if (file) {
			file->onRemovedQueue(aQI->getTarget(), true);
			fire(ViewFileManagerListener::FileFinished(), file);
		}
	}

	bool ViewFileManager::isViewedItem(const QueueItemPtr& aQI) noexcept {
		return aQI->isSet(QueueItem::FLAG_CLIENT_VIEW) && !aQI->isSet(QueueItem::FLAG_USER_LIST) && !aQI->isSet(QueueItem::FLAG_OPEN);
	}

	void ViewFileManager::on(QueueManagerListener::ItemStatusUpdated, const QueueItemPtr& aQI) noexcept {
		if (!isViewedItem(aQI)) {
			return;
		}

		auto file = getFile(aQI->getTTH());
		if (file) {
			file->onProgress(aQI->getTarget(), aQI->getDownloadedBytes());
		}
	}

	void ViewFileManager::on(QueueManagerListener::ItemRemoved, const QueueItemPtr& aQI, bool finished) noexcept {
		if (finished || !isViewedItem(aQI)) {
			return;
		}

		removeFile(aQI->getTTH());
	}

	void ViewFileManager::on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept {
		if (!isViewedItem(aQI)) {
			return;
		}

		auto file = createFile(aQI->getTarget(), aQI->getTTH(), aQI->isSet(QueueItem::FLAG_TEXT), false);
		if (file) {
			file->onAddedQueue(aQI->getTarget(), aQI->getSize());
		}
	}

	ViewFilePtr ViewFileManager::createFile(const string& aFileName, const TTHValue& aTTH, bool aIsText, bool aIsLocalFile) noexcept {
		auto file = std::make_shared<ViewFile>(aFileName, aTTH, aIsText, aIsLocalFile,
			std::bind(&ViewFileManager::onFileStateUpdated, this, std::placeholders::_1));

		{
			WLock l(cs);
			viewFiles[aTTH] = file;
		}

		fire(ViewFileManagerListener::FileAdded(), file);
		return file;
	}

	void ViewFileManager::onFileStateUpdated(const TTHValue& aTTH) noexcept {
		auto file = getFile(aTTH);
		if (file) {
			fire(ViewFileManagerListener::FileStateUpdated(), file);
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

	bool ViewFileManager::addLocalFile(const string& aPath, const TTHValue& aTTH, bool aIsText) noexcept {
		if (getFile(aTTH)) {
			return false;
		}

		auto file = createFile(aPath, aTTH, aIsText, true);

		fire(ViewFileManagerListener::FileFinished(), file);
		return true;
	}
	
	bool ViewFileManager::addUserFileThrow(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsText) throw(QueueException, FileException) {
		if (aUser == ClientManager::getInstance()->getMe()) {
			auto paths = ShareManager::getInstance()->getRealPaths(aTTH);
			if (!paths.empty()) {
				return addLocalFile(paths.front(), aTTH, aIsText);
			}

			return false;
		}

		if (getFile(aTTH)) {
			return false;
		}

		QueueManager::getInstance()->addOpenedItem(aFileName, aSize, aTTH, aUser, true, aIsText);
		return true;
	}

	bool ViewFileManager::addUserFileNotify(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsText) noexcept {
		try {
			if (addUserFileThrow(aFileName, aSize, aTTH, aUser, aIsText)) {
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

		// In case it hasn't been removed yet
		QueueManager::getInstance()->removeFile(file->getPath());

		{
			WLock l(cs);
			viewFiles.erase(aTTH);
		}

		fire(ViewFileManagerListener::FileClosed(), file);

		return true;
	}

} //dcpp