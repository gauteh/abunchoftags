# pragma once

# include <vector>
# include <string>

using namespace std;

# include <notmuch.h>

notmuch_database_t * setup_db (const char *);

template<class T> bool has (vector<T>, T);

bool verbose = true;

/* tags handled by maildir flags */
vector<string> maildir_flag_tags = { "draft",
                                     "flagged",
                                     "passed",
                                     "replied",
                                     "unread",
                                   };

/* a copy should always remain in the archive maildir */
string archive_maildir = "archive";


