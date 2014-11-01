#! /bin/bash
#
# do sync according to strategy in README.md
#

db=/home/gaute/.mail/
qry="path:gaute.vetsj.com/**"

echo "=> full sync on $db.."
last_sync_rev=$HOME/.config/astroid/last_sync_rev
if [ -e ${last_sync_rev} ]; then
  lastrev=$(cat ${last_sync_rev})
else
  lastrev=0
fi
revnow=$(./notmuch_get_revision $db)

# sync tags local-to-remote
./keywsync -m $db -q "$qry AND lastmod:${lastrev}..${revnow}" -t -f -v --more-verbose

before_offlineimap=$( date +%s )

# sync maildir <-> imap
offlineimap

# check for new messages, including doing any auto-tagging in
# a post-new hook.
notmuch new

# sync tags remote-to-local
./keywsync -m $db -q "$qry" --mtime ${before_offlineimap} -k -f -v

# store revision of last sync
./notmuch_get_revision $db > ${last_sync_rev}

