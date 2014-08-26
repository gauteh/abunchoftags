/* tag folder sync:
 *
 * Sync maildir folders with notmuch tags
 *
 *
 * tag-to-maildir:
 *
 *    o. run through all messages that have been changed since
 *       a given revision.
 *
 *    o. check if they exist in all maildirs they should, or if
 *       they should be removed from maildirs where they no longer
 *       belong.
 *
 * maildir-to-tag:
 *
 *    o. run through all messages that have been changed since
 *       a given revision.
 *
 *    o. check if they have all the tags they should depending on the
 *       maildir they are in.
 *
 *    o. check if they have tags they should no longer have when they
 *       have been deleted from a maildir.
 *
 *
 */

/* program options */
# include <boost/program_options.hpp>

# include <iostream>
# include <string>
# include <sstream>

# include <notmuch.h>

# include "tfsync.hh"

using namespace std;

int main (int argc, char ** argv) {
  cout << "** tag folder sync" << endl;

  if (argc < 2) {
    cerr << "error: specify database path." << endl;
    return 1;
  }

  string db_path = string (argv[1]);
  cout << "=> db: " << db_path << endl;

  if (argc < 3) {
    cerr << "error: specify either tag-to-maildir or maildir-to-tag" << endl;
    return 1;
  }

  enum Direction {
    TAG_TO_MAILDIR,
    MAILDIR_TO_TAG,
  };

  Direction direction;

  if (string(argv[2]) == "tag-to-maildir") {
    direction = TAG_TO_MAILDIR;
    cout << "=> direction: tag-to-maildir" << endl;
  } else if (string(argv[2]) == "maildir-to-tag") {
    cout << "=> direction: maildir-to-tag" << endl;
    direction = MAILDIR_TO_TAG;
  } else {
    cerr << "error: unknown argument for direction." << endl;
    return 1;
  }

  if (argc < 4) {
    cerr << "error: specify last modification time or 0 to run through all messages." << endl;
    return 1;
  }

  string lastmod (argv[3]);
  cout << "=> lastmod: " << lastmod << endl;

  bool dryrun = true;

  if (argc == 5) {
    if (string (argv[4]) == "--dryrun")  {
      cout << "=> note: dryrun!" << endl;
      dryrun = true;
    } else {
      cerr << "error: unknown 4th argument." << endl;
      return 1;
    }
  }

  /* open db */
  notmuch_database_t * db = setup_db (db_path.c_str());

  unsigned int revision = notmuch_database_get_revision (db);
  cout << "db: current revision: " << revision << endl;

  stringstream ss;
  ss << revision;
  string revision_s = ss.str();

  if (direction == MAILDIR_TO_TAG) {

    notmuch_query_t * query;
    query = notmuch_query_create (db,
        ("lastmod:" + lastmod + "..." + revision_s).c_str());


  } else {
    cerr << "error: not implemented." << endl;
    exit (1);
  }

  return 0;
}


notmuch_database_t * setup_db (const char * db_path) {

  notmuch_database_t * db;
  auto s = notmuch_database_open (db_path,
      NOTMUCH_DATABASE_MODE_READ_WRITE,
      &db);

  if (s != NOTMUCH_STATUS_SUCCESS) {
    cerr << "db: could not open database." << endl;
    exit (1);
  }

  return db;
}


