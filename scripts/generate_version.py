#!/bin/sh

import argparse
import subprocess

import textwrap
import time

def content_equals(file_path, content):
    try:
        with open(file_path, 'r', encoding="utf-8") as file:
            return file.read() == content
    except IOError:
        return False

def get_git_output(args):
    result = subprocess.run(["git"] + args, stdout=subprocess.PIPE)
    return result.stdout.decode('utf-8').rstrip()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output_file", help="Generate an XML file that can be upload to Transifex")
    parser.add_argument("version")
    parser.add_argument("application_name")
    parser.add_argument("application_id")

    args = parser.parse_args()

    version=args.version
    appName=args.application_name
    appId=args.application_id
    versionDate=int(time.time()) 
    commitCount=0
    commit=""

    result = subprocess.run(["git", "ls-remote"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) 
    if result.returncode != 0:
        print("-- Not using a Git version")
    # else:
    elif get_git_output(["rev-parse", "--abbrev-ref", "HEAD"]) != 'master' or 'b' in version or 'a' in version:
      version=get_git_output(["describe", "--tags", "--abbrev=4", "--dirty=-d"])
      commitCount=get_git_output(["rev-list", "HEAD", "--count"])
      versionDate=get_git_output(["show", "--format=%at"]).partition('\n')[0]
      commit=get_git_output(["rev-parse", "HEAD"])

      print("-- Git version detected ({0})".format(version))
    
    output = textwrap.dedent('''\
      #define GIT_TAG "{version}"
      #define GIT_COMMIT "{commit}"
      #define GIT_COMMIT_COUNT {commitCount}
      #define VERSION_DATE {versionDate}
      #define APPNAME_INC "{appName}"
      #define APPID_INC "{appId}"
    ''').format(
        version=version,
        commit=commit,
        commitCount=commitCount,
        versionDate=versionDate,
        appName=appName,
        appId=appId
    )

    if not content_equals(args.output_file, output):
      with open(args.output_file, "w") as f:
          f.write(output)
