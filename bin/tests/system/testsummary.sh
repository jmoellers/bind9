#!/bin/sh
#
# Copyright (C) Internet Systems Consortium, Inc. ("ISC")
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# See the COPYRIGHT file distributed with this work for additional
# information regarding copyright ownership.

# Creates the system tests output file from the various test.output files.  It
# then searches that file and prints the number of tests passed, failed, not
# run.  It also checks whether the IP addresses 10.53.0.[1-8] were set up and,
# if not, prints a warning.
#
# Usage:
#    testsummary.sh [-n]
#
# -n	Do NOT delete the individual test.output files after concatenating
#	them into systests.output.
#
# Status return:
# 0 - no tests failed
# 1 - one or more tests failed

SYSTEMTESTTOP=.
. $SYSTEMTESTTOP/conf.sh

keepfile=0

while getopts "n" flag; do
    case $flag in
	n) keepfile=1 ;;
    esac
done

cat */test.output > systests.output 2> /dev/null
if [ $keepfile -eq 0 ]; then
    rm -f */test.output
fi

status=0
echo "I:System test result summary:"
grep 'R:[a-z0-9_-][a-z0-9_-]*:[A-Z][A-Z]*' systests.output | cut -d':' -f3 | sort | uniq -c | sed -e 's/^/I:/'
grep 'R:[a-z0-9_-][a-z0-9_-]*:FAIL' systests.output > /dev/null && status=1

exit $status
