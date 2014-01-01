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
#include "Mapper_WinUPnP.h"

#include "Util.h"
#include "Text.h"
#include "w.h"
#include "AirUtil.h"

#ifdef HAVE_WINUPNP_H
#include <ole2.h>
#include <natupnp.h>
#else // HAVE_WINUPNP_H
struct IUPnPNAT { };
struct IStaticPortMappingCollection { };
#endif // HAVE_WINUPNP_H

namespace dcpp {

const string Mapper_WinUPnP::name = "Windows UPnP";

Mapper_WinUPnP::Mapper_WinUPnP(const string& localIp, bool v6) :
Mapper(localIp, v6)
{
}

bool Mapper_WinUPnP::supportsProtocol(bool v6) const {
	return !v6;
}

#ifdef HAVE_WINUPNP_H

bool Mapper_WinUPnP::init() {
	HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if(FAILED(hr))
		return false;

	if(pUN)
		return true;

	// Lacking the __uuidof in mingw...
	CLSID upnp;
	OLECHAR upnps[] = L"{AE1E00AA-3FD5-403C-8A27-2BBDC30CD0E1}";
	CLSIDFromString(upnps, &upnp);
	IID iupnp;
	OLECHAR iupnps[] = L"{B171C812-CC76-485A-94D8-B6B3A2794E99}";
	CLSIDFromString(iupnps, &iupnp);
	pUN = 0;
	hr = ::CoCreateInstance(upnp, 0, CLSCTX_INPROC_SERVER, iupnp, reinterpret_cast<LPVOID*>(&pUN));
	if(FAILED(hr))
		pUN = 0;
	return pUN ? true : false;
}

void Mapper_WinUPnP::uninit() {
	::CoUninitialize();
}

bool Mapper_WinUPnP::add(const string& port, const Protocol protocol, const string& description) {
	IStaticPortMappingCollection* pSPMC = getStaticPortMappingCollection();
	if(!pSPMC)
		return false;

	/// @todo use a BSTR wrapper
	BSTR protocol_ = SysAllocString(Text::toT(protocols[protocol]).c_str());
	BSTR description_ = SysAllocString(Text::toT(description).c_str());
	BSTR localIP = !localIp.empty() ? SysAllocString(Text::toT(localIp).c_str()) : nullptr;
	auto port_ = Util::toInt(port);

	IStaticPortMapping* pSPM = 0;
	HRESULT hr = pSPMC->Add(port_, protocol_, port_, localIP, VARIANT_TRUE, description_, &pSPM);

	SysFreeString(protocol_);
	SysFreeString(description_);
	SysFreeString(localIP);

	bool ret = SUCCEEDED(hr);
	if(ret) {
		pSPM->Release();

		lastPort = port_;
		lastProtocol = protocol;
	}
	pSPMC->Release();
	return ret;
}

bool Mapper_WinUPnP::remove(const string& port, const Protocol protocol) {
	IStaticPortMappingCollection* pSPMC = getStaticPortMappingCollection();
	if(!pSPMC)
		return false;

	/// @todo use a BSTR wrapper
	BSTR protocol_ = SysAllocString(Text::toT(protocols[protocol]).c_str());
	auto port_ = Util::toInt(port);

	HRESULT hr = pSPMC->Remove(port_, protocol_);
	pSPMC->Release();

	SysFreeString(protocol_);

	bool ret = SUCCEEDED(hr);
	if(ret && port_ == lastPort && protocol == lastProtocol) {
		lastPort = 0;
	}
	return ret;
}

string Mapper_WinUPnP::getDeviceName() {
	/// @todo use IUPnPDevice::ModelName <http://msdn.microsoft.com/en-us/library/aa381670(VS.85).aspx>?
	return Util::emptyString;
}

string Mapper_WinUPnP::getExternalIP() {
	// Get the External IP from the last added mapping
	if(!lastPort)
		return Util::emptyString;

	IStaticPortMappingCollection* pSPMC = getStaticPortMappingCollection();
	if(!pSPMC)
		return Util::emptyString;

	/// @todo use a BSTR wrapper
	BSTR protocol_ = SysAllocString(Text::toT(protocols[lastProtocol]).c_str());

	// Lets Query our mapping
	IStaticPortMapping* pSPM;
	HRESULT hr = pSPMC->get_Item(lastPort, protocol_, &pSPM);

	SysFreeString(protocol_);

	// Query failed!
	if(FAILED(hr) || !pSPM) {
		pSPMC->Release();
		return Util::emptyString;
	}

	BSTR bstrExternal = 0;
	hr = pSPM->get_ExternalIPAddress(&bstrExternal);
	if(FAILED(hr) || !bstrExternal) {
		pSPM->Release();
		pSPMC->Release();
		return Util::emptyString;
	}

	// convert the result
	string ret = Text::wideToAcp(bstrExternal);

	// no longer needed
	SysFreeString(bstrExternal);

	// no longer needed
	pSPM->Release();
	pSPMC->Release();

	return ret;
}

IStaticPortMappingCollection* Mapper_WinUPnP::getStaticPortMappingCollection() {
	if(!pUN)
		return 0;
	IStaticPortMappingCollection* ret = 0;
	HRESULT hr = 0;

	// some routers lag here
	for(int i = 0; i < 3; i++) {
		hr = pUN->get_StaticPortMappingCollection (&ret);
			
		if(SUCCEEDED(hr) && ret) break;

		Sleep(1500);
	}

	if(FAILED(hr))
		return 0;
	return ret;
}

#else // HAVE_WINUPNP_H

bool Mapper_WinUPnP::init() {
	return false;
}

void Mapper_WinUPnP::uninit() {
}

bool Mapper_WinUPnP::add(const string& /*port*/, const Protocol /*protocol*/, const string& /*description*/) {
	return false;
}

bool Mapper_WinUPnP::remove(const string& /*port*/, const Protocol /*protocol*/) {
	return false;
}

string Mapper_WinUPnP::getDeviceName() {
	return Util::emptyString;
}

string Mapper_WinUPnP::getExternalIP() {
	return Util::emptyString;
}

IStaticPortMappingCollection* Mapper_WinUPnP::getStaticPortMappingCollection() {
	return 0;
}

#endif // HAVE_WINUPNP_H

} // dcpp namespace
