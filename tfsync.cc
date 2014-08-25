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

# include <iostream>
# include <string>
# include <sstream>

# include <notmuch.h>

# include "tfsync.hh"

using namespace std;

int main (int argc, char ** argv) {
  cout << "** tag folder sync" << endl;

  if (argc < 2) {
    cerr << "error: specify either tag-to-maildir or maildir-to-tag" << endl;
    return 1;
  }

  enum Direction {
    TAG_TO_MAILDIR,
    MAILDIR_TO_TAG,
  };

  Direction direction;

  if (string(argv[1]) == "tag-to-maildir") {
    direction = TAG_TO_MAILDIR;
    cout << "=> direction: tag-to-maildir" << endl;
  } else if (string(argv[1]) == "maildir-to-tag") {
    cout << "=> direction: maildir-to-tag" << endl;
    direction = MAILDIR_TO_TAG;
  } else {
    cerr << "error: unknown argument for direction." << endl;
    return 1;
  }

  if (argc < 3) {
    cerr << "error: specify last modification time or 0 to run through all messages." << endl;
    return 1;
  }

  int lastmod;
  istringstream (string (argv[2])) >> lastmod;
  cout << "=> lastmod: " << lastmod << endl;

  bool dryrun = false;

  if (argc == 4) {
    if (string (argv[3]) == "--dryrun")  {
      cout << "=> note: dryrun!" << endl;
      dryrun = true;
    } else {
      cerr << "error: unknown 4th argument." << endl;
      return 1;
    }
  }



  return 0;
}


notmuch_database_t * setup_db () {

}


