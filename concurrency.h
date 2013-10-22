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

#ifndef DCPLUSPLUS_DCPP_CONCURRENCY
#define DCPLUSPLUS_DCPP_CONCURRENCY

//#define USE_INTEL_TBB

#if !defined(_MSC_VER) || defined(USE_INTEL_TBB)

#include <tbb/task_group.h>
#include <tbb/parallel_for_each.h>
#include <tbb/concurrent_queue.h>

namespace dcpp
{

using tbb::concurrent_queue;
using tbb::parallel_for_each;
using tbb::task_group;

//using namespace tbb;

}

#else

#include <ppl.h>
#include <concurrent_queue.h>

namespace dcpp
{

using concurrency::concurrent_queue;
using concurrency::task_group;
using concurrency::parallel_for_each;

}

#endif



#endif /* DCPLUSPLUS_DCPP_ATOMIC_HPP_ */
