#!/bin/sh

codesign -s - -v -f --entitlements info.plist ./build/cr
