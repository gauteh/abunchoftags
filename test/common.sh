#! /usr/bin/bash

function die() {
  echo "=> error: $1"
  exit 1
}

dbroot=$(realpath ./test/mail/test_mail)



