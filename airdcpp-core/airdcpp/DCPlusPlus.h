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

#ifndef DCPLUSPLUS_DCPP_DCPLUSPLUS_H
#define DCPLUSPLUS_DCPP_DCPLUSPLUS_H

#include "compiler.h"
#include "typedefs.h"

namespace dcpp {

class StartupLoader {
public:
	StartupLoader(const StepFunction& aStepF, const ProgressFunction& aProgressF, const MessageFunction& aMessageF) : stepF(aStepF), progressF(aProgressF), messageF(aMessageF) {}

	const StepFunction stepF;
	const ProgressFunction& progressF;
	const MessageFunction& messageF;

	// Tasks to run after everything has finished loading
	// Use for task involving hooks
	void addPostLoadTask(Callback&& aCallback) noexcept {
		postLoadTasks.push_back(std::move(aCallback));
	}

	const vector<Callback>& getPostLoadTasks() const noexcept {
		return postLoadTasks;
	}
private:
	vector<Callback> postLoadTasks;
};

using StartupLoadCallback = function<void (StartupLoader&)>;
using ShutdownUnloadCallback = function<void (StepFunction&, ProgressFunction&)>;

// This will throw AbortException in case of fatal errors (such as hash database initialization errors)
extern void startup(StepFunction stepF, MessageFunction messageF, Callback runWizard, ProgressFunction progressF, Callback moduleInitF = nullptr, StartupLoadCallback moduleLoadF = nullptr);

extern void shutdown(StepFunction stepF, ProgressFunction progressF, ShutdownUnloadCallback moduleUnloadF = nullptr, Callback moduleDestroyF = nullptr);

extern void initializeUtil(const string& aConfigPath = "") noexcept;

} // namespace dcpp

#endif // !defined(DC_PLUS_PLUS_H)