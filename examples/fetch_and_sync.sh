#! /bin/bash
#
# do a full sync: local-to-remote, offlineimap, remote-to-local. see
#                 README.md for details.
#
#

function fail() {
  echo "=> failed: $1"
  exit 1
}

db=/home/gaute/.mail/
qry="path:gaute.vetsj.com/**"

echo "=> full sync on $db.."
last_sync_rev=$HOME/.config/astroid/last_sync_rev
if [ -e ${last_sync_rev} ]; then
  lastrev=$(cat ${last_sync_rev})
else
  lastrev=0
fi
revnow=$(notmuch_get_revision $db)

# sync tags local-to-remote
keywsync -m $db -q "$qry AND lastmod:${lastrev}..${revnow}" -t -f -v || fail "local-to-remote did not complete."

before_offlineimap=$( date +%s )

# sync maildir <-> imap
offlineimap || fail "offlineimap did not complete."

# check for new messages, including doing any auto-tagging in
# a post-new hook.
notmuch new || fail "notmuch new did not complete."

# sync tags remote-to-local
keywsync -m $db -q "$qry" --mtime ${before_offlineimap} -k -f -v || fail "remote-to-local did not complete"

# store revision of last sync (if you have very short sync times, this might
# work best)
#echo -n "storing current db revision: "
#notmuch_get_revision $db | tee ${last_sync_rev} || fail "could not get current revision."

# store revision of last sync (using time from before local-to-remote, this
# wors best with long sync times)
echo -n "storing version from before local-to-remote sync: "
echo ${revnow} | tee ${last_sync_rev}

