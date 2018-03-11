/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_VIEWTASKS_H
#define DCPLUSPLUS_DCPP_VIEWTASKS_H

#include <airdcpp/CriticalSection.h>


namespace webserver {

// Must be in merging order (lower ones replace other)
enum Tasks {
	UPDATE_ITEM = 0,
	ADD_ITEM,
	REMOVE_ITEM
};


template<class T>
class ItemTasks {
public:
	struct MergeTask {
		int8_t type;
		PropertyIdSet updatedProperties;

		MergeTask(int8_t aType, const PropertyIdSet& aUpdatedProperties = PropertyIdSet()) : type(aType), updatedProperties(aUpdatedProperties) {

		}

		void merge(const MergeTask& aTask) {
			// Ignore
			if (type > aTask.type) {
				return;
			}

			// Merge
			if (type == aTask.type) {
				updatedProperties.insert(aTask.updatedProperties.begin(), aTask.updatedProperties.end());
				return;
			}

			// Replace the task
			type = aTask.type;
			updatedProperties = aTask.updatedProperties;
		}
	};

	typedef map<T, MergeTask> TaskMap;

	void addItem(const T& aItem) {
		WLock l(cs);
		queueTask(aItem, MergeTask(ADD_ITEM));
	}

	void removeItem(const T& aItem) {
		WLock l(cs);
		queueTask(aItem, MergeTask(REMOVE_ITEM));
	}

	void updateItem(const T& aItem, const PropertyIdSet& aUpdatedProperties) {
		WLock l(cs);
		updatedProperties.insert(aUpdatedProperties.begin(), aUpdatedProperties.end());
		queueTask(aItem, MergeTask(UPDATE_ITEM, aUpdatedProperties));
	}

	void clear() {
		WLock l(cs);
		updatedProperties.clear();
		tasks.clear();
	}

	void get(typename ItemTasks::TaskMap& tasks_, PropertyIdSet& updatedProperties_) {
		WLock l(cs);
		tasks_.swap(tasks);
		updatedProperties_.swap(updatedProperties);
	}
private:
	void queueTask(const T& aItem, MergeTask&& aData) {
		auto j = tasks.find(aItem);
		if (j != tasks.end()) {
			(*j).second.merge(aData);
			return;
		}

		tasks.emplace(aItem, move(aData));
	}

	PropertyIdSet updatedProperties;
	SharedMutex cs;
	TaskMap tasks;
};

}

#endif
