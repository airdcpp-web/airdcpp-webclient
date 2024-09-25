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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_H_
#define DCPLUSPLUS_DCPP_UPLOAD_H_

#include <airdcpp/forward.h>
#include <airdcpp/Transfer.h>
#include <airdcpp/Flags.h>
#include <airdcpp/GetSet.h>

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

	Upload(UserConnection& aSource, const string& aPath, const TTHValue& aTTH, unique_ptr<InputStream> aIS);
	~Upload() override;

	void getParams(const UserConnection& aSource, ParamMap& params) const noexcept override;

	IGETSET(int64_t, fileSize, FileSize, -1);

	InputStream* getStream();
	void setFiltered();

	void appendFlags(OrderedStringSet& flags_) const noexcept override;
	bool checkDelaySecond() noexcept;
	void disableDelayCheck() noexcept;
private:
	unique_ptr<InputStream> stream;
	int8_t delayTime = 0;
};

} // namespace dcpp

#endif /*UPLOAD_H_*/
