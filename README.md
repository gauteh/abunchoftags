# keywsync

attempts to synchronize notmuch tags between the X-Keywords header of
messages and the notmuch database.

## Usage

## Remote to local

Add all tags from remote to notmuch db:

`$ ./keywsync -m /path/to/db -k -p -a -q query`

Remove all tags from notmuch db that are not in remote:

`$ ./keywsync -m /path/to/db -k -p -r -q query`

Do both in one go:

`$ ./keywsync -m /path/to/db -k -p -q query`

#### Note:
Typically use a query with `tag:new`, but since notmuch will not detect a
change within a message (say a keyword has been changed on an existing message)
it is necessary to run this on all messages now and then to ensure all remote
tag changes have been caught.

## Local to remote

Add all tags from local to remote:

`$ ./keywsync -m /path/to/db -t -p -a -q query`

Remove all tags from local to remote:

`$ ./keywsync -m /path/to/db -t -p -r -q query`

Do both in one go:

`$ ./keywsync -m /path/to/db -t -p -q query`

## Strategy:

Assuming you have fully synced database and you want to synchronize your
maildir with the remote maildir:

1. Synchronize tags local-to-remote (`-t`), now all tag changes done in the
   notmuch db are synchronized with the message files (preferably using a
   `lastmod:` query [1])

1. Run `offlineimap` to synchronize your local maildir and messages with the
   remote. According to the offlineimap documentation [0] the X-Keywords flags
   are synchronized in the same way as maildir flags (whatever that means [2]).

1. Synchronize tags remote-to-local (`-k`) (preferably using a `tag:new`
   query). Notice however, that changes to a flag will without a message
   addition will not be detected. A `lastmod:` query may detect renames, but
   still messages with changed `X-Keywords` may remain. A search for message
   files with mtimes newer than before the `offlineimap` run has to be done.


[0] http://offlineimap.readthedocs.org/en/next/MANUAL.html?highlight=keywords#sync-from-gmail-to-a-local-maildir-with-labels
[1] id:1413181203-1676-1-git-send-email-aclements@csail.mit.edu
[2] Someone have any documentation anywhere?

