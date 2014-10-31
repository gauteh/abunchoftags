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

>  Note: The query needs to filter messages so that only the messages of the
>  GMail maildir are tested.

1. Synchronize tags local-to-remote (`-t`), now all tag changes done in the
   notmuch db are synchronized with the message files (preferably using a
   `lastmod:` query [1] which catches messages where  have been done after
   the revision of the db at the time of the last remote-to-local synchronization)

1. Save the current unix time: `$ before_offlineimap=$( date +%s )`

1. Run `offlineimap` to synchronize your local maildir and messages with the
   remote. According to the offlineimap documentation [0] the X-Keywords flags
   are synchronized in the same way as maildir flags (whatever that means [2]).

1. Synchronize tags remote-to-local (`-k`) using a `query` that filters out anything
   but the maildir in question. Use the `--mtime` flag to only sync messages that match
   the `query` and are modified after offlineimap was run: `echo $before_offlineimap`.

1. Store the current database time for the next `lastmod` search in the local-to-remote
   step of your next search: `$ notmuch_get_revision /path/to/db`.

> Note: `notmuch new` does not detect message changes that do not include a file addition,
> removal or rename. Therefore simple changes to the `X-Keywords` header will not be detected.
> Use the --mtime query to filter out unchanged files.


[0] http://offlineimap.readthedocs.org/en/next/MANUAL.html?highlight=keywords#sync-from-gmail-to-a-local-maildir-with-labels  
[1] id:1413181203-1676-1-git-send-email-aclements@csail.mit.edu  
[2] Someone have any documentation anywhere?  

