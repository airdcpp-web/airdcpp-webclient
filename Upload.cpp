/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include "Upload.h"

#include "UserConnection.h"
#include "Streams.h"
#include "FilteredFile.h"
#include "ZUtils.h"

namespace dcpp {

Upload::Upload(UserConnection& conn, const string& path, const TTHValue& tth, unique_ptr<InputStream> aIS) : 
	Transfer(conn, path, tth), stream(move(aIS)) { 

	conn.setUpload(this);
}

InputStream* Upload::getStream() { 
	return stream.get(); 
}

void Upload::setFiltered() {
	stream.reset(new FilteredInputStream<ZFilter, true>(stream.release()));
	setFlag(Upload::FLAG_ZUPLOAD);
}

Upload::~Upload() {
	if (bundle) {
		bundle->removeUpload(this);
		bundle = nullptr;
	}

	getUserConnection().setUpload(nullptr);
}

void Upload::getParams(const UserConnection& aSource, ParamMap& params) const {
	Transfer::getParams(aSource, params);
	params["source"] = (getType() == TYPE_PARTIAL_LIST ? STRING(PARTIAL_FILELIST) : getPath());
}

void Upload::resume(int64_t aStart, int64_t aSize) noexcept {
	setSegment(Segment(aStart, aSize));
	setFlag(Upload::FLAG_RESUMED);
	delayTime = 0;

	auto s = stream.get()->releaseRootStream();
	s->setPos(aStart);
	stream.reset(s);
	resetPos();

	if((aStart + aSize) < fileSize) {
		stream.reset(new LimitedInputStream<true>(stream.release(), aSize));
	}
}


} // namespace dcpp
