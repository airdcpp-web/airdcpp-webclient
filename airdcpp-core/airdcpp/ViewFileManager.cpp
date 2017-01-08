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

	ViewFilePtr ViewFileManager::addLocalFile(const TTHValue& aTTH, bool aIsText) noexcept {
		if (getFile(aTTH)) {
			return nullptr;
		}

		auto paths = ShareManager::getInstance()->getRealPaths(aTTH);
		if (paths.empty()) {
			return nullptr;
		}

		auto file = createFile(paths.front(), aTTH, aIsText, true);

		fire(ViewFileManagerListener::FileFinished(), file);
		return file;
	}
	
	ViewFilePtr ViewFileManager::addUserFileThrow(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsText) {
		if (ShareManager::getInstance()->isFileShared(aTTH)) {
			return addLocalFile(aTTH, aIsText);
		}

		if (aUser == ClientManager::getInstance()->getMe()) {
			return nullptr;
		}

		if (getFile(aTTH)) {
			return nullptr;
		}

		auto qi = QueueManager::getInstance()->addOpenedItem(aFileName, aSize, aTTH, aUser, true, aIsText);

		auto file = createFile(qi->getTarget(), qi->getTTH(), qi->isSet(QueueItem::FLAG_TEXT), false);
		if (file) {
			file->onAddedQueue(qi->getTarget(), qi->getSize());
		}

		return file;
	}

	ViewFilePtr ViewFileManager::addUserFileNotify(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsText) noexcept {
		try {
			auto file = addUserFileThrow(aFileName, aSize, aTTH, aUser, aIsText);
			if (file) {
				return file;
			}

			LogManager::getInstance()->message(STRING_F(FILE_ALREADY_VIEWED, aFileName), LogMessage::SEV_NOTIFY);
		} catch (const Exception& e) {
			LogManager::getInstance()->message(STRING_F(ADD_FILE_ERROR, aFileName % ClientManager::getInstance()->getFormatedNicks(aUser) % e.getError()), LogMessage::SEV_NOTIFY);
		}

		return nullptr;
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