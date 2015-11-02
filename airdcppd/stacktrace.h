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

#ifndef _COW_STACKTRACE_H_
#define _COW_STACKTRACE_H_

#include <vector>
#include <ostream>
#include <string>

#ifndef USE_ADDR2LINE
    #if defined(__linux__)
        #define USE_ADDR2LINE 1
    #else
        #define USE_ADDR2LINE 0
    #endif
#endif


namespace cow {

/** This class represents a stack frame. */
class StackFrame
{
public:
    /** Constructs a new stack frame. */
    StackFrame(const std::string &object, const std::string &function,
                const std::string &address, const std::string &file,
                int line):
        m_object(object), m_function(function),
        m_address(address), m_file(file),
        m_line(line) { }

    StackFrame(const std::string &address):
        m_object(), m_function(), m_address(address),
        m_file(), m_line(0) { }

    std::string get_object() const { return m_object; }
    std::string get_function() const { return m_function; }
    std::string get_address() const { return m_address; }
    std::string get_file() const { return m_file; }
    int get_line() const { return m_line; }

    /** Writes a stack frame to a stream.
     * @param stream Stream to write to
     * @param frame Stack frame to write */
    friend std::ostream &operator<<(std::ostream &stream, const StackFrame &frame) {
        if(frame.m_object.empty()) {
            stream << "Stack frame corrupted?";
        }
        else {
            stream << frame.m_object << " in function " << frame.m_function;
            if(!frame.m_file.empty())
                stream << " in file " << frame.m_file << ":" << frame.m_line;
        }
        stream << " [" << frame.m_address << "]";
        return stream;
    }
private:
    std::string m_object;
    std::string m_function;
    std::string m_address;
    std::string m_file;
    int m_line;
};

typedef std::vector<cow::StackFrame> Frames;

/**
 * @brief A class for generating stack traces.
 * Compile with \c -rdynamic and \c -g to get
 * file names and line numbers.
 * @par Example
 * @code
 *  cow::StackTrace trace;
 *  trace.generate_frames();
 *  std::copy(trace.begin(), trace.end(),
 *      std::ostream_iterator<cow::StackFrame>(std::cout, "\n"));
 * @endcode
 * @par Output:
 * @code
 * ./a.out in function cow::StackTrace::generate_frames() [0x804ba0c]
 * ./a.out in function c() [0x804ad60]
 * ./a.out in function b() [0x804ae1f]
 * ./a.out in function [unknown] [0x804ae38]
 * lib/tls/libc.so.6 in function __libc_start_main [0xb7d32ea8]
 * ./a.out in function __gxx_personality_v0 [0x804ab31]
 * @endcode
 */
class StackTrace:
    private Frames
{
public:
    using Frames::begin;
    using Frames::end;
    using Frames::rbegin;
    using Frames::rend;
    using Frames::size;
	
	StackTrace(const std::string& aAppPath);

    /** Generate the stack frames of the function calls in the currently active thread. */
    void generate_frames();

    /** @brief Demangles a mangled name using C++ ABI mangling rules.
        @par Example input/output
        @code
        _ZNSt13basic_filebufIcSt11char_traitsIcEE19_M_underflow_commonEb
        ->
        std::basic_filebuf<char, std::char_traits<char> >::_M_underflow_common(bool)
        @endcode

        See: http://kegel.com/mangle.html
    */
    static std::string demangle(const std::string &name);
private:
    StackFrame parse_line(const std::string &line);
    #if USE_ADDR2LINE
    void run_addr2line(const std::string &object, const std::string &location, std::string &function, std::string &file, int &linenum);
    std::string appPath;
    #endif
};

}

#endif // _COW_STACKTRACE_H_

