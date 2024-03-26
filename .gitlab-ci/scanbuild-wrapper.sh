#!/bin/sh -e

# deadcode.DeadStores: allows assigning a value to an auto-cleanup
# plist-html: see `scanbuild-plist-to-junit.py`
scan-build --status-bugs \
           -disable-checker deadcode.DeadStores \
           -plist-html \
           -v \
           "$@"