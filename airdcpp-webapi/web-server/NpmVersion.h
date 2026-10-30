/*
* Copyright (C) 2026 AirDC++ Project
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/

#ifndef DCPLUSPLUS_WEBSERVER_NPMVERSION_H
#define DCPLUSPLUS_WEBSERVER_NPMVERSION_H

#include <nlohmann/json.hpp>
#include <semver/semver.hpp>

#include <optional>
#include <string>

namespace webserver {

	struct NpmVersionSelection {
		std::string version;
		std::string majorVersion;
	};

	inline NpmVersionSelection selectNpmVersion(const nlohmann::json& aVersions, const std::optional<semver::version<>>& aCurrentVersion) {
		NpmVersionSelection selection;
		std::optional<semver::version<>> selectedSemver;
		std::optional<semver::version<>> majorSemver;

		// JSON object keys are lexicographically ordered, not semver ordered.
		// Keep the first key in reverse lexical order when precedence is equal.
		for (auto elem = aVersions.rbegin(); elem != aVersions.rend(); ++elem) {
			const auto remoteSemver = semver::from_string(elem.key());
			if (remoteSemver.is_prerelease() && (!aCurrentVersion || !aCurrentVersion->is_prerelease())) {
				continue;
			}

			if (aCurrentVersion && remoteSemver.major() > aCurrentVersion->major()) {
				// Announce the newest major version without installing it.
				if (!majorSemver || remoteSemver > *majorSemver) {
					majorSemver = remoteSemver;
					selection.majorVersion = elem.key();
				}
				continue;
			}

			if (aCurrentVersion && (remoteSemver.major() < aCurrentVersion->major() || remoteSemver <= *aCurrentVersion)) {
				continue;
			}

			if (!selectedSemver || remoteSemver > *selectedSemver) {
				selectedSemver = remoteSemver;
				selection.version = elem.key();
			}
		}

		return selection;
	}
}

#endif
