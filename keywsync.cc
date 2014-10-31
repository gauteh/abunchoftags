/* key word sync:
 *
 * Sync X-Keywords with notmuch tags
 *
 * Author: Gaute Hope <eg@gaute.vetsj.com> / 2014 (c) GNU GPL v3 or later.
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
 *  run
 *
 *    $ ./keywsync -h
 *
 *  for usage information.
 *
 */

/* program options */
# include <boost/program_options.hpp>
# include <boost/filesystem.hpp>
# include <boost/date_time/posix_time/posix_time.hpp>

# include <iostream>
# include <string>
# include <sstream>
# include <vector>
# include <algorithm>
# include <chrono>

# include <glibmm.h>
# include <gmime/gmime.h>

# include <notmuch.h>

# include "keywsync.hh"

using namespace std;
using namespace boost::filesystem;
using namespace boost::posix_time;

int main (int argc, char ** argv) {
  cout << "** tag folder sync" << endl;

  /* options */
  namespace po = boost::program_options;
  po::options_description desc ("options");
  desc.add_options ()
    ( "help,h", "print this help message")
    ( "database,m", po::value<string>(), "notmuch database")
    ( "keyword-to-tag,k", "sync keywords to tags")
    ( "mtime", po::value<int>(), "only operate on files with modified after mtime when doing keyword-to-tag sync (unix time)")
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

  if (vm.count("mtime") > 0) {
    if (direction != KEYWORD_TO_TAG) {
      cerr << "error: the mtime argument only makes sense for keyword-to-tag sync direction" << endl;
      exit (1);
    }

    mtime_set = true;
    int mtime = vm["mtime"].as<int>();
    time_t mtime_t = mtime;

    only_after_mtime = from_time_t (mtime_t);

    cout << "mtime: only operating messages with mtime newer than: " << to_simple_string(only_after_mtime) << endl;

  }

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
  chrono::time_point<chrono::steady_clock> t0_c = chrono::steady_clock::now ();

  notmuch_query_t * query;
  query = notmuch_query_create (nm_db,
      inputquery.c_str());

  int total_messages = notmuch_query_count_messages (query);
  cout << "*  messages to check: " << total_messages << endl;

  notmuch_messages_t * messages = notmuch_query_search_messages (query);

  cout << "*  query time: " << ((clock() - gt0) * 1000.0 / CLOCKS_PER_SEC) << " ms." << endl;

  notmuch_message_t * message;

  int count = 0;
  int count_changed = 0;

  for (;
       notmuch_messages_valid (messages);
       notmuch_messages_move_to_next (messages)) {

    message = notmuch_messages_get (messages);

    if (verbose)
      cout << "==> working on message (" << count << " of " << total_messages << "): " << notmuch_message_get_message_id (message) << endl;

    vector<string> file_tags;
    vector<string> paths;

    bool mtime_changed = false;

    // get source files {{{
    notmuch_filenames_t * nm_fnms = notmuch_message_get_filenames (message);
    for (;
         notmuch_filenames_valid (nm_fnms);
         notmuch_filenames_move_to_next (nm_fnms)) {

      const char * fnm = notmuch_filenames_get (nm_fnms);

      /* only add file if mtime is newer than specified */
      if (mtime_set) {
        path p (fnm);
        time_t last_write_t = last_write_time (p);

        ptime last_write = from_time_t (last_write_t);

        if (last_write >= only_after_mtime) {
          mtime_changed = true;
        }
      }

      paths.push_back (fnm);
      if (verbose)
        cout << "* message file: " << fnm << endl;
    }

    notmuch_filenames_destroy (nm_fnms); // }}}

    if (mtime_changed && mtime_set) {
      if (verbose) {
        cout << "=> message changed, checking.." << endl;
      }

    } else {
      if (verbose) {
        cout << "=> message _not_ changed, skipping.." << endl;
      }

      count++;
      notmuch_message_destroy (message);
      continue;

    }

    /* get and test if keywords are consistent between all paths */
    bool consistent = keywords_consistency_check (paths, file_tags);
    if (!consistent) {
      cerr << "=> error: inconsistent tags for files!" << endl;
      if (paranoid) {
        exit (1);
      } else {
        /* possibly keep going? */
        cerr << "=> skipping message." << endl;
        count++;
        notmuch_message_destroy (message);
        continue;
      }
    }

    /* get tags from db */
    vector<string> db_tags;
    notmuch_tags_t * nm_tags = notmuch_message_get_tags (message);
    for (;
         notmuch_tags_valid (nm_tags);
         notmuch_tags_move_to_next (nm_tags)) {

      const char * tag = notmuch_tags_get (nm_tags);
      db_tags.push_back (tag);
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


    cout << "* message (" << count << "), file tags (" << file_tags.size()
         << "): ";
    for (auto t : file_tags) cout << t << " ";
    cout << ", db tags (" << db_tags.size() << "): ";
    for (auto t : db_tags) cout << t << " ";
    cout << endl;

    if (direction == KEYWORD_TO_TAG) { // {{{

      /* keyword to tag mode */


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

      bool changed = false;

      if (!only_remove) {
        if (add.size () > 0) {
          cout << "=> adding tags: ";
          changed = true;
          for (auto t : add) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;

          if (!dryrun) {
            for (auto t : add) {
              notmuch_status_t s = notmuch_message_add_tag (
                  message,
                  t.c_str());

              if (s != NOTMUCH_STATUS_SUCCESS) {
                cerr << "error: could not add tag " << t << " to message." << endl;
                exit (1);
              }

            }
          }
        }
      }

      if (!only_add) {
        if (rem.size () > 0) {
          cout << "=> removing tags: ";
          changed = true;
          for (auto t : rem) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;

          if (!dryrun) {
            for (auto t : rem) {
              notmuch_status_t s = notmuch_message_remove_tag (
                  message,
                  t.c_str());

              if (s != NOTMUCH_STATUS_SUCCESS) {
                cerr << "error: could not add tag " << t << " to message." << endl;
                exit (1);
              }

            }

          }
        }
      }

      if (changed) count_changed++;

      // }}}
    } else { /* tag to keyword mode {{{ */

      bool change = false;

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

      vector<string> new_file_tags = file_tags;

      if (!only_remove) {
        if (add.size () > 0) {
          cout << "=> adding tags: ";
          for (auto t : add) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;
        }

        for (auto t : add)
          new_file_tags.push_back (t);

        change = true;
      }

      sort (new_file_tags.begin (), new_file_tags.end());

      if (!only_add) {
        if (rem.size () > 0) {
          cout << "=> removing tags: ";
          for (auto t : rem) cout << t << " ";

          if (dryrun) cout << "[dryrun]";
          cout << endl;
        }

        vector<string> diff;
        set_difference (new_file_tags.begin(),
                        new_file_tags.end (),
                        rem.begin (),
                        rem.end (),
                        back_inserter (diff));
        new_file_tags = diff;
        change = true;
      }

      /* get file tags with normally ignored kws */
      auto file_tags_all = get_keywords (paths[0], true);
      vector<string> diff;
      set_difference (file_tags_all.begin(),
                      file_tags_all.end (),
                      file_tags.begin (),
                      file_tags.end (),
                      back_inserter (diff));
      for (auto t : diff)
        new_file_tags.push_back (t);

      if (change) {
        for (string p : paths) {
          if (verbose) {
            cout << "old tags: ";
            for (auto t : file_tags) cout << t << " ";
            cout << endl;
            cout << "new tags: ";
            for (auto t : new_file_tags) cout << t << " ";
            cout << endl;
          }

          write_tags (p, new_file_tags);
        }

        count_changed++;
      }
    } // }}}

    notmuch_message_destroy (message);

    if (verbose)
      cout << "==> message (" << count << ") done." << endl;

    count++;
  }

  notmuch_database_close (nm_db);

  chrono::duration<double> elapsed = chrono::steady_clock::now() - t0_c;

  cout << "=> done, checked: " << count << " messages and changed: " << count_changed << " messages in " << ((clock() - gt0) * 1000.0 / CLOCKS_PER_SEC) << " ms [cpu], " << elapsed.count() << " s [real time]." << endl;

  return 0;
}

bool keywords_consistency_check (vector<string> &paths, vector<string> &file_tags) { // {{{
  /* check if all source files for one message have the same tags, outputs
   * all discovered tags to file_tags */

  bool first = true;
  bool valid = true;

  for (string & p : paths) {
    auto t = get_keywords (p, false);

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
} // }}}

vector<string> get_keywords (string p, bool dont_ignore) { // {{{
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

  if (enable_split_chars) {
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
  } else {
    split_string (file_tags, kws, ",");
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

  if (!dont_ignore) {
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
  }

  return file_tags;
} // }}}

void write_tags (string path, vector<string> tags) { // {{{
  /* do the reverse replacements */

  if (enable_split_chars) {
    cerr << "error: cant do reverse tag/keyword transformation when enable_split_chars is enabled" << endl;
    exit (1);
  }

  GMimeStream * f = g_mime_stream_file_new_for_path (path.c_str(),
      "r");
  GMimeParser * parser = g_mime_parser_new_with_stream (f);
  GMimeMessage * message = g_mime_parser_construct_message (parser);

  const char * xkeyw = g_mime_object_get_header (GMIME_OBJECT(message), "X-Keywords");
  if (xkeyw != NULL) {
    cout << "=> current xkeywords: " << xkeyw << endl;
  } else {
    cout << "=> current xkeywords: non-existent." << endl;
  }

  /* reverse map */
  for (auto &t : tags) {
    for (auto rep : replace_chars) {
      replace (t.begin(), t.end(), rep.first, rep.second);
    }

    auto fnd = find_if (map_tags.begin(), map_tags.end (),
        [&](pair<string,string> p) {
          return (t == p.second);
        });

    if (fnd != map_tags.end ()) {
      t = (*fnd).first;
    }
  }

  sort (tags.begin (), tags.end());

  string newh;
  bool first = true;
  for (auto t : tags) {
    if (!first) newh = newh + ",";
    first = false;
    newh = newh + t;
  }

  if (!dryrun) {
    g_mime_object_set_header (GMIME_OBJECT(message), "X-Keywords", newh.c_str());

    GMimeStream * out = g_mime_stream_file_new_for_path ("/tmp/testmsg", "w");
    g_mime_object_write_to_stream (GMIME_OBJECT(message), out);

    g_mime_stream_flush (out);
    g_mime_stream_close (out);
  }

  g_object_unref (message);
  g_object_unref (parser);
  g_mime_stream_close (f);

} // }}}

/* utils {{{ */

template<class T> bool has (vector<T> v, T e) {
  return (find(v.begin (), v.end (), e) != v.end ());
}

void split_string (vector<string> & tokens, string str, string delim) {

  tokens = Glib::Regex::split_simple(delim, str);

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

/* }}} */

