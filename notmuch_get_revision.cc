/* return the current datbase revision, depends on the patches in
 * id:1413181203-1676-1-git-send-email-aclements@csail.mit.edu
 */
# include <iostream>
# include <notmuch.h>

using namespace std;

int main (int argc, char ** argv) {
  if (argc < 2) {
    cerr << "specify path to notmuch database." << endl;
    return 1;
  }


  notmuch_database_t * db;
  notmuch_status_t s = notmuch_database_open (argv[1],
      NOTMUCH_DATABASE_MODE_READ_WRITE,
      &db);

  if (s != NOTMUCH_STATUS_SUCCESS) {
    cerr << "db: could not open database." << endl;
    return 1;
  }

  const char *uuid;
  unsigned long revision = notmuch_database_get_revision (db, &uuid);

  cout << revision << endl;

  return 0;
}

