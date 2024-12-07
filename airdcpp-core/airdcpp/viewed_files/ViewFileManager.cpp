/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#include <airdcpp/viewed_files/ViewFileManager.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/share/temp_share/TempShareManager.h>


namespace dcpp {
	ViewFileManager::ViewFileManager() noexcept {
		QueueManager::getInstance()->addListener(this);
	}

	ViewFileManager::~ViewFileManager() noexcept {
		QueueManager::getInstance()->removeListener(this);
	}

	void ViewFileManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
		LogManager::getInstance()->message(aMsg, aSeverity, STRING(FILES));
	}

	ViewFileManager::ViewFileList ViewFileManager::getFiles() const noexcept {
		ViewFileList ret;

		{
			RLock l(cs);
			ranges::copy(viewFiles | views::values, back_inserter(ret));
		}

		return ret;
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

	void ViewFileManager::on(QueueManagerListener::ItemTick, const QueueItemPtr& aQI) noexcept {
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

	ViewFilePtr ViewFileManager::createFile(const string& aFileName, const string& aPath, const TTHValue& aTTH, bool aIsText, bool aIsLocalFile) noexcept {
		auto file = std::make_shared<ViewFile>(aFileName, aPath, aTTH, aIsText, aIsLocalFile,
			std::bind_front(&ViewFileManager::onFileStateUpdated, this));

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

	ViewFilePtr ViewFileManager::addLocalFileThrow(const TTHValue& aTTH, bool aIsText) {
		if (getFile(aTTH)) {
			return nullptr;
		}

		UploadFileQuery query(aTTH);
		auto fileInfo = ShareManager::getInstance()->toRealWithSize(query);
		if (!fileInfo.found) {
			throw Exception(STRING(FILE_NOT_FOUND));
		}

		auto file = createFile(PathUtil::getFileName(fileInfo.path), fileInfo.path, aTTH, aIsText, true);

		fire(ViewFileManagerListener::FileFinished(), file);
		return file;
	}
	
	ViewFilePtr ViewFileManager::addUserFileHookedThrow(const ViewedFileAddData& aFileInfo) {
		if (ShareManager::getInstance()->isFileShared(aFileInfo.tth) || TempShareManager::getInstance()->isTempShared(nullptr, aFileInfo.tth)) {
			return addLocalFileThrow(aFileInfo.tth, aFileInfo.isText);
		}

		if (aFileInfo.user == ClientManager::getInstance()->getMe()) {
			throw Exception(STRING(NO_DOWNLOADS_FROM_SELF));
		}

		if (getFile(aFileInfo.tth)) {
			return nullptr;
		}

		auto qi = QueueManager::getInstance()->addOpenedItemHooked(aFileInfo, true);

		auto file = createFile(aFileInfo.file, qi->getTarget(), qi->getTTH(), aFileInfo.isText, false);
		if (file) {
			file->onAddedQueue(qi->getTarget(), qi->getSize());
		}

		return file;
	}

	ViewFilePtr ViewFileManager::addUserFileHookedNotify(const ViewedFileAddData& aFileInfo) noexcept {
		try {
			auto file = addUserFileHookedThrow(aFileInfo);
			if (file) {
				return file;
			}

			log(STRING_F(FILE_ALREADY_VIEWED, aFileInfo.file), LogMessage::SEV_NOTIFY);
		} catch (const Exception& e) {
			log(STRING_F(ADD_FILE_ERROR, aFileInfo.file % ClientManager::getInstance()->getFormattedNicks(aFileInfo.user) % e.getError()), LogMessage::SEV_NOTIFY);
		}

		return nullptr;
	}

	ViewFilePtr ViewFileManager::addLocalFileNotify(const TTHValue& aTTH, bool aIsText, const string& aFileName) noexcept {
		try {
			auto file = addLocalFileThrow(aTTH, aIsText);
			if (file) {
				return file;
			}

			log(STRING_F(FILE_ALREADY_VIEWED, aFileName), LogMessage::SEV_NOTIFY);
		} catch (const Exception& e) {
			log(STRING_F(FAILED_TO_OPEN_FILE, aFileName % e.getError()), LogMessage::SEV_NOTIFY);
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