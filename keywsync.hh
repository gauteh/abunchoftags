# pragma once

# include <vector>
# include <string>
# include <glibmm.h>

# include <boost/filesystem.hpp>
# include <boost/date_time/posix_time/posix_time.hpp>

using namespace std;
using namespace boost::filesystem;
using namespace boost::posix_time;

# include <notmuch.h>

# define ustring Glib::ustring

notmuch_database_t * setup_db (const char *);

/* tags to ignore from syncing (_must_ be sorted!)
 *
 * these are either internal notmuch tags or tags handled
 * by maildirflags.
 *
 */
const vector<ustring> ignore_tags = {
  "attachment",
  "draft",
  "encrypted"
  "flagged",
  "important",
  "new",
  "passed",
  "replied",
  "sent",
  "signed",
  "unread",
};

/* map keyword to tag, done before ignore_tags */
const list<pair<ustring,ustring>> map_tags {
  { "\\Draft", "draft" },
  { "\\Important", "important" },
  { "\\Inbox", "inbox" },
  { "\\Junk", "spam" },
  { "\\Muted", "muted" },
  { "\\Sent", "sent" },
  { "\\Starred", "flagged" },
  { "\\Trash", "deleted" },
};

/* replace chars, done before map keyword to tag */
bool enable_replace_chars = true;
const list<pair<char,char>> replace_chars {
  { '/', '.' },
};

bool keywords_consistency_check (vector<ustring> &, vector<ustring> &);
vector<ustring> get_keywords (ustring p, bool);
void split_string (vector<ustring> &, ustring, ustring);

void write_tags (ustring p, vector<ustring> tags);

template<class T> bool has (vector<T>, T);

enum Direction {
  NONE,
  TAG_TO_KEYWORD,
  KEYWORD_TO_TAG,
};

Direction direction;
ustring    inputquery;

bool  mtime_set = false;
ptime only_after_mtime;

bool verbose = false;
bool more_verbose = false;
bool dryrun  = false;
bool paranoid = false;
bool only_add = false;
bool only_remove = false;
bool maildir_flags = false;
bool enable_add_x_keywords_header = false;
path add_x_keyw_path;

bool remove_double_x_keywords_header = true;

int skipped_messages = 0;

ustring db_path;
notmuch_database_t * nm_db;


