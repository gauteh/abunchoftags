/* key word sync:
 *
 * Sync X-Keywords with notmuch tags
 *
 * Author: Gaute Hope <eg@gaute.vetsj.com> / 2014 (c) GNU GPL v3 or later.
 *
 * tag-to-keyword:
 *
 *   o. check if the messages in the query have a matching X-Keywords header
 *      to the list of tags. if not, update and re-write the message.
 *
 * keyword-to-tag:
 *
 *   o. check if the messages in the query have matching tags to the
 *      X-Keywords header. if not, update the tags in the notmuch db.
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

# include "keywsync.hh"

# include <iostream>
# include <string>
# include <sstream>
# include <vector>
# include <algorithm>
# include <chrono>

# include <glibmm.h>
# include <gmime/gmime.h>

# include <boost/program_options.hpp>
# include <boost/filesystem.hpp>
# include <boost/date_time/posix_time/posix_time.hpp>

# include <notmuch.h>

# include "spruce-imap-utils.h"

using namespace std;
using namespace boost::filesystem;
using namespace boost::posix_time;

int main (int argc, char ** argv) {
  cout << "** keyword <-> tag sync" << endl;

  /* options */
  namespace po = boost::program_options;
  po::options_description desc ("options");
  desc.add_options ()
    ( "help,h", "print this help message")
    ( "database,m", po::value<string>(), "notmuch database")
    ( "flags,f", "make notmuch sync maildir flags while passing through" )
    ( "keyword-to-tag,k", "sync keywords to tags")
    ( "mtime", po::value<int>(), "only operate on files with modified after mtime when doing keyword-to-tag sync (unix time)")
    ( "tag-to-keyword,t", "sync tags to keywords")
    ( "query,q", po::value<string>(), "restrict which messages to sync with notmuch query")
    ( "dry-run,d", "do not apply any changes.")
    ( "verbose,v", "verbose")
    ( "more-verbose", "more verbosity")
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

  more_verbose = (vm.count("more-verbose") > 0);
  verbose = (vm.count("verbose") > 0) || more_verbose;
  paranoid = (vm.count("paranoid") > 0);
  only_add = (vm.count("only-add") > 0);
  only_remove = (vm.count("only-remove") > 0);
  maildir_flags = (vm.count("flags") > 0);

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

    if (more_verbose)
      cout << "==> working on message (" << count << " of " << total_messages << "): " << notmuch_message_get_message_id (message) << endl;

    vector<ustring> file_tags;
    vector<ustring> paths;

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
      if (more_verbose)
        cout << "* message file: " << fnm << endl;
    }

    notmuch_filenames_destroy (nm_fnms); // }}}

    if (mtime_set) {
      if (mtime_changed) {
        if (verbose) {
          cout << "=> " << notmuch_message_get_message_id (message) << " changed, checking.." << endl;
        }

      } else {
        if (more_verbose) {
          cout << "=> message _not_ changed, skipping.." << endl;
        }

        count++;
        notmuch_message_destroy (message);
        continue;

      }
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
    vector<ustring> db_tags;
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
    vector<ustring> diff;
    set_difference (db_tags.begin (),
                    db_tags.end (),
                    ignore_tags.begin (),
                    ignore_tags.end (),
                    back_inserter (diff));

    db_tags = diff;


    bool changed = false;

    if (direction == KEYWORD_TO_TAG) { // {{{
      /* keyword to tag mode */

      /* check maildir flags */
      if (maildir_flags) {
        /* may change path of file */
        notmuch_message_maildir_flags_to_tags (message);
      }


      /* tags to add */
      vector<ustring> add;
      set_difference (file_tags.begin (),
                      file_tags.end (),
                      db_tags.begin (),
                      db_tags.end (),
                      back_inserter (add));


      /* tags to remove */
      vector<ustring> rem;
      set_difference (db_tags.begin (),
                      db_tags.end (),
                      file_tags.begin (),
                      file_tags.end (),
                      back_inserter (rem));

      if (!only_remove) {
        if (add.size () > 0) {
          changed = true;
          if (more_verbose) {
            cout << "=> adding tags: ";
            for (auto t : add) cout << t.raw() << " ";

            if (dryrun) cout << "[dryrun]";
            cout << endl;
          }

          if (!dryrun) {
            for (auto t : add) {
              notmuch_status_t s = notmuch_message_add_tag (
                  message,
                  t.c_str());

              if (s != NOTMUCH_STATUS_SUCCESS) {
                cerr << "error: could not add tag " << t.raw() << " to message." << endl;
                exit (1);
              }

            }
          }
        }
      }

      if (!only_add) {
        if (rem.size () > 0) {
          changed = true;

          if (more_verbose) {
            cout << "=> removing tags: ";
            for (auto t : rem) cout << t.raw() << " ";

            if (dryrun) cout << "[dryrun]";
            cout << endl;
          }

          if (!dryrun) {
            for (auto t : rem) {
              notmuch_status_t s = notmuch_message_remove_tag (
                  message,
                  t.c_str());

              if (s != NOTMUCH_STATUS_SUCCESS) {
                cerr << "error: could not add tag " << t.raw() << " to message." << endl;
                exit (1);
              }

            }

          }
        }
      }

      if (changed) count_changed++;

      // }}}
    } else { /* tag to keyword mode {{{ */

      /* tags to add */
      vector<ustring> add;
      set_difference (db_tags.begin (),
                      db_tags.end (),
                      file_tags.begin (),
                      file_tags.end (),
                      back_inserter (add));


      /* tags to remove */
      vector<ustring> rem;
      set_difference (file_tags.begin (),
                      file_tags.end (),
                      db_tags.begin (),
                      db_tags.end (),
                      back_inserter (rem));

      vector<ustring> new_file_tags = file_tags;

      if (!only_remove) {
        if (add.size () > 0) {
          if (more_verbose) {
            cout << "=> adding tags: ";
            for (auto t : add) cout << t.raw() << " ";

            if (dryrun) cout << "[dryrun]";
            cout << endl;
          }

          for (auto t : add)
            new_file_tags.push_back (t);

          changed = true;
        }
      }

      sort (new_file_tags.begin (), new_file_tags.end());

      if (!only_add) {
        if (rem.size () > 0) {
          if (more_verbose) {
            cout << "=> removing tags: ";
            for (auto t : rem) cout << t.raw() << " ";

            if (dryrun) cout << "[dryrun]";
            cout << endl;
          }

          vector<ustring> diff;
          set_difference (new_file_tags.begin(),
                          new_file_tags.end (),
                          rem.begin (),
                          rem.end (),
                          back_inserter (diff));
          new_file_tags = diff;
          changed = true;
        }
      }

      /* get file tags with normally ignored kws */
      auto file_tags_all = get_keywords (paths[0], true);
      vector<ustring> diff;
      set_difference (file_tags_all.begin(),
                      file_tags_all.end (),
                      file_tags.begin (),
                      file_tags.end (),
                      back_inserter (diff));
      for (auto t : diff)
        new_file_tags.push_back (t);

      if (changed) {
        for (ustring p : paths) {
          if (more_verbose) {
            cout << "old tags: ";
            for (auto t : file_tags) cout << t.raw() << " ";
            cout << endl;
            cout << "new tags: ";
            for (auto t : new_file_tags) cout << t.raw() << " ";
            cout << endl;
          }

          write_tags (p, new_file_tags);
        }

        /* check maildir flags */
        if (maildir_flags) {
          notmuch_message_tags_to_maildir_flags (message);
        }

        count_changed++;
      }
    } // }}}

    if ((verbose && changed) || more_verbose) {
      cout << "* message (" << count << "), file tags (" << file_tags.size()
           << "): ";
      for (auto t : file_tags) cout << t.raw() << " ";
      cout << ", db tags (" << db_tags.size() << "): ";
      for (auto t : db_tags) cout << t.raw() << " ";
      cout << endl;
    }

    notmuch_message_destroy (message);

    if (more_verbose)
      cout << "==> message (" << count << ") done." << endl;

    count++;
  }

  notmuch_database_close (nm_db);

  chrono::duration<double> elapsed = chrono::steady_clock::now() - t0_c;

  cout << "=> done, checked: " << count << " messages and changed: " << count_changed << " messages in " << ((clock() - gt0) * 1000.0 / CLOCKS_PER_SEC) << " ms [cpu], " << elapsed.count() << " s [real time]." << endl;

  return 0;
}

bool keywords_consistency_check (vector<ustring> &paths, vector<ustring> &file_tags) { // {{{
  /* check if all source files for one message have the same tags, outputs
   * all discovered tags to file_tags */

  bool first = true;
  bool valid = true;

  for (ustring & p : paths) {
    auto t = get_keywords (p, false);

    if (first) {
      first = false;
      file_tags = t;
    } else {

      vector<ustring> diff;
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

vector<ustring> get_keywords (ustring p, bool dont_ignore) { // {{{
  /* get the X-Keywords header from a message and return
   * a _sorted_ vector of strings with the keywords. */

  vector<ustring> file_tags;

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

  char * kws_c = spruce_imap_utf7_utf8(x_keywords);
  ustring kws = ustring(kws_c);

  if (!kws.validate ()) {
    cout << "error: invalid utf8 in keywords" << endl;
  }

  if (more_verbose) {
    cout << "parsing keywords: " << kws.raw() << endl;
  }


  if (enable_split_chars) {
    vector<ustring> initial_tags;
    split_string (initial_tags, kws, ",");

    /* split tags that need splitting into separate tags */
    for (ustring s : split_chars) {
      for (auto t : initial_tags) {
        vector<ustring> k;
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

  if (more_verbose) {
    cout << "tags: ";
    for (auto t : file_tags) {
      cout << t.raw() << " ";
    }
    cout << endl;
  }

  /* do map */
  for (ustring &t : file_tags) {
    for (auto rep : replace_chars) {
      ustring::size_type f = t.find (rep.first);
      if (f != ustring::npos) {
        t.replace (f, 1, 1, rep.second);
      }
    }

    auto fnd = find_if (map_tags.begin(), map_tags.end (),
        [&](pair<ustring,ustring> p) {
          return (t == p.first);
        });

    if (fnd != map_tags.end ()) {
      t = (*fnd).second;
    }
  }

  sort (file_tags.begin (), file_tags.end());

  if (more_verbose) {
    cout << "tags after map: ";
    for (auto t : file_tags) {
      cout << "'" <<  t.raw() << "' ";
    }
    cout << endl;
  }

  if (!dont_ignore) {
    /* remove ignored */
    vector<ustring> diff;
    set_difference (file_tags.begin (),
                    file_tags.end (),
                    ignore_tags.begin (),
                    ignore_tags.end (),
                    back_inserter (diff));


    file_tags = diff;

    if (more_verbose) {
      cout << "tags after ignore: ";
      for (auto t : file_tags) {
        cout << t.raw() << " ";
      }
      cout << endl;
    }
  }

  return file_tags;
} // }}}

void write_tags (ustring path, vector<ustring> tags) { // {{{
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
    if (more_verbose)
      cout << "=> current xkeywords: " << xkeyw << endl;
  } else {
    if (more_verbose)
      cout << "=> current xkeywords: non-existent." << endl;
  }

  /* reverse map */
  for (auto &t : tags) {
    for (auto rep : replace_chars) {
      ustring::size_type f = t.find (rep.second);
      if (f != ustring::npos) {
        t.replace (f, 1, 1, rep.first);
      }
    }

    auto fnd = find_if (map_tags.begin(), map_tags.end (),
        [&](pair<ustring,ustring> p) {
          return (t == p.second);
        });

    if (fnd != map_tags.end ()) {
      t = (*fnd).first;
    }
  }

  sort (tags.begin (), tags.end());

  ustring newh;
  bool first = true;
  for (auto t : tags) {
    if (!first) newh = newh + ",";
    first = false;
    newh = newh + t;
  }

  char * newh_utf7 = spruce_imap_utf8_utf7 (newh.c_str());

  g_mime_object_set_header (GMIME_OBJECT(message), "X-Keywords", newh_utf7);

  char fname[80] = "/tmp/keywsync-XXXXXX";
  int tmpfd = mkstemp (fname);
  FILE * tmpf = fdopen (tmpfd, "w");
  GMimeStream * out = g_mime_stream_file_new (tmpf);
  g_mime_object_write_to_stream (GMIME_OBJECT(message), out);

  g_mime_stream_flush (out);
  g_mime_stream_close (out);

  if (verbose) {
    cout << "new file written to: " << fname << endl;
  }

  /* do move */
  if (verbose) {
    cout << "moving " << fname << " to " << path << endl;
  }

  if (!dryrun) {
    try {
      rename (fname, path.c_str());
    } catch (boost::filesystem::filesystem_error &ex) {
      /* in case of cross-device try regular copy and remove */
      unlink (path.c_str());
      copy (fname, path.c_str());
      unlink (fname);
    }
  } else {
    cout << "dryrun: new file located in: " << fname << endl;
  }

  g_object_unref (message);
  g_object_unref (parser);
  g_mime_stream_close (f);

} // }}}

/* utils {{{ */

template<class T> bool has (vector<T> v, T e) {
  return (find(v.begin (), v.end (), e) != v.end ());
}

void split_string (vector<ustring> & tokens, ustring str, ustring delim) {

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

