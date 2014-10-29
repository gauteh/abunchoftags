# pragma once

# include <vector>
# include <string>

# include <boost/filesystem.hpp>

using namespace std;
using namespace boost::filesystem;

# include <notmuch.h>

notmuch_database_t * setup_db (const char *);

/* tags to ignore from syncing (_must_ be sorted!)
 *
 * these are either internal notmuch tags or tags handled
 * by maildirflags.
 *
 */
const vector<string> ignore_tags = {
  "draft",
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
const list<pair<string,string>> map_tags {
  { "\\Important", "important" },
  { "\\Sent", "sent" },
  { "\\Inbox", "inbox" },
};

/* replace chars, done before map keyword to tag */
const list<pair<char,char>> replace_chars {
  { '/', '.' },
};

/* split chars, done before replace chars */
const vector<string> split_chars {
  "/",
};

bool keywords_consistency_check (vector<string> &, vector<string> &);
vector<string> get_keywords (string p);
void split_string (vector<string> &, string, string);

template<class T> bool has (vector<T>, T);

enum Direction {
  NONE,
  TAG_TO_KEYWORD,
  KEYWORD_TO_TAG,
};

Direction direction;
string    inputquery;

bool verbose = false;
bool dryrun  = false;
bool paranoid = false;
bool only_add = false;
bool only_remove = false;

string db_path;
notmuch_database_t * nm_db;


