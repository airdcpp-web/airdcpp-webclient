/*
* Copyright (C) 2026 AirDC++ Project
* SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <web-server/NpmVersion.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

	using json = nlohmann::json;

	struct TestCase {
		std::string current;
		std::vector<std::string> versions;
		std::string expected;
		std::string major;
	};

	void require(bool aCondition, const std::string& aMessage) {
		if (!aCondition) {
			throw std::runtime_error(aMessage);
		}
	}

	webserver::NpmVersionSelection select(const json& aVersions, const std::string& aCurrent) {
		const auto current = aCurrent.empty() ? std::nullopt : std::optional(semver::from_string(aCurrent));
		return webserver::selectNpmVersion(aVersions, current);
	}

	void check(const TestCase& aCase) {
		// Preserve both input orders in the serialized JSON before production parsing.
		for (const auto reverse : { false, true }) {
			auto keys = aCase.versions;
			if (reverse) {
				std::reverse(keys.begin(), keys.end());
			}

			std::string input = "{";
			for (const auto& key : keys) {
				if (input.size() > 1) {
					input += ',';
				}
				input += json(key).dump() + ':' + json({ { "dist", {
					{ "tarball", "url:" + key }, { "shasum", "sha:" + key }
				} } }).dump();
			}
			input += '}';

			const auto versions = json::parse(input);
			const auto result = select(versions, aCase.current);
			require(result.version == aCase.expected, "Incorrect candidate for " + aCase.current + " in " + input);
			require(result.majorVersion == aCase.major, "Incorrect major notification for " + input);
			if (!result.version.empty()) {
				const auto& dist = versions.at(result.version).at("dist");
				require(dist.at("tarball") == "url:" + aCase.expected, "Wrong tarball");
				require(dist.at("shasum") == "sha:" + aCase.expected, "Wrong shasum");
			}
		}
	}

	void expectInvalidVersion(const json& aVersions, const std::string& aCurrent) {
		try {
			select(aVersions, aCurrent);
		} catch (const std::system_error&) {
			return;
		}
		throw std::runtime_error("Invalid version was accepted");
	}
}

int main() {
	try {
		const std::vector<TestCase> cases = {
			{ "", { "1.9.0", "1.10.0" }, "1.10.0", "" },
			{ "1.9.0", { "1.9.0", "1.10.0" }, "1.10.0", "" },
			{ "1.8.0", { "1.9.0", "1.10.0" }, "1.10.0", "" },
			{ "1.2.8", { "1.2.9", "1.2.10" }, "1.2.10", "" },
			{ "1.10.0", { "1.9.0", "1.10.0" }, "", "" },
			{ "1.10.0", { "1.9.0" }, "", "" },
			{ "1.9.0-beta.1", { "1.9.0-beta.1", "1.9.0" }, "1.9.0", "" },
			{ "1.9.0-beta.1", { "1.9.0", "1.10.0-beta.1" }, "1.10.0-beta.1", "" },
			{ "1.10.0-beta.8", { "1.10.0-beta.9", "1.10.0-beta.10" }, "1.10.0-beta.10", "" },
			{ "1.9.0", { "1.9.0", "1.10.0-beta.1" }, "", "" },
			{ "", { "1.9.0-beta.1", "1.10.0-beta.1" }, "", "" },
			{ "", { "1.9.0", "2.0.0" }, "2.0.0", "" },
			{ "", { "2.0.0", "10.0.0" }, "10.0.0", "" },
			{ "1.9.0", { "1.9.0", "2.0.0" }, "", "2.0.0" },
			{ "1.8.0", { "1.9.0", "1.10.0", "2.0.0", "10.0.0" }, "1.10.0", "10.0.0" },
			{ "1.9.0", { "2.0.0-beta.1" }, "", "" },
			{ "1.9.0-beta.1", { "2.0.0-beta.1", "10.0.0-beta.1" }, "", "10.0.0-beta.1" },
			{ "2.1.0", { "1.99.0", "2.1.0" }, "", "" },
			{ "1.0.0", {}, "", "" },
			{ "", {}, "", "" },
			{ "1.0.0", { "1.0.0" }, "", "" },
			// Build metadata never makes an installed version newer.
			{ "1.2.3+build.1", { "1.2.3+build.2" }, "", "" },
			{ "1.2.3", { "1.2.3+build.2" }, "", "" },
			{ "1.2.3+build.1", { "1.2.3" }, "", "" },
			// Preserve the original key and its metadata when precedence is equal.
			{ "1.0.0", { "1.2.3+build.1", "1.2.3+build.2" }, "1.2.3+build.2", "" },
			{ "1.0.0", { "2.1.0+build.1", "2.1.0+build.2" }, "", "2.1.0+build.2" },
			{ "", { "1.2.3+build.2", "1.2.4+build.1" }, "1.2.4+build.1", "" },
			// All prerelease labels follow the same existing installation policy.
			{ "", { "1.2.3-dev.1" }, "", "" },
			{ "1.2.0", { "1.2.3-dev.1" }, "", "" },
			{ "1.2.0", { "2.0.0-dev.1" }, "", "" },
			{ "1.2.3-dev.1", { "1.2.3-dev.2", "1.2.3-dev.10" }, "1.2.3-dev.10", "" },
			{ "1.2.3-dev.1", { "1.2.3" }, "1.2.3", "" },
			{ "1.2.3-dev.1", { "2.0.0-dev.1" }, "", "2.0.0-dev.1" },
			{ "1.2.3-dev.1+build.1", { "1.2.3-dev.1+build.2" }, "", "" },
			{ "1.2.3-alpha.9", { "1.2.3-alpha.10", "1.2.3-alpha.beta" }, "1.2.3-alpha.beta", "" },
			// Strict SemVer prerelease identifiers are case sensitive.
			{ "1.2.3-BETA.1", { "1.2.3-beta.1" }, "1.2.3-beta.1", "" },
			{ "1.2.3-beta.1", { "1.2.3-BETA.1" }, "", "" },
			// 1.x accepts wider core components and arbitrary-size prerelease numbers.
			{ "1.65535.0", { "1.65536.0" }, "1.65536.0", "" },
			{ "65535.0.0", { "65536.0.0" }, "", "65536.0.0" },
			{ "", { "4294967295.0.0" }, "4294967295.0.0", "" },
			{ "1.2.3-alpha.65535", { "1.2.3-alpha.9999999999999999999999" }, "1.2.3-alpha.9999999999999999999999", "" },
		};
		for (const auto& test : cases) {
			check(test);
		}

		// Use strict parsing, never coerce partial or malformed versions.
		const std::vector<std::string> invalidVersions = {
			"garbage", "1.2", "1.2.3junk", "1.02.3", "01.2.3", "1.2.03", "1.2.3-beta.01",
			"4294967296.0.0", "1.2.3+build..1", "1.2.3+", "1.2.3-", "v1.2.3", " 1.2.3 ",
			"1.2.3-" + std::string(507, 'a')
		};
		for (const auto& invalid : invalidVersions) {
			expectInvalidVersion(json::object(), invalid);
			expectInvalidVersion(json{ { invalid, json::object() } }, "1.0.0");
		}

		// A complete scan also encounters invalid keys hidden by the old early return.
		expectInvalidVersion(json{ { "0.bad", {} }, { "1.9.0", {} } }, "1.0.0");
		expectInvalidVersion(json{ { "garbage", {} }, { "1.9.0", {} } }, "1.0.0");
		std::cout << cases.size() << " selection cases in both JSON orders and "
			<< invalidVersions.size() * 2 + 2 << " invalid-version checks passed\n";
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
