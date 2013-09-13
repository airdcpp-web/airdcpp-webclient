/*
 * Copyright (C) 2012-2013 AirDC++ Project
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
#include "UploadBundle.h"
#include "Util.h"

namespace dcpp {

UploadBundle::UploadBundle(const string& aTarget, const string& aToken, int64_t aSize, bool aSingleUser, int64_t aUploaded) : target(aTarget), token(aToken), size(aSize),
	singleUser(aSingleUser), uploadedSegments(aUploaded) { 

	if (uploadedSegments > size)
		uploadedSegments = size;
}

void UploadBundle::addUploadedSegment(int64_t aSize) {
	dcassert(aSize + uploadedSegments <= size);
	if (singleUser && aSize + uploadedSegments <= size) {
		countSpeed();
		uploadedSegments += aSize;
		uploaded -= aSize;
	}
}

void UploadBundle::setSingleUser(bool aSingleUser, int64_t aUploadedSegments) {
	if (aSingleUser) {
		singleUser = true;
		totalSpeed = 0;
		if (aUploadedSegments <= size)
			uploadedSegments = aUploadedSegments;
	} else {
		singleUser = false;
		uploaded = 0;
	}
}

uint64_t UploadBundle::getSecondsLeft() const {
	double avg = totalSpeed > 0 ? totalSpeed : speed;
	int64_t bytesLeft =  getSize() - getUploaded();
	return (avg > 0) ? static_cast<int64_t>(bytesLeft / avg) : 0;
}

string UploadBundle::getName() const {
	if(target.back() == PATH_SEPARATOR)
		return Util::getLastDir(target);
	else
		return Util::getFilePath(target);
}

void UploadBundle::addUpload(Upload* u) {
	//can't have multiple bundles for it
	if (u->getBundle())
		u->getBundle()->removeUpload(u);
	uploads.push_back(u);
	u->setBundle(this);
	if (uploads.size() == 1) {
		findBundlePath(target);
		delayTime = 0;
	}
}

bool UploadBundle::removeUpload(Upload* u) {
	auto s = find(uploads.begin(), uploads.end(), u);
	dcassert(s != uploads.end());
	if (s != uploads.end()) {
		addUploadedSegment(u->getPos());
		uploads.erase(s);
		u->setBundle(nullptr);
		return uploads.empty();
	}
	dcassert(0);
	return uploads.empty();
}

uint64_t UploadBundle::countSpeed() {
	int64_t bundleSpeed = 0, bundleRatio = 0, bundlePos = 0;
	int up = 0;
	for (auto u: uploads) {
		if (u->getAverageSpeed() > 0 && u->getStart() > 0) {
			bundleSpeed += u->getAverageSpeed();
			if (singleUser) {
				up++;
				int64_t pos = u->getPos();
				bundleRatio += pos > 0 ? (double)u->getActual() / (double)pos : 1.00;
				bundlePos += pos;
			}
		}
	}

	if (bundleSpeed > 0) {
		speed = bundleSpeed;
		if (singleUser) {
			bundleRatio = bundleRatio / up;
			actual = ((int64_t)((double)uploaded * (bundleRatio == 0 ? 1.00 : bundleRatio)));
			uploaded = bundlePos;
		}
	}
	return bundleSpeed;
}

void UploadBundle::findBundlePath(const string& aName) {
	if (uploads.empty())
		return;

	Upload* u = uploads.front();
	string upath = u->getPath();
	size_t pos = u->getPath().rfind(aName);
	if (pos != string::npos) {
		if (pos + aName.length() == u->getPath().length()) //file bundle
			target = u->getPath();
		else //dir bundle
			target = upath.substr(0, pos + aName.length() + 1);
	}
}

}