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

#include <semver/semver.hpp>

#include <web-server/NpmRepository.h>

#include <airdcpp/connection/http/HttpDownload.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/core/thread/Thread.h>
#include <airdcpp/util/Util.h>


namespace webserver {
	const string NpmRepository::repository = "npm";

	NpmRepository::NpmRepository(InstallF&& aInstallF, ModuleLogger&& aLoggerF) : installF(std::move(aInstallF)), loggerF(std::move(aLoggerF)) {
		
	}

	NpmRepository::~NpmRepository() {
		for (;;) {
			{
				RLock l(cs);
				if (httpDownloads.empty()) {
					break;
				}
			}

			Thread::sleep(50);
		}
	}

	void NpmRepository::checkUpdates(const string& aName, const string& aCurrentVersion) noexcept {
		// https://github.com/npm/registry/blob/master/docs/REGISTRY-API.md#getpackage
		const auto url = "https://registry.npmjs.org/" + aName;

		HttpOptions options;
		options.setHeaders(StringPairList({
			{ "Accept", "application/vnd.npm.install-v1+json" },
		}));

		WLock l(cs);
		httpDownloads.try_emplace(aName, make_shared<HttpDownload>(url, [this, aName, aCurrentVersion]() {
			onPackageInfoDownloaded(aName, aCurrentVersion);
		}, options));
	}

	void NpmRepository::install(const string& aName) noexcept {
		checkUpdates(aName, Util::emptyString);
	}

	void NpmRepository::onPackageInfoDownloaded(const string& aName, const string& aCurrentVersion) noexcept {
		// Don't allow the same download to be initiated again until the installation has finished
		ScopedFunctor([&]() {
			WLock l(cs);
			httpDownloads.erase(aName);
		});

		HttpDownloadMap::mapped_type download = nullptr;

		// Get the download
		{
			WLock l(cs);
			auto i = httpDownloads.find(aName);
			if (i == httpDownloads.end()) {
				dcassert(0);
				return;
			}

			download = i->second;
		}

		if (download->buf.empty()) {
			loggerF(STRING_F(WEB_EXTENSION_UPDATE_CHECK_FAILED, aName % download->status), LogMessage::SEV_ERROR);
			return;
		}

		try {
			checkPackageData(download->buf, aName, aCurrentVersion);
		} catch (const std::exception& e) {
			loggerF(STRING_F(WEB_EXTENSION_UPDATE_CHECK_FAILED, aName % e.what()), LogMessage::SEV_ERROR);
		}
	}

	// https://github.com/npm/registry/blob/master/docs/responses/package-metadata.md
	void NpmRepository::checkPackageData(const string& aPackageData, const string& aName, const string& aCurrentVersion) const {
		optional<semver::version> curSemver = !aCurrentVersion.empty() ? optional(semver::version(aCurrentVersion)) : nullopt;

		const auto packageJson = json::parse(aPackageData);
		auto versions = packageJson.at("versions");

		bool majorVersionAnnounced = false;

		// Versions are listed from oldest to newest, start with the newest ones
		for (auto elem = versions.rbegin(); elem != versions.rend(); ++elem) {
			semver::version remoteSemver { elem.key() };

			auto isRemotePrerelease = remoteSemver.prerelease_type != semver::prerelease::none;
			if (curSemver) {
				if (isRemotePrerelease && (*curSemver).prerelease_type == semver::prerelease::none) {
					// Don't update to pre-release versions
					continue;
				}

				if (remoteSemver.major > (*curSemver).major) {
					if (!majorVersionAnnounced) {
						loggerF(STRING_F(WEB_EXTENSION_MAJOR_UPDATE, elem.key() % aName), LogMessage::SEV_INFO);
						majorVersionAnnounced = true;
					}

					// Don't perform major upgrades automatically
					continue;
				} else if (remoteSemver.major == (*curSemver).major) {
					// Same major version, compare normally
					auto comp = (*curSemver).compare(remoteSemver);
					if (comp >= 0) {
						// No new version available
						dcdebug("No updates available for extension %s\n", aName.c_str());
						return;
					} else {
						dcdebug("New update available for extension %s (current version %s, available version %s)\n", aName.c_str(), aCurrentVersion.c_str(), elem.key().c_str());
					}
				} else {
					// Old major version, we shouldn't really be here
					continue;
				}
			} else if (isRemotePrerelease) {
				// Don't install pre-release version for now
				continue;
			}

			// Install
			const auto dist = elem.value().at("dist");

			const auto url = dist.at("tarball");
			const auto sha = dist.at("shasum");

			installF(aName, url, sha);
			return;
		}
	}
}