/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
#include "HttpDownload.h"

namespace dcpp {

HttpDownload::HttpDownload(const string& address, CompletionF f, bool coralize, bool v4only /*false*/) :
	c(new HttpConnection(coralize, true, v4only)),
f(f)
{
	c->addListener(this);
	c->downloadFile(address);
}

HttpDownload::~HttpDownload() {
	c->removeListener(this);
}

void HttpDownload::on(HttpConnectionListener::Data, HttpConnection*, const uint8_t* buf_, size_t len) noexcept {
	buf.append(reinterpret_cast<const char*>(buf_), len);
}

void HttpDownload::on(HttpConnectionListener::Failed, HttpConnection*, const string& status_) noexcept {
	buf.clear();
	status = status_;
	f();
}

void HttpDownload::on(HttpConnectionListener::Complete, HttpConnection*, const string& status_, bool) noexcept {
	status = status_;
	f();
}

void HttpDownload::on(HttpConnectionListener::Retried, HttpConnection*, bool connected) noexcept {
	if(connected)
		buf.clear();
}

} // namespace dcpp
