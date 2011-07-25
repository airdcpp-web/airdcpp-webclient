/*
 * Copyright (C) 2001-2007 Jacek Sieka, j_s@telia.com
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

#if !defined(AFX_TraceManager_H__73C7E0F5_5C7D_4A2A_827B_53267D0EF4C5__INCLUDED_)
#define AFX_TraceManager_H__73C7E0F5_5C7D_4A2A_827B_53267D0EF4C5__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Thread.h"
#include "File.h"

namespace dcpp {

class TraceManager : public Singleton<TraceManager>
{
public:
	void CDECL trace_print(const char* format, ...) noexcept;
	void CDECL trace_start(const char* format, ...) noexcept;
	void CDECL trace_end(const char* format, ...) noexcept;

private:

	void  print(string msg) ;

	friend class Singleton<TraceManager>;
	CriticalSection cs;

	File* f;
	map<DWORD, int> indents;
	
	TraceManager()
	{
		f = new File("error.log", File::WRITE, File::OPEN | File::CREATE);
		f->setEndPos(0);
	};
	~TraceManager() {delete f; };

};

#define TracePrint TraceManager::getInstance()->trace_print
#define TraceStart TraceManager::getInstance()->trace_start
#define TraceEnd TraceManager::getInstance()->trace_end

} // namespace dcpp

#endif // !defined(AFX_TraceManager_H__73C7E0F5_5C7D_4A2A_827B_53267D0EF4C5__INCLUDED_)

/**
 * @file
 * $Id: TraceManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
