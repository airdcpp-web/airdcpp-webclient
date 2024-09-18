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
#include "UploadBundleInfoSender.h"

#include "Bundle.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "Download.h"
#include "DownloadManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "SearchManager.h"
#include "UploadBundleInfo.h"
#include "UserConnection.h"

namespace dcpp {

const auto ENABLE_DEBUG = false;


const string UploadBundleInfoSender::FEATURE_ADC_UBN1 = "UBN1";
	
UploadBundleInfoSender::UploadBundleInfoSender() noexcept {
	DownloadManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);

	if (SETTING(USE_UPLOAD_BUNDLES)) {
		ConnectionManager::getInstance()->userConnectionSupports.add(FEATURE_ADC_UBN1);
	}

	SettingsManager::getInstance()->registerChangeHandler({
		SettingsManager::USE_UPLOAD_BUNDLES,
	}, [this](auto ...) {
		if (SETTING(USE_UPLOAD_BUNDLES)) {
			ConnectionManager::getInstance()->userConnectionSupports.add(FEATURE_ADC_UBN1);
		} else {
			ConnectionManager::getInstance()->userConnectionSupports.remove(FEATURE_ADC_UBN1);
		}
	});
}

UploadBundleInfoSender::~UploadBundleInfoSender() noexcept {
	DownloadManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
}

void UploadBundleInfoSender::dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
	if (ENABLE_DEBUG) {
		LogManager::getInstance()->message(aMsg, aSeverity, "UBN (sender)");
	} else if (aSeverity == LogMessage::SEV_WARNING || aSeverity == LogMessage::SEV_ERROR) {
#ifdef _DEBUG
		LogManager::getInstance()->message(aMsg, aSeverity, "UBN (sender)");
		dcdebug("UBN (sender): %s\n", aMsg.c_str());
#endif
	}
}


void UploadBundleInfoSender::on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept {
	auto ubnBundle = findInfoByBundleToken(aBundle->getToken());
	if (!ubnBundle) {
		return;
	}

	dbgMsg("send size update for bundle " + ubnBundle->getBundle()->getName(), LogMessage::SEV_VERBOSE);
	ubnBundle->sendSizeUpdate();
}

void UploadBundleInfoSender::on(DownloadManagerListener::Starting, const Download* aDownload) noexcept {
	if (!aDownload->getUserConnection().getSupports().includes(FEATURE_ADC_UBN1)) {
		return;
	}

	auto bundle = aDownload->getBundle();
	if (!bundle) {
		// Existing bundle connection being used for non-bundle files (or filelists)?
		WLock l(cs);
		if (auto i = connectionTokenMap.find(aDownload->getConnectionToken()); i != connectionTokenMap.end()) {
			removeRunningUserUnsafe(i->second, &aDownload->getUserConnection(), true);
			dbgMsg("no new bundle for a connection " + aDownload->getConnectionToken() + ", previously " + i->second->getBundle()->getName(), LogMessage::SEV_VERBOSE);
			connectionTokenMap.erase(i);
		}

		return;
	}

	// Get/create bundle info
	auto ubnBundle = findInfoByBundleToken(bundle->getToken());
	if (!ubnBundle) {
		ubnBundle = make_shared<UBNBundle>(
			bundle, 
			[this](auto... args) { sendUpdate(args...); },
			bind_front(&UploadBundleInfoSender::dbgMsg, this)
		);

		{
			WLock l(cs);
			bundleTokenMap[bundle->getToken()] = ubnBundle;
		}

		dbgMsg("created a new info " + bundle->getName() + " for a connection " + aDownload->getConnectionToken(), LogMessage::SEV_VERBOSE);
	} else {
		dbgMsg("found an existing info " + bundle->getName() + " for a connection " + aDownload->getConnectionToken(), LogMessage::SEV_VERBOSE);
	}

	{
		WLock l(cs);
		auto i = connectionTokenMap.find(aDownload->getConnectionToken());
		if (i != connectionTokenMap.end()) {
			// Existing bundle connection being moved to an existing bundle?
			if (i->second != ubnBundle) {
				removeRunningUserUnsafe(i->second, &aDownload->getUserConnection(), false);
				addRunningUserUnsafe(ubnBundle, &aDownload->getUserConnection());
				dbgMsg("moved connection " + aDownload->getConnectionToken() + " to an info " + bundle->getName() + ", previously in " + i->second->getBundle()->getName(), LogMessage::SEV_VERBOSE);
			}
		} else {
			// New bundle connection
			addRunningUserUnsafe(ubnBundle, &aDownload->getUserConnection());
		}
	}
}

void UploadBundleInfoSender::on(DownloadManagerListener::Idle, const UserConnection* aSource, const string&) noexcept {
	removeRunningUser(aSource, false);
}

void UploadBundleInfoSender::on(DownloadManagerListener::Remove, const UserConnection* aSource) noexcept {
	removeRunningUser(aSource, false);
}

void UploadBundleInfoSender::on(DownloadManagerListener::Failed, const Download* aDownload, const string&) noexcept {
	if (!aDownload->getBundle()) {
		return;
	}

	removeRunningUser(&aDownload->getUserConnection(), false);
}

void UploadBundleInfoSender::addRunningUserUnsafe(const UBNBundle::Ptr& aBundle, const UserConnection* aSource) noexcept {
	aBundle->addRunningUser(aSource);
	connectionTokenMap[aSource->getToken()] = aBundle;
}

void UploadBundleInfoSender::removeRunningUserUnsafe(const UBNBundle::Ptr& aBundle, const UserConnection* aSource, bool aSendRemove) noexcept {
	if (aBundle->removeRunningUser(aSource, aSendRemove)) {
		dbgMsg("removed connection " + aSource->getToken() + " from an info " + aBundle->getBundle()->getName() + " (no bundle connections remaining)", LogMessage::SEV_VERBOSE);
		bundleTokenMap.erase(aBundle->getBundle()->getToken());
	} else {
		dbgMsg("removed connection " + aSource->getToken() + " from an info " + aBundle->getBundle()->getName() + " (bundle connections remain)", LogMessage::SEV_VERBOSE);
	}
}

void UploadBundleInfoSender::removeRunningUser(const UserConnection* aSource, bool aSendRemove) noexcept {
	if (!aSource->getSupports().includes(FEATURE_ADC_UBN1)) {
		return;
	}

	auto ubnBundle = findInfoByConnectionToken(aSource->getToken());
	if (!ubnBundle) {
		// Non-bundle download
		return;
	}

	{
		WLock l(cs);
		removeRunningUserUnsafe(ubnBundle, aSource, aSendRemove);
		connectionTokenMap.erase(aSource->getToken());
	}
}


void UploadBundleInfoSender::on(DownloadManagerListener::BundleTick, const BundleList& aBundles, uint64_t) noexcept {
	for (const auto& b: aBundles) {
		auto ubnBundle = findInfoByBundleToken(b->getToken());
		if (!ubnBundle) {
			continue;
		}

		ubnBundle->onDownloadTick();
	}
}

UploadBundleInfoSender::UBNBundle::Ptr UploadBundleInfoSender::findInfoByBundleToken(QueueToken aBundleToken) const noexcept {
	RLock l(cs);
	auto i = bundleTokenMap.find(aBundleToken);
	return i != bundleTokenMap.end() ? i->second : nullptr;
}

UploadBundleInfoSender::UBNBundle::Ptr UploadBundleInfoSender::findInfoByConnectionToken(const string& aDownloadToken) const noexcept {
	RLock l(cs);
	auto i = connectionTokenMap.find(aDownloadToken);
	return i != connectionTokenMap.end() ? i->second : nullptr;
}

string UploadBundleInfoSender::UBNBundle::formatSpeed(int64_t aSpeed) noexcept {
	char buf[64];
	if (aSpeed < 1024) {
		snprintf(buf, sizeof(buf), "%d%s", (int)(aSpeed & 0xffffffff), "b");
	} else if (aSpeed < 1048576) {
		snprintf(buf, sizeof(buf), "%.02f%s", (double)aSpeed / (1024.0), "k");
	} else {
		snprintf(buf, sizeof(buf), "%.02f%s", (double)aSpeed / (1048576.0), "m");
	}
	return buf;
}

void UploadBundleInfoSender::UBNBundle::getTickParams(string& percent_, string& speedStr_) noexcept {
	auto speed = bundle->getSpeed();
	if (abs(speed - lastSpeed) > (lastSpeed / 10)) {
		//LogManager::getInstance()->message("SEND SPEED: " + Util::toString(abs(speed-lastSpeed)) + " is more than " + Util::toString(lastSpeed / 10));
		speedStr_ = formatSpeed(speed);
		lastSpeed = speed;
	} else {
		//LogManager::getInstance()->message("DON'T SEND SPEED: " + Util::toString(abs(speed-lastSpeed)) + " is less than " + Util::toString(lastSpeed / 10));
	}

	auto downloadedBytes = bundle->getDownloadedBytes();
	if (abs(lastDownloaded - downloadedBytes) > (bundle->getSize() / 200)) {
		//LogManager::getInstance()->message("SEND PERCENT: " + Util::toString(abs(lastDownloaded-getDownloadedBytes())) + " is more than " + Util::toString(size / 200));
		auto percent = bundle->getPercentage(downloadedBytes);
		dcassert(percent <= 100.00);
		percent_ = Util::toString(percent);
		lastDownloaded = downloadedBytes;
	} else {
		//LogManager::getInstance()->message("DON'T SEND PERCENT: " + Util::toString(abs(lastDownloaded-getDownloadedBytes())) + " is less than " + Util::toString(size / 200));
	}
}

void UploadBundleInfoSender::UBNBundle::onDownloadTick() noexcept {
	if (singleUser || uploadReports.empty()) {
		return;
	}

	string speedStr, percentStr;
	getTickParams(speedStr, speedStr);

	if (!speedStr.empty() || !percentStr.empty()) {
		for (const auto& user: uploadReports | views::keys) {
			auto cmd = getTickCommand(percentStr, speedStr);
			sendUpdate(cmd, user);
		}
	}
}

bool UploadBundleInfoSender::UBNBundle::addRunningUser(const UserConnection* aSource) noexcept {
	bool newBundle = true;
	if (auto y = uploadReports.find(aSource->getUser()); y == uploadReports.end()) {
		if (uploadReports.size() == 1) {
			setUserMode(false);
		}
	} else {
		dcassert(!y->second.contains(aSource->getToken()));
		newBundle = false;
	}

	uploadReports[aSource->getUser()].insert(aSource->getToken());

	// Tell the uploader to connect this token to a correct bundle
	auto cmd = getAddCommand(aSource->getToken(), newBundle);
	debugMsg("sending add command for info " + bundle->getName() + " (" + string(newBundle ? "complete" : "connect only") + "), connection " + aSource->getToken(), LogMessage::SEV_VERBOSE);
	sendUpdate(cmd, aSource->getUser());
	if (newBundle) {
		//add a new upload report
		if (!uploadReports.empty()) {
			lastSpeed = 0;
			lastDownloaded = 0;
		}

		return true;
	}

	return false;
}

bool UploadBundleInfoSender::UBNBundle::removeRunningUser(const UserConnection* aSource, bool aSendRemove) noexcept {
	bool finished = false;
	auto y = uploadReports.find(aSource->getUser());
	dcassert(y != uploadReports.end());
	if (y != uploadReports.end()) {
		dcassert(y->second.contains(aSource->getToken()));
		y->second.erase(aSource->getToken());
		if (y->second.empty()) {
			uploadReports.erase(aSource->getUser());
			if (uploadReports.size() == 1) {
				setUserMode(true);
			}

			finished = true;
		}

		if (finished || aSendRemove) {
			debugMsg("sending " + string(finished ? "finished" : "removal") + " command for info " + bundle->getName() + ", connection " + aSource->getToken(), LogMessage::SEV_VERBOSE);
			auto cmd = finished ? getBundleFinishedCommand() : getRemoveCommand(aSource->getToken());
			sendUpdate(cmd, aSource->getUser());
		}
	}

	return uploadReports.empty();
}

AdcCommand UploadBundleInfoSender::UBNBundle::getBundleFinishedCommand() const noexcept {
	AdcCommand cmd(UploadBundleInfo::CMD_UBD, AdcCommand::TYPE_UDP);
	cmd.addParam("BU", bundle->getStringToken());
	cmd.addParam("FI1");
	return cmd;
}

AdcCommand UploadBundleInfoSender::UBNBundle::getRemoveCommand(const string& aConnectionToken) const noexcept {
	AdcCommand cmd(UploadBundleInfo::CMD_UBD, AdcCommand::TYPE_UDP);
	cmd.addParam("TO", aConnectionToken);
	cmd.addParam("RM1");
	return cmd;
}

AdcCommand UploadBundleInfoSender::UBNBundle::getUserModeCommand() const noexcept {
	AdcCommand cmd(UploadBundleInfo::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("BU", bundle->getStringToken());
	cmd.addParam("UD1");
	if (singleUser) {
		cmd.addParam("SU1");
		cmd.addParam("DL", Util::toString(bundle->getDownloadedBytes()));
	} else {
		cmd.addParam("MU1");
	}

	return cmd;
}

AdcCommand UploadBundleInfoSender::UBNBundle::getBundleSizeUpdateCommand() const noexcept {
	AdcCommand cmd(UploadBundleInfo::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("BU", bundle->getStringToken());
	cmd.addParam("SI", Util::toString(bundle->getSize()));
	cmd.addParam("UD1");
	return cmd;
}

AdcCommand UploadBundleInfoSender::UBNBundle::getAddCommand(const string& aConnectionToken, bool aNewBundle) const noexcept {
	AdcCommand cmd(UploadBundleInfo::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("TO", aConnectionToken);
	cmd.addParam("BU", bundle->getStringToken());
	if (aNewBundle) {
		cmd.addParam("SI", Util::toString(bundle->getSize()));
		cmd.addParam("NA", bundle->getName());
		cmd.addParam("DL", Util::toString(bundle->getDownloadedBytes()));
		cmd.addParam(singleUser ? "SU1" : "MU1");
		cmd.addParam("AD1");
	} else {
		cmd.addParam("CH1");
	}

	return cmd;
}

AdcCommand UploadBundleInfoSender::UBNBundle::getTickCommand(const string& aPercent, const string& aSpeed) const noexcept {
	AdcCommand cmd(UploadBundleInfo::CMD_UBN, AdcCommand::TYPE_UDP);

	cmd.addParam("BU", bundle->getStringToken());
	if (!aSpeed.empty())
		cmd.addParam("DS", aSpeed);

	if (!aPercent.empty())
		cmd.addParam("PE", aPercent);

	return cmd;
}

void UploadBundleInfoSender::UBNBundle::setUserMode(bool aSetSingleUser) noexcept {
	if (aSetSingleUser) {
		lastSpeed = 0;
		lastDownloaded = 0;
		singleUser = true;
		debugMsg("sending enable single user mode for info " + bundle->getName(), LogMessage::SEV_VERBOSE);
	} else {
		singleUser = false;
		debugMsg("sending disabling single user mode for info " + bundle->getName(), LogMessage::SEV_VERBOSE);
	}

	if (!uploadReports.empty()) {
		const auto& u = uploadReports.begin()->first;

		auto cmd = getUserModeCommand();
		sendUpdate(cmd, u);
	}
}

void UploadBundleInfoSender::sendUpdate(AdcCommand& aCmd, const UserPtr& aUser) noexcept {
	// Send in a different thread as most calls are fired from inside a (locked) listener
	SearchManager::getInstance()->getUdpServer().addTask([=] {
		auto cmd = aCmd;
		ClientManager::getInstance()->sendUDPHooked(cmd, aUser->getCID(), true, true);
	});
}

void UploadBundleInfoSender::UBNBundle::sendSizeUpdate() const noexcept {
	for (const auto& user: uploadReports | views::keys) {
		auto cmd = getBundleSizeUpdateCommand();
		sendUpdate(cmd, user);
	}
}

}
