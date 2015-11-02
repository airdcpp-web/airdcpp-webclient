/* vim:set ts=4 sw=4 sts=4 et cindent: */
/*
 * nanodc - The ncurses DC++ client
 * Copyright © 2005-2006 Markus Lindqvist <nanodc.developer@gmail.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Contributor(s):
 *  
 */

#include <iostream>
#include <cxxabi.h> // __cxxabiv1::__cxa_demangle
#if USE_STACKTRACE
#include <unistd.h>
#include <execinfo.h> // backtrace_symbols
#endif

#include "stacktrace.h"

namespace cow {

void StackTrace::generate_frames()
{
#if USE_STACKTRACE
    this->clear();

    const int size = 200;
    void *addresses[size];
    int depth = backtrace(addresses, size);
    if (!depth)
        return;

    char **symbols = backtrace_symbols(addresses, depth);
    for (int i = 0; i < depth; ++i)
        this->push_back(parse_line(symbols[i]));

    free(symbols);
#endif // USE_STACKTRACE
}

/* Example lines

./a.out(_ZN3cow10StackTrace15generate_framesEv+0x27) [0x8049499]
./a.out(_Z1cv+0x34) [0x804910a]
./a.out(_Z1bv+0xb) [0x804913b]
./a.out(main+0x16) [0x8049154]
/lib/tls/libc.so.6(__libc_start_main+0xc8) [0xb7d13ea8]
./a.out(__gxx_personality_v0+0x65) [0x8048f61]
*/

StackFrame StackTrace::parse_line(const std::string &line)
{
#if USE_STACKTRACE
  std::string object;
    std::string function;
    std::string address;
    std::string file;
    int linenum = 0;

    auto start = line.find("[0x");
    auto end = line.find("]", start);
    if(start != std::string::npos &&
       end   != std::string::npos)
    {
        ++start;
        address = line.substr(start, end-start);
    }
    else
        address = "0x0000000";

    /* the line should not start with "["
     * this happens when the stack frames are corrupted or something..
     * then the line contains just a hexadecimal address of something(?) */
    if(line.find("[") == 0) {
        return StackFrame(address);
    }

    end = line.find("(");
    if(end == std::string::npos)
    {
        end = line.find(" ");
    }
    object = line.substr(0, end);

    start = line.find("(_");
    end = line.find("+", start);

    if(start == std::string::npos ||
       end   == std::string::npos)
    {
        function = "[unknown]";
    }
    else {
        ++start;
        function = demangle(line.substr(start, end-start));
    }

#if USE_ADDR2LINE
    run_addr2line(object, address, function, file, linenum);
#endif // USE_ADDR2LINE

    return StackFrame(object, function, address, file, linenum);
#endif // USE_STACKTRACE
}

#if USE_ADDR2LINE

StackTrace::StackTrace(const std::string& aAppPath) : appPath(aAppPath) {
	
}


void StackTrace::run_addr2line(const std::string &object, const std::string &address,
                                        std::string &function, std::string &file, int &linenum)
{
	std::string command = "addr2line -C -f -e " + appPath + " " + address + " 2>/dev/null";
    std::string output;
    FILE *fd = popen(command.c_str(), "r");
    if(fd) {
        const int BUF_SIZE = 512;
        char c[BUF_SIZE];
        int r = 1;

        while(!feof(fd)) {
            r = fread(c, 1, BUF_SIZE, fd);
            c[r-1] = '\0';
            output += c;
        }
        pclose(fd);

        unsigned int newline = output.find('\n');
        std::string temp = output.substr(0, newline);
        if(function == "[unknown]" && temp != "??")
            function = temp;

        newline++;
        unsigned int colon = output.rfind(':');
        unsigned int end = output.rfind('\n');
        file = output.substr(newline, colon-newline);
        if(file == "??")
            file.clear();

        linenum = std::atoi(output.substr(colon+1, end-colon).c_str());
    }
}
#endif // USE_ADDR2LINE

std::string StackTrace::demangle(const std::string &name)
{
    std::string ret = name;
    int status;
    char *demangled = __cxxabiv1::__cxa_demangle(ret.c_str(), 0, 0, &status);
    if(status == 0) {
        ret = demangled;
        free(demangled);
    }
    return ret;
}

} // namespace cow

