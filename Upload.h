/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef UPLOAD_H_
#define UPLOAD_H_

#include "forward.h"
#include "Transfer.h"
#include "UploadBundle.h"
#include "Flags.h"
#include "GetSet.h"
#include "Util.h"

namespace dcpp {

class Upload : public Transfer, public Flags {
public:
	enum Flags {
		FLAG_ZUPLOAD = 0x01,
		FLAG_PENDING_KICK = 0x02,
		FLAG_RESUMED = 0x04,
		FLAG_CHUNKED = 0x08,
		FLAG_PARTIAL = 0x10
	};

	bool operator==(const Upload* u) const {
		return compare(getToken(), u->getToken()) == 0;
	}

	Upload(UserConnection& aSource, const string& aPath, const TTHValue& aTTH, unique_ptr<InputStream> aIS);
	~Upload();

	void getParams(const UserConnection& aSource, ParamMap& params) const;

	GETSET(int64_t, fileSize, FileSize);
	GETSET(UploadBundlePtr, bundle, Bundle);

	uint8_t delayTime;
	InputStream* getStream();
	void setFiltered();
	void setPos(int64_t pos, int64_t aMaxBytes) noexcept;
private:
	unique_ptr<InputStream> stream;
};

} // namespace dcpp

#endif /*UPLOAD_H_*/
