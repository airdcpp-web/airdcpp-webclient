/*
 * Copyright (C) 2001-2021 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_DCPLUSPLUS_H
#define DCPLUSPLUS_DCPP_DCPLUSPLUS_H

#include "compiler.h"
#include "typedefs.h"
#include "Exception.h"

namespace dcpp {

typedef function<void(const string&)> StepF;
typedef function<void(float)> ProgressF;
typedef function<void()> Callback;
typedef function<bool(const string& /*Message*/, bool /*isQuestion*/, bool /*isError*/)> MessageF;

class StartupLoader {
public:
	StartupLoader(const StepF& aStepF, const ProgressF& aProgressF, const MessageF& aMessageF) : stepF(aStepF), progressF(aProgressF), messageF(aMessageF) {}

	const StepF stepF;
	const ProgressF& progressF;
	const MessageF& messageF;

	// Tasks to run after everything has finished loading
	// Use for task involving hooks
	void addPostLoadTask(Callback&& aCallback) noexcept {
		postLoadTasks.push_back(aCallback);
	}

	const vector<Callback>& getPostLoadTasks() const noexcept {
		return postLoadTasks;
	}
private:
	vector<Callback> postLoadTasks;
};

typedef function<void(StartupLoader&)> StartupLoadCallback;
typedef function<void(StepF&, ProgressF&)> ShutdownUnloadCallback;

// This will throw AbortException in case of fatal errors (such as hash database initialization errors)
extern void startup(StepF stepF, MessageF messageF, Callback runWizard, ProgressF progressF, Callback moduleInitF = nullptr, StartupLoadCallback moduleLoadF = nullptr);

extern void shutdown(StepF stepF, ProgressF progressF, ShutdownUnloadCallback moduleUnloadF = nullptr, Callback moduleDestroyF = nullptr);

} // namespace dcpp

#endif // !defined(DC_PLUS_PLUS_H)