# keywsync

attempts to synchronize notmuch tags between the X-Keywords header of
messages and the notmuch database.

## Disclaimer

This is highly experimental. It will modify you emails. It may totally destroy all your email. Use at own risk.

## Things to be aware of

Check out `keywsync.hh` to see which tags are ignored and how the mappings are
done. I also replace `/` with `.`, so if you got any tags with `.` in them it
is going to become a mess. This can be controlled with `--replace-chars` and
`--no-replace-chars`.

Also, there is no support for tags with spaces in them (they should probably be
wrapped in quotes at some point). Make sure you have a recent version of
OfflineIMAP, some issues with [multiple occurences of
tags](https://github.com/OfflineIMAP/offlineimap/pull/136) should be fixed
there.

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

1. Run `notmuch new` to detect any new or deleted files, or and renames.

1. Synchronize tags remote-to-local (`-k`) using a `query` that filters out anything
   but the maildir in question. Use the `--mtime` flag to only sync messages that match
   the `query` and are modified after offlineimap was run: `echo $before_offlineimap`.

1. Store the current database revision for the next `lastmod` search in the local-to-remote
   step of your next search: `$ notmuch_get_revision /path/to/db`. Alternatively, store the revision from before the local-to-remote sync. In that way it is possible to detect
   local changes that happened during the sync. This will re-check the messages that were modified as part of the remote-to-local sync.

> Note: `notmuch new` does not detect message changes that do not include a file addition,
> removal or rename. Therefore simple changes to the `X-Keywords` header will not be detected.
> Use the --mtime query to filter out unchanged files.

## Initial sync

If you wish to discard all local tags, start with  a `remote-to-local` sync with a query
matching the entire directory.

If you wish to discard all remote tags, start with a `local-to-remote` sync.

You might be able to preserve some stuff using the `--only-add` and `--only-remove` flags during
sync. Otherwise dumping notmuch tags and restoring at a later time when you have brought your local
copy up-to-date with the remote might work.

## Timing

Running a full keyword-to-tag sync on a Macbook Pro with around 55k messages on an encfs volume
took 1m48s and used about 108MB of memory.

Running a full tag-to-keyword check on the same message base with 3 changed messages
took about 1m5s and 100MB of memory.


## References

[0] http://offlineimap.readthedocs.org/en/next/MANUAL.html?highlight=keywords#sync-from-gmail-to-a-local-maildir-with-labels  
[1] id:1413181203-1676-1-git-send-email-aclements@csail.mit.edu  
[2] Someone have any documentation [anywhere?](https://github.com/OfflineIMAP/offlineimap/issues/130)

#### Other resources

Access to GMail labels via IMAP extensions: https://developers.google.com/gmail/imap_extensions#access_to_gmail_labels_x-gm-labels  
Commit in OfflineIMAP for GMail label support: https://github.com/OfflineIMAP/offlineimap/commit/0e4afa913253c43409e6a32a6b6e11e8b03ed3d9  
Original patch for OfflineIMAP: http://thread.gmane.org/gmane.mail.imap.offlineimap.general/5943/focus=5970  
Special tags for notmuch: http://notmuchmail.org/special-tags/  

