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
 * Usage:
 *
 *    ./tfsync database_path direction lastmod query [--dryrun]
 *
 *    database_path:    path to notmuch database
 *    direction:        tag-to-maildir or maildir-to-tag
 *    lastmod:          which revision to start processing from
 *    query:            further restrict messages to operate on with query
 *    --dryrun:         do not make any changes.
 *
 */

/* program options */
# include <boost/program_options.hpp>
# include <boost/filesystem.hpp>

# include <iostream>
# include <string>
# include <sstream>
# include <vector>
# include <algorithm>

# include <notmuch.h>

# include "tfsync.hh"

using namespace std;
using namespace boost::filesystem;

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

  if (argc < 5) {
    cerr << "error: did not specify query, use \"*\" for all messages." << endl;
    return 1;
  }

  string inputquery (argv[4]);
  cout << "=> query: " << inputquery << endl;

  bool dryrun = true;

  if (argc == 6) {
    if (string (argv[5]) == "--dryrun")  {
      cout << "=> note: dryrun!" << endl;
      dryrun = true;
    } else {
      cerr << "error: unknown 5th argument." << endl;
      return 1;
    }
  }

  /* open db */
  notmuch_database_t * db = setup_db (db_path.c_str());

  unsigned int revision = notmuch_database_get_revision (db);
  cout << "* db: current revision: " << revision << endl;

  stringstream ss;
  ss << revision;
  string revision_s = ss.str();

  if (direction == MAILDIR_TO_TAG) {
    cout << "==> running: maildir to tag.." << endl;
    time_t gt0 = clock ();

    notmuch_query_t * query;
    query = notmuch_query_create (db,
        ("lastmod:" + lastmod + ".." + revision_s + " " + inputquery).c_str());

    int total_messages = notmuch_query_count_messages (query);
    cout << "*  messages changed since " << lastmod << ": " << total_messages << endl;

    notmuch_messages_t * messages = notmuch_query_search_messages (query);

    cout << "*  query time: " << ((clock() - gt0) * 1000.0 / CLOCKS_PER_SEC) << " ms." << endl;

    notmuch_message_t * message;

    int count = 0;

    for (;
         notmuch_messages_valid (messages);
         notmuch_messages_move_to_next (messages)) {

      message = notmuch_messages_get (messages);

      if (verbose)
        cout << "working on message (" << count << " of " << total_messages << "): " << notmuch_message_get_message_id (message) << endl;

      vector<string> maildirs;

      notmuch_filenames_t * nm_fnms = notmuch_message_get_filenames (message);
      for (;
           notmuch_filenames_valid (nm_fnms);
           notmuch_filenames_move_to_next (nm_fnms)) {

        const char * fnm = notmuch_filenames_get (nm_fnms);


        /* path is in style:
         *
         * /path/to/db/gaute.vetsj.com/archive/cur/1400669687_1.17691.strange,U=150275,FMD5=e5b16880bf86ed7af066aa97fb0288d8:2,S
         *
         * we are only interested in the maildir part.
         *
         */

        path p (fnm);
        p = p.parent_path (); // cur or new
        p = p.parent_path (); // full maildir
        p = p.filename ();    // maildir

        maildirs.push_back (p.c_str());

        if (verbose) {
          cout << "message (" << count << "): maildir: " << p.c_str() << endl;
        }
      }

      notmuch_filenames_destroy (nm_fnms);

      /* get tags */
      vector<string> tags;
      notmuch_tags_t * nm_tags = notmuch_message_get_tags (message);
      for (;
           notmuch_tags_valid (nm_tags);
           notmuch_tags_move_to_next (nm_tags)) {
        const char * tag = notmuch_tags_get (nm_tags);
        tags.push_back (tag);

        if (verbose) {
          cout << "message (" << count << "): tag: " << tag << endl;
        }
      }

      notmuch_tags_destroy (nm_tags);

      /* sort both maildir and tags */
      sort (maildirs.begin (), maildirs.end ());
      sort (tags.begin (), tags.end());

      /* remove tags that are taken care of by notmuch maildir flags */


      cout << "message (" << count << "), maildirs: " << maildirs.size() << ", tags: " << tags.size() << endl;


      /* tags to add */
      vector<string> add;


      /* tags to remove */
      vector<string> rem;


      notmuch_message_destroy (message);
      count++;
    }



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

template<class T> bool has (vector<T> v, T e) {
  return (find(v.begin (), v.end (), e) != v.end ());
}


