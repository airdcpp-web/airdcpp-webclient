#!/usr/bin/env python

from __future__ import print_function

import argparse
import re
import os
import textwrap
import xml.etree.ElementTree as ET

camel_case_re = re.compile(r"(?:^|[-_])(\w)")

def camel_case_replace(match):
    return match.group(1).upper()

def camel_case(string):
    string = string.lower()
    return camel_case_re.sub(camel_case_replace, string)

def parse_content(header_path):
    enum_started = False
    texts = dict()
    with open(header_path, 'r', encoding="utf-8") as f:
        # tmp = f.read()
        for l in f:
            parts = l.split(None, 1)
            if parts and parts[0] == "enum":
                enum_started = True
            elif enum_started:
                parts = l.split("//", 1)
                if parts and len(parts) == 2:
                    name, string = parts

                    name = name.strip(" \t,")
                    string = string.strip(" \t\r\n")[1:-1] # Remove quotation marks

                    if name == "LAST":
                        break

                    texts[camel_case(name)] = string
    return texts

def generate_stringdefs(directory, force = False):
    header_path = os.path.join(directory, "StringDefs.h")
    cpp_path = os.path.join(directory, "StringDefs.cpp")
    if not force and (os.path.isfile(cpp_path) and os.path.getmtime(header_path) < os.path.getmtime(cpp_path)):
        print("-- No string changes detected, using old StringDefs.cpp")
        return False

    texts = parse_content(header_path)

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

        def write_text(strings, name):
            formatted_texts = "\n".join('\t"{}",'.format(t) for t in strings)
            f.write(template.format(name=name, texts=formatted_texts))

        write_text(texts.values(), "strings")
        write_text(texts.keys(), "names")

    print("-- StringDefs.cpp was generated")
    return True

replace_map = [
    (r'\t', '\t'),
    (r'\r', '\r'),
    (r'\n', '\n'),
    (r'\\', '\\'),
    (r'\"', '\"'),
    (r'\'', '\''),
]

# Convert StringDefs to Android resource XML file that is supported by Transifex
def generate_xml(directory, output_file):
    header_path = os.path.join(directory, "StringDefs.h")
    texts = parse_content(header_path)

    root = ET.Element('resources')
    for key, text in texts.items():
        string = ET.SubElement(root, 'string', name=key)

        for str, replacement in replace_map:
            text = text.replace(str, replacement)

        string.text = text

    tree = ET.ElementTree(root)
    ET.indent(tree, '\t')
    with open(output_file, "wb") as f:
        f.write(b'<?xml version="1.0" encoding="utf-8" standalone="yes"?>\n')
        tree.write(f, xml_declaration=False, encoding='utf-8')

    print('-- XML file {} was written'.format(output_file))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", help="Directory containing StringDefs.h")
    parser.add_argument("--xml_output_path", help="Generate an XML file that can be upload to Transifex", required=False)
    parser.add_argument("--quiet", required=False, action='store_true')
    parser.add_argument("--force", required=False, action='store_true')
    parser.set_defaults(quiet=False)
    parser.set_defaults(force=False)

    args = parser.parse_args()

    generate_stringdefs(args.directory, args.force)
    
    if (args.xml_output_path):
        generate_xml(args.directory, args.xml_output_path)
