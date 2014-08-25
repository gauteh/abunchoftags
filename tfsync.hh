# pragma once

using namespace std;

# include <notmuch.h>

notmuch_database_t * setup_db ();
notmuch_query_t *    query (int lastmod);

