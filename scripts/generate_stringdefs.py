#!/usr/bin/env python

from __future__ import print_function

import argparse
import re
import os
import sys
import textwrap

camel_case_re = re.compile(r"(?:^|[-_])(\w)")

def camel_case_replace(match):
    return match.group(1).upper()

def camel_case(string):
    string = string.lower()
    return camel_case_re.sub(camel_case_replace, string)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    args = parser.parse_args()

    header_path = os.path.join(args.directory, "StringDefs.h")
    cpp_path = os.path.join(args.directory, "StringDefs.cpp")
    if os.path.isfile(cpp_path) and os.path.getmtime(header_path) < os.path.getmtime(cpp_path):
        print("-- No string changes detected, using old StringDefs.cpp")
        sys.exit(0)

    texts = {"names": [], "strings": []}
    enum_started = False
    with open(header_path) as f:
        for l in f:
            parts = l.split(None, 1)
            if parts and parts[0] == "enum":
                enum_started = True
            elif enum_started:
                parts = l.split("//", 1)
        if parts and len(parts) == 2:
            name, string = parts
                    name = name.strip(" \t,")
                    string = string.strip(" \t\r\n")
                    if name == "LAST":
                        break
                    texts["names"].append('"' + camel_case(name) + '"')
                    texts["strings"].append(string)
    template = textwrap.dedent('''\
    std::string dcpp::ResourceManager::{name}[] = {{
    {texts}
    }};
    ''')
    with open(cpp_path, "w") as f:
        f.write(textwrap.dedent(
              '''\
              #include "stdinc.h"
              #include "ResourceManager.h"
              '''
        ))
        for n in ("strings", "names"):
            formatted_texts = "\n".join("\t{},".format(t) for t in texts[n])
            f.write(template.format(name=n, texts=formatted_texts))
    print("-- StringDefs.cpp was generated")
