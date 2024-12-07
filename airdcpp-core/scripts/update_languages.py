#!/usr/bin/env python

from __future__ import print_function

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path

import glob, os
import shutil

def write_file(root, output_file):
    tree = ET.ElementTree(root)
    ET.indent(tree, '\t')
    with open(output_file, "wb") as f:
        f.write(b'<?xml version="1.0" encoding="utf-8" standalone="yes"?>\n')
        tree.write(f, xml_declaration=False, encoding='utf-8')

# Increase the version number for the new generated file
def get_new_version(file_path):
    try:
        source_tree = ET.parse(file_path)
        source_root = source_tree.getroot()

        old_revision = source_root.attrib['Revision']
        return str(int(old_revision) + 1)
    except IOError:
        return '0'

replace_map = [
    (r'\t', '\t'),
    (r'\r', ''),
    (r'\n', '\n'),
    (r'\\', '\\'),
    (r'\"', '\"'),
    (r'\'', '\''),
]


# Convert and save the translation
def convert_file(input_file, output_file):
    source_tree = ET.parse(input_file)
    source_root = source_tree.getroot()
    
    new_revision = get_new_version(output_file)
    output_root = ET.Element('Language', Name='Lang', Author='AirDC++ Translation Team', Revision=new_revision)
    output_strings = ET.SubElement(output_root, 'Strings')

    for child in source_root:
        string = ET.SubElement(output_strings, 'String', Name=child.attrib['name'])

        text = child.text
        for str, replacement in replace_map:
            text = text.replace(str, replacement)

        string.text = text

    write_file(output_root, output_file)
    print('Translation {} was saved (revision {})'.format(output_file, new_revision))


# Check if the newly downloaded file is different from the previously converted one (done before conversions)
def has_changed(source_file_path, temp_file_path):
    with open(source_file_path, 'r', encoding="utf-8") as file:
        old_content = file.read()

    try:
        with open(temp_file_path, 'r', encoding="utf-8") as file:
            new_content = file.read()
    except IOError:
        print('Could not read previous file {}, creating new one'.format(temp_file_path))
        return True

    return old_content != new_content

# Iterate over the files in source_directory and convert them
def update_translation_files(source_directory, target_directory, force = False):
    temp_directory = os.path.join(source_directory, 'previous')

    Path(temp_directory).mkdir(parents=True, exist_ok=True)
    Path(target_directory).mkdir(parents=True, exist_ok=True)

    converted = 0
    unchanged = 0
    failed = 0

    os.chdir(source_directory)
    for name in glob.glob("*.xml"):
        source_file_path = os.path.join(source_directory, name)
        temp_file_path = os.path.join(temp_directory, name)

        if not force and not has_changed(source_file_path, temp_file_path):
            print('File {} unchanged, skipping'.format(source_file_path))
            unchanged += 1
            continue

        # Convert it
        target_file_path = os.path.join(target_directory, name)
        convert_file(source_file_path, target_file_path)

        # Update the comparison file
        try:
            shutil.copyfile(source_file_path, temp_file_path)
            converted += 1
        except IOError:
            print('Failed to copy file to {}'.format(source_file_path))
            failed += 1

    print('Converted: {}, unchanged: {}, failed: {}'.format(converted, unchanged, failed))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source_directory", help="Directory where the files downloaded from Transifex are located")
    parser.add_argument("target_directory", help="Directory for the converted files")
    args = parser.parse_args()

    update_translation_files(source_directory=args.source_directory, target_directory=args.target_directory)
