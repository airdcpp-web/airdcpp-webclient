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

#ifndef DCPLUSPLUS_DCPP_WINDOWS_FILESYSTEM_H
#define DCPLUSPLUS_DCPP_WINDOWS_FILESYSTEM_H

#include <airdcpp/typedefs.h>

namespace webserver {
	class Filesystem {
	public:
		static json getDriveListing(bool aListCdrom) {
			json retJson;

			//Enumerate the drive letters
			auto dwDrives = GetLogicalDrives();
			DWORD dwMask = 1;
			for (int i = 0; i<32; i++) {
				if (dwDrives & dwMask) {

					tstring sDrive;
					sDrive = (wchar_t)('A' + i);
					sDrive += _T(":");

					auto driveType = GetDriveType(sDrive.c_str());
					if (aListCdrom || driveType != DRIVE_CDROM) {
						retJson.push_back(serializeDrive(sDrive, driveType));
					}
				}

				dwMask <<= 1;
			}

			return retJson;
		}

	private:
		static json serializeDrive(const tstring& aDrive, UINT aDriveType) {
			return {
				{ "name", Text::fromT(aDrive) },
				{ "type", {
					{ "id", driveTypeToString(aDriveType) }
				} }
			};
		}

		static string driveTypeToString(UINT aDriveType) {
			switch (aDriveType) {
				case DRIVE_RAMDISK:
				case DRIVE_REMOVABLE: return "removable";
				case DRIVE_FIXED: return "drive_fixed";
				case DRIVE_REMOTE: return "drive_remote";
				case DRIVE_CDROM: return "drive_cdrom";
			}

			return Util::emptyString;
		}
	};
}

#endif