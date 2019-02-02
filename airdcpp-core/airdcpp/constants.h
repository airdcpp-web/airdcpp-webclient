/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_CONSTANTS_H_
#define DCPLUSPLUS_DCPP_CONSTANTS_H_

namespace dcpp {

#define SP_HIDDEN 1


// Protocol separators

#define ADC_SEPARATOR '/'
#define ADC_SEPARATOR_STR "/"

#define NMDC_SEPARATOR '\\'
#define NMDC_SEPARATOR_STR "\\"

#define ADC_ROOT ADC_SEPARATOR
#define ADC_ROOT_STR ADC_SEPARATOR_STR

// Empty char defines would cause issues with clang
#define NMDC_ROOT_STR ""


// Filesystem separators

#ifdef _WIN32

#define PATH_SEPARATOR '\\'
#define PATH_SEPARATOR_STR "\\"

#else

# define PATH_SEPARATOR '/'
# define PATH_SEPARATOR_STR "/"

#endif
}

#endif /* DCPLUSPLUS_DCPP_CONSTANTS_H_ */
