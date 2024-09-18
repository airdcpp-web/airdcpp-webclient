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

#include "stdinc.h"
#include "Upload.h"

#include "FilteredFile.h"
#include "ResourceManager.h"
#include "StreamBase.h"
#include "UserConnection.h"
#include "ZUtils.h"

namespace dcpp {

Upload::Upload(UserConnection& conn, const string& path, const TTHValue& tth, unique_ptr<InputStream> aIS) : 
	Transfer(conn, path, tth), stream(std::move(aIS)) { 

	dcassert(!conn.getUpload());
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
	getUserConnection().setUpload(nullptr);
}

void Upload::getParams(const UserConnection& aSource, ParamMap& params) const noexcept {
	Transfer::getParams(aSource, params);
	params["source"] = (getType() == TYPE_PARTIAL_LIST ? STRING(PARTIAL_FILELIST) + " (" + getPath() + ")" : getPath());
}

void Upload::appendFlags(OrderedStringSet& flags_) const noexcept {
	if (isSet(Upload::FLAG_PARTIAL)) {
		flags_.emplace("P");
	}

	if (isSet(Upload::FLAG_ZUPLOAD)) {
		flags_.emplace("Z");
	}

	if (isSet(Upload::FLAG_CHUNKED)) {
		flags_.emplace("C");
	}

	Transfer::appendFlags(flags_);
}

constexpr int8_t DELAY_SECONDS = 10;
bool Upload::checkDelaySecond() noexcept {
	if (delayTime == -1) {
		return false;
	}
	
	delayTime++;
	return delayTime > DELAY_SECONDS;
}

void Upload::disableDelayCheck() noexcept {
	delayTime = -1;
}

} // namespace dcpp
