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
 * TODO:
 * - tag-to-keyword -> on many x-keywords headers, merge 'em
 *
 */

# include "keywsync.hh"

# include <iostream>
# include <fstream>
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

  /* options {{{ */
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
    ( "only-remove,r", "only remove tags")
    ( "replace-chars", "Replace '/' with '.' and the inverse")
    ( "no-replace-chars", "Do not replace '/' with '.' and the inverse")
    ( "enable-add-x-keywords-for-path", po::value<string>(), "allow adding an X-Keywords header if non-existent, when message file is contained in specified path (do not add a trailing /)" );

  po::variables_map vm;
  po::store ( po::command_line_parser (argc, argv).options(desc).run(), vm );

  if (vm.count ("help")) {
    cout << desc << endl;

    exit (0);
  }

  if (vm.count ("replace-chars") && !vm.count("no-replace-chars")) {
    enable_replace_chars = true;
    cout << "replace chars: true" << endl;
  } else if (!vm.count("replace-chars") && vm.count("no-replace-chars")) {
    enable_replace_chars = false;
    cout << "replace chars: false" << endl;
  } else {
    cout << "error: specify either --replace-chars or --no-replace-chars" << endl;
    exit (1);
  }

  /* load config */
  if (vm.count("database")) {
    db_path = vm["database"].as<string>();
  } else {
    cout << "error: specify database path." << endl;
    exit (1);
  }

  path _db_path (db_path);
  _db_path = boost::filesystem::canonical (_db_path);
  db_path = ustring (_db_path.c_str ());

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

  if (vm.count("enable-add-x-keywords-for-path") > 0) {

    if (direction != TAG_TO_KEYWORD) {
      cerr << "the enable add-x-keywords-for-path option is only allowed for tag-to-keyword sync" << endl;
      exit (1);
    }

    enable_add_x_keywords_header = true;
    add_x_keyw_path = absolute(path (vm["enable-add-x-keywords-for-path"].as<string>()));

    cout << "=> adding x-keywords-header is enabled for: " << add_x_keyw_path << endl;

    if (!exists(add_x_keyw_path)) {
      cerr << "path does not exist!" << endl;
      exit (1);
    }
  }

  if (vm.count ("dry-run")) {
    cout << "=> note: dryrun!" << endl;
    dryrun = true;
  } else {
    // TODO: remove when more confident
    cout << "=> note: real-mode, not dry-run!" << endl;
  }

  more_verbose  = (vm.count("more-verbose") > 0);
  verbose       = (vm.count("verbose") > 0) || more_verbose;
  paranoid      = (vm.count("paranoid") > 0);
  remove_double_x_keywords_header &= !paranoid;
  only_add      = (vm.count("only-add") > 0);
  only_remove   = (vm.count("only-remove") > 0);
  maildir_flags = (vm.count("flags") > 0);

  cout << "=> remove double x-keywords header: " << remove_double_x_keywords_header << endl;

  if (vm.count("mtime") > 0) {
    if (direction != KEYWORD_TO_TAG) {
      cerr << "error: the mtime argument only makes sense for keyword-to-tag sync direction" << endl;
      exit (1);
    }

    mtime_set = true;
    int mtime = vm["mtime"].as<int>();
    time_t mtime_t = mtime;

    only_after_mtime = from_time_t (mtime_t);

    cout << "mtime: only working on messages with mtime newer than: " << to_simple_string(only_after_mtime) << endl;
  }

  if (only_add && only_remove) {
    cerr << "only one of -a or -r can be specified at the same time" << endl;
    exit (1);
  }

  /* }}} */

  /* open db */
  nm_db = setup_db (db_path.c_str());

# ifdef HAVE_NOTMUCH_GET_REV
  const char * uuid;
  unsigned long revision = notmuch_database_get_revision (nm_db, &uuid);
  cout << "* db: current revision: " << revision  << endl;
# endif


  time_t gt0 = clock ();
  chrono::time_point<chrono::steady_clock> t0_c = chrono::steady_clock::now ();

  notmuch_query_t * query;
  query = notmuch_query_create (nm_db,
      inputquery.c_str());

  notmuch_status_t st;
  unsigned int total_messages;
  st = notmuch_query_count_messages_st (query, &total_messages);

  if (st != NOTMUCH_STATUS_SUCCESS) {
    cerr << "db: failed to get message count." << endl;
    exit (1);
  }

  cout << "*  messages to check: " << total_messages << endl;

  notmuch_messages_t * messages;
  st = notmuch_query_search_messages_st (query, &messages);

  if (st != NOTMUCH_STATUS_SUCCESS) {
    cerr << "db: failed to search messages." << endl;
    exit (1);
  }

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

      if ((mtime_set && mtime_changed) || !mtime_set) {
        /* check if we have xkeyw header on this file */
        GMimeStream * f = g_mime_stream_file_new_for_path (fnm, "r");
        if (f == NULL) {
          cerr << "gmime: could not open file: " << fnm << endl;
          exit (1);
        }
        GMimeParser * parser = g_mime_parser_new_with_stream (f);
        GMimeMessage * msg = g_mime_parser_construct_message (parser);
        g_mime_stream_file_set_owner (GMIME_STREAM_FILE(f), true);

        const char * x_keywords = g_mime_object_get_header (GMIME_OBJECT(msg),
            "X-Keywords");
        if (x_keywords == NULL) {
          /* no such field */
          if (enable_add_x_keywords_header) {
            cerr << "warning: no X-Keywords header for file, will be added for file: " << fnm << endl;
          } else {
            cerr << "warning: no X-Keywords header for file, skipping: " << fnm << endl;
            skipped_messages++;
            g_object_unref (parser);
            g_object_unref (f);
            g_mime_stream_close (f);
            continue;
          }
        }

        g_object_unref (parser);
        g_object_unref (f);
        g_mime_stream_close (f);
      }

      paths.push_back (fnm);
      if (more_verbose)
        cout << "* message file: " << fnm << endl;
    }

    notmuch_filenames_destroy (nm_fnms); // }}}

    if (paths.size() == 0) {
      cout << "no files with x-keywords header, skipping message." << endl;
      skipped_messages++;
      count++;
      continue;
    }

    if (mtime_set) {
      if (mtime_changed) {
        if (verbose) {
          cout << "=> " << notmuch_message_get_message_id (message) << " changed, checking.." << endl;
        }

      } else {
        if (more_verbose) {
          cout << "=> message _not_ changed, skipping.." << endl;
        }

        skipped_messages++;
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
        skipped_messages++;
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
        if (more_verbose) {
          cout << "checking maildir flags.." << endl;
        }
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

          if (more_verbose) {
            cout << "file: " << p << endl;
          }
          write_tags (p, new_file_tags);
        }


        count_changed++;
      }

      /* check maildir flags */
      if (maildir_flags) {
        if (more_verbose) {
          cout << "checking maildir flags.." << endl;
        }
        notmuch_message_tags_to_maildir_flags (message);
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

  cout << "=> done, checked: " << count << " messages and changed: " << count_changed << " messages (skipped: " << skipped_messages << ") in " << ((clock() - gt0) * 1000.0 / CLOCKS_PER_SEC) << " ms [cpu], " << elapsed.count() << " s [real time]." << endl;

  return 0;
}

bool keywords_consistency_check (vector<ustring> &paths, vector<ustring> &file_tags) { // {{{
  /* check if all source files for one message have the same tags, outputs
   * all discovered tags to file_tags */

  bool first = true;
  bool valid = true;

  for (ustring & p : paths) {
    path pp (p);
    if (!exists (pp)) {
      cerr << "file does not exist: db out of sync: " << p << endl;
      exit (1);
    }

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

  if (more_verbose) {
    cout << "parsing keywords: " << x_keywords << endl;
  }

  split_string (file_tags, ustring(x_keywords), ",");

  for (ustring &t : file_tags) {
    char * tag_c = spruce_imap_utf7_utf8(t.c_str());
    t = tag_c;

    if (!t.validate ()) {
      cout << "error: invalid utf8 in keywords" << endl;
    }
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
    if (enable_replace_chars) {
      for (auto rep : replace_chars) {
        ustring::size_type f;
        while (f = t.find (rep.first), f != ustring::npos) {
          t.replace (f, 1, 1, rep.second);
        }
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

void write_tags (ustring msg_path, vector<ustring> tags) { // {{{
  /* write tags back to the X-Keywords header */

  /* reverse map */
  for (auto &t : tags) {
    if (enable_replace_chars) {
      for (auto rep : replace_chars) {
        ustring::size_type f;
        while (f = t.find (rep.second), f != ustring::npos) {
          t.replace (f, 1, 1, rep.first);
        }
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

  if (more_verbose) {
    cout << "=> writing new x-keywords: " << newh_utf7 << endl;
  }

  GMimeStream * f = g_mime_stream_file_new_for_path (msg_path.c_str(),
      "r");
  if (f == NULL) {
    cerr << "gmime: could not open file: " << msg_path << endl;
    exit (1);
  }
  GMimeParser * parser = g_mime_parser_new_with_stream (f);
  GMimeObject * prt = g_mime_parser_construct_part (parser);

  const char * headers_raw = g_mime_object_get_headers (prt);

  gint64 header_end = strlen (headers_raw);

  /* find header end */
  /*
  gint64 header_end = g_mime_parser_get_headers_end (parser);
  if (header_end == -1) {
    cerr << "could not find end of header!" << endl;
    exit (1);
  }
  */

  g_object_unref (prt);
  g_object_unref (parser);
  g_mime_stream_close (f);

  stringstream headers_s;
  stringstream contents_s;

  /* read in headers */
  std::ifstream orig (msg_path.c_str());
  std::filebuf * fbuf = orig.rdbuf ();
  char headers_c[header_end+1];
  int read = fbuf->sgetn (headers_c, header_end);
  if (read != header_end) {
    cerr << "could not read until end of header!";
    exit (1);
  }
  headers_c[header_end] = 0;
  headers_s << headers_c;

  /* get rest of contents */
  contents_s << fbuf;
  orig.close ();


  /* scan for X-Keywords header */
  ustring headers (headers_s.str());
  stringstream headers_new;

  bool found_xkeyw = false;

  while (!headers_s.eof ()) {
    string inbuf;
    getline ( headers_s, inbuf );

    int xks = inbuf.find ("X-Keywords");
    if (xks == 0) {
      if (found_xkeyw) {
        if (paranoid) {
          cerr << "found more than one X-Keywords header, failing: "
            << msg_path << endl;
          exit (1);
        } else {
          if (remove_double_x_keywords_header) {
            cerr << "found more than one X-Keywords header, skipping redundant lines.." << endl;
            continue;
          } else {
            cerr << "found more than one X-Keywords header, both are being updated." << endl;
          }
        }
      }

      found_xkeyw = true;

      /* replace */
      if (more_verbose) {
        cout << "=> current xkeywords header: " << inbuf << endl;
      }

      headers_new << "X-Keywords: " << newh_utf7 << endl;


    } else {
      if (inbuf.size() > 0)
        headers_new << inbuf << endl;
    }
  }

  if (!found_xkeyw) {
    cerr << "could not find exisiting X-Keywords header." << endl;
    if (enable_add_x_keywords_header) {
      path m_p = absolute(path(msg_path.c_str()));

      /* test if path is in allowed path */
      bool allowed = false;
      while (m_p != path("/")) {
        if (m_p == add_x_keyw_path) {
          allowed = true;
          break;
        }
        m_p = m_p.parent_path();
      }

      if (allowed) {
        cerr << "adding new X-Keywords header for " << msg_path << endl;
        headers_new << "X-Keywords: " << newh_utf7 << endl;
      } else {
        cerr << "not allowed to add X-Keywords header for: " << msg_path << endl;
      }

    } else {
      exit (1);
    }
  }


  char fname[1024] = "/tmp/keywsync-XXXXXX";
  int tmpfd = mkstemp (fname);

  string headers_new_str = headers_new.str();
  string contents = contents_s.str();

  ssize_t r;
  /* write header */
  r = write (tmpfd, headers_new_str.c_str(), headers_new_str.size());

  if (r == -1) {
    cerr << "failed writing file!" << endl;
    exit (1);
  }

  /* write contents */
  r = write (tmpfd, contents.c_str(), contents.size());

  if (r == -1) {
    cerr << "failed writing file!" << endl;
    exit (1);
  }

  close (tmpfd);

  if (verbose) {
    cout << "new file written to: " << fname << endl;
  }

  /* replace contents */
  if (verbose) {
    cout << "replacing contents from file " << fname << " into " << msg_path << endl;
  }


  if (!dryrun) {
    /* we have to replace the contents of the message file while
     * not updating the creation time to prevent offlineimap from
     * treating the file as a new one (and the previous a deleted one).
     */

    std::ifstream i (fname, ios::binary);
    std::ofstream o (msg_path.c_str(), ios::binary | std::ofstream::trunc);

    o << i.rdbuf ();
    i.close ();
    o.close ();

    unlink (fname);

  } else {
    cout << "dryrun: new file located in: " << fname << endl;
  }

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

