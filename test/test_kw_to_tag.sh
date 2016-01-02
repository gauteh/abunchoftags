#! /usr/bin/bash

source test/common.sh

echo "testing keyword-to-tag"

notmuch count

./keywsync -m $dbroot -k -p -v --more-verbose -a -q "*" || die "failed keyword-to-tag"


