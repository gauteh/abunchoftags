#! /usr/bin/bash

source test/common.sh

./notmuch_get_revision $dbroot || die "notmuch_get_revision failed!"
