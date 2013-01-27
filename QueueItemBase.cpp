/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#include "QueueItemBase.h"

namespace dcpp {

QueueItemBase::QueueItemBase(const string& aTarget, int64_t aSize, Priority aPriority, time_t aAdded, Flags::MaskType aFlags) : 
	target(aTarget), size(aSize), priority(aPriority), added(aAdded), autoPriority(false), Flags(aFlags) {

}

//QueueItemBase::QueueItemBase(const QueueItemPtr qi) : target(qi->getTarget()), size(qi->getSize()), priority(qi->getPriority()), added(qi->getAdded()), autoPriority(qi->getAutoPriority()) {

//}


}