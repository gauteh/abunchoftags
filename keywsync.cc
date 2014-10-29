/* key word sync:
 *
 * Sync X-Keywords with notmuch tags
 *
 *
 * tag-to-keyword:
 *
 *    o. run through all messages that have been changed since
 *       a given revision.
 *
 *    o. check if they have all the keywords they should, or if
 *       keywords should be deleted where there is no corresponding tag anymore.
 *
 * keyword-to-tag:
 *
 *    o. run through all messages that have been changed since
 *       a given revision.
 *
 *    o. check if they have all the tags they should depending on the
 *       keywords they have.
 *
 *    o. check if they have tags they should no longer have when they
 *       no longer have keywords.
 *
 *
 * Usage:
 *
 *    ./keywsync database_path direction lastmod query [--dryrun]
 *
 *    database_path:    path to notmuch database
 *    direction:        tag-to-keyword or keyword-to-tag
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

# include <glibmm.h>

# include <notmuch.h>

# include "keywsync.hh"

using namespace std;
using namespace boost::filesystem;

int main (int argc, char ** argv) {
  cout << "** tag folder sync" << endl;

  /* options */
  namespace po = boost::program_options;
  po::options_description desc ("options");
  desc.add_options ()
    ( "help,h", "print this help message")
    ( "database,m", po::value<string>(), "notmuch database")
    ( "keyword-to-tag,k", "sync keywords to tags")
    ( "tag-to-keyword,t", "sync tags to keywords")
    ( "query,q", po::value<string>(), "restrict which messages to sync with notmuch query")
    ( "dry-run,d", "do not apply any changes.")
    ( "verbose,v", "verbose")
    ( "paranoid,p", "be paranoid, fail easily.")
    ( "only-add,a", "only add tags")
    ( "only-remove,r", "only remove tags");

  po::variables_map vm;
  po::store ( po::command_line_parser (argc, argv).options(desc).run(), vm );

  if (vm.count ("help")) {
    cout << desc << endl;

    exit (0);
  }

  /* load config */
  if (vm.count("database")) {
    db_path = vm["database"].as<string>();
  } else {
    cout << "error: specify database path." << endl;
    exit (1);
  }

  cout << "=> db: " << db_path << endl;

  direction = NONE;

  if (vm.count("tag-to-keyword")) {
    direction = TAG_TO_KEYWORD;
    cout << "=> direction: tag-to-keyword" << endl;
  }

  if (vm.count("keyword-to-tag")) {
    if (direction != NONE) {
      cerr << "error: only specify one direction." << endl;
      exit (1);
    }
    cout << "=> direction: keyword-to-tag" << endl;
    direction = KEYWORD_TO_TAG;
  }

  if (direction == NONE) {
    cerr << "error: no direction specified" << endl;
    exit (1);
  }

  if (vm.count("query")) {
    inputquery = vm["query"].as<string>();
  } else {
    cerr << "error: did not specify query, use \"*\" for all messages." << endl;
    exit (1);
  }

  cout << "=> query: " << inputquery << endl;

  if (vm.count ("dry-run")) {
    cout << "=> note: dryrun!" << endl;
    dryrun = true;
  } else {
    // TODO: remove when more confident
    cout << "=> note: real-mode, not dry-run!" << endl;
  }

  verbose = (vm.count("verbose") > 0);
  paranoid = (vm.count("paranoid") > 0);
  only_add = (vm.count("only-add") > 0);
  only_remove = (vm.count("only-remove") > 0);

  if (only_add && only_remove) {
    cerr << "only one of -a or -r can be specified at the same time" << endl;
    exit (1);
  }

  /* open db */
  nm_db = setup_db (db_path.c_str());

  unsigned int revision = notmuch_database_get_revision (nm_db);
  cout << "* db: current revision: " << revision << endl;

  stringstream ss;
  ss << revision;
  string revision_s = ss.str();

  time_t gt0 = clock ();

  notmuch_query_t * query;
  query = notmuch_query_create (nm_db,
      inputquery.c_str());

  int total_messages = notmuch_query_count_messages (query);
  cout << "*  messages to check: " << total_messages << endl;

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

    vector<string> file_tags;
    vector<string> paths;

    notmuch_filenames_t * nm_fnms = notmuch_message_get_filenames (message);
    for (;
         notmuch_filenames_valid (nm_fnms);
         notmuch_filenames_move_to_next (nm_fnms)) {

      const char * fnm = notmuch_filenames_get (nm_fnms);

      paths.push_back (fnm);

      if (verbose) {
        cout << "message (" << count << "): file: " << fnm << endl;
      }
    }

    notmuch_filenames_destroy (nm_fnms);

    /* test if keywords are consistent between all paths */
    bool consistent = keywords_consistency_check (paths, file_tags);
    if (!consistent) {
      cerr << "error: inconsistent tags for files!" << endl;
      if (paranoid) {
        exit (1);
      } else {
        /* possibly keep going? */
        continue;
      }
    }


    /* get tags */
    vector<string> db_tags;
    notmuch_tags_t * nm_tags = notmuch_message_get_tags (message);
    for (;
         notmuch_tags_valid (nm_tags);
         notmuch_tags_move_to_next (nm_tags)) {
      const char * tag = notmuch_tags_get (nm_tags);
      db_tags.push_back (tag);

      if (verbose) {
        cout << "message (" << count << "): tag: " << tag << endl;
      }
    }

    notmuch_tags_destroy (nm_tags);

    /* sort tags (file_tags are already sorted) */
    sort (db_tags.begin (), db_tags.end());

    /* remove ignored tags */
    vector<string> diff;
    set_difference (db_tags.begin (),
                    db_tags.end (),
                    ignore_tags.begin (),
                    ignore_tags.end (),
                    back_inserter (diff));


    db_tags = diff;



    cout << "message (" << count << "), file tags (" << file_tags.size()
         << "): ";
    for (auto t : file_tags) cout << t << " ";
    cout << ", db tags (" << db_tags.size() << "): ";
    for (auto t : db_tags) cout << t << " ";
    cout << endl;

    if (direction == KEYWORD_TO_TAG) {

      /* tags to add */
      vector<string> add;
      set_difference (file_tags.begin (),
                      file_tags.end (),
                      db_tags.begin (),
                      db_tags.end (),
                      back_inserter (add));


      /* tags to remove */
      vector<string> rem;
      set_difference (db_tags.begin (),
                      db_tags.end (),
                      file_tags.begin (),
                      file_tags.end (),
                      back_inserter (rem));


      if (!only_remove) {
        if (add.size () > 0) {
          cout << "=> adding tags: ";
          for (auto t : add) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;
        }
      }

      if (!only_add) {
        if (rem.size () > 0) {
          cout << "=> removing tags: ";
          for (auto t : rem) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;
        }
      }

    } else {
      /* tags to add */
      vector<string> add;
      set_difference (db_tags.begin (),
                      db_tags.end (),
                      file_tags.begin (),
                      file_tags.end (),
                      back_inserter (add));


      /* tags to remove */
      vector<string> rem;
      set_difference (file_tags.begin (),
                      file_tags.end (),
                      db_tags.begin (),
                      db_tags.end (),
                      back_inserter (rem));


      if (!only_remove) {
        if (add.size () > 0) {
          cout << "=> adding tags: ";
          for (auto t : add) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;
        }
      }

      if (!only_add) {
        if (rem.size () > 0) {
          cout << "=> removing tags: ";
          for (auto t : rem) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;
        }
      }
    }

    notmuch_message_destroy (message);
    count++;
  }


  cout << "=> done, checked: " << count << " messages in " << ((clock() - gt0) * 1000.0 / CLOCKS_PER_SEC) << " ms." << endl;

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

bool keywords_consistency_check (vector<string> &paths, vector<string> &file_tags) {
  /* check if all source files for one message have the same tags, outputs
   * all discovered tags to file_tags */

  bool first = true;
  bool valid = true;

  for (string & p : paths) {
    auto t = get_keywords (p);

    if (first) {
      first = false;
      file_tags = t;
    } else {

      vector<string> diff;
      set_difference (t.begin (),
                      t.end (),
                      file_tags.begin (),
                      file_tags.end (),
                      back_inserter (diff));


      if (diff.size () > 0) {
        valid = false;
        for (auto &tt : diff) {
          file_tags.push_back (tt);
        }

        sort (file_tags.begin (), file_tags.end ());
      }
    }
  }

  return valid;
}

vector<string> get_keywords (string p) {
  /* get the X-Keywords header from a message and return
   * a _sorted_ vector of strings with the keywords. */

  vector<string> file_tags;

  /* read X-Keywords header */
  notmuch_message_t * message;
  notmuch_status_t s = notmuch_database_find_message_by_filename (
      nm_db,
      p.c_str(),
      &message);

  if (s != NOTMUCH_STATUS_SUCCESS) {
    cerr << "error: opening message file: " << p << endl;
    exit (1);
  }

  const char * x_keywords = notmuch_message_get_header (message, "X-Keywords");
  if (x_keywords == NULL) {
    /* no such field */
    cout << "warning: no X-Keywords header for file: " << p << endl;
    if (paranoid) {
      exit (1);
    } else {
      return file_tags;
    }
  }

  string kws (x_keywords);

  if (verbose) {
    cout << "parsing keywords: " << kws << endl;
  }

  vector<string> initial_tags;
  split_string (initial_tags, kws, ",");

  /* split tags that need splitting into separate tags */
  for (string s : split_chars) {
    for (auto t : initial_tags) {
      vector<string> k;
      split_string (k, t, s);

      for (auto kt : k) {
        file_tags.push_back (kt);
      }
    }
  }

  sort (file_tags.begin (), file_tags.end());
  auto it = unique (file_tags.begin(), file_tags.end());
  file_tags.resize (distance (file_tags.begin(), it));

  notmuch_message_destroy (message);

  if (verbose) {
    cout << "tags: ";
    for (auto t : file_tags) {
      cout << t << " ";
    }
    cout << endl;
  }

  /* do map */
  for (auto &t : file_tags) {
    for (auto rep : replace_chars) {
      replace (t.begin(), t.end(), rep.first, rep.second);
    }

    auto fnd = find_if (map_tags.begin(), map_tags.end (),
        [&](pair<string,string> p) {
          return (t == p.first);
        });

    if (fnd != map_tags.end ()) {
      t = (*fnd).second;
    }
  }

  sort (file_tags.begin (), file_tags.end());

  if (verbose) {
    cout << "tags after map: ";
    for (auto t : file_tags) {
      cout << "'" <<  t << "' ";
    }
    cout << endl;
  }

  /* remove ignored */
  vector<string> diff;
  set_difference (file_tags.begin (),
                  file_tags.end (),
                  ignore_tags.begin (),
                  ignore_tags.end (),
                  back_inserter (diff));


  file_tags = diff;

  if (verbose) {
    cout << "tags after ignore: ";
    for (auto t : file_tags) {
      cout << t << " ";
    }
    cout << endl;
  }

  return file_tags;
}

void split_string (vector<string> & tokens, string str, string delim) {

  tokens = Glib::Regex::split_simple(delim, str);

}



