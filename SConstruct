import os, sys
from subprocess import *

def getGitDesc():
  return Popen('git describe --abbrev=8 --tags --always', stdout=PIPE, shell=True).stdout.read ().strip ()


GIT_DESC = getGitDesc ()
print "building " + GIT_DESC + ".."
env = Environment ()

# Verbose / Non-verbose output{{{
colors = {}
colors['cyan']   = '\033[96m'
colors['purple'] = '\033[95m'
colors['blue']   = '\033[94m'
colors['green']  = '\033[92m'
colors['yellow'] = '\033[93m'
colors['red']    = '\033[91m'
colors['end']    = '\033[0m'

#If the output is not a terminal, remove the colors
if not sys.stdout.isatty():
   for key, value in colors.iteritems():
      colors[key] = ''

compile_source_message = '%scompiling %s==> %s$SOURCE%s' % \
   (colors['blue'], colors['purple'], colors['yellow'], colors['end'])

compile_shared_source_message = '%scompiling shared %s==> %s$SOURCE%s' % \
   (colors['blue'], colors['purple'], colors['yellow'], colors['end'])

link_program_message = '%slinking Program %s==> %s$TARGET%s' % \
   (colors['red'], colors['purple'], colors['yellow'], colors['end'])

link_library_message = '%slinking Static Library %s==> %s$TARGET%s' % \
   (colors['red'], colors['purple'], colors['yellow'], colors['end'])

ranlib_library_message = '%sranlib Library %s==> %s$TARGET%s' % \
   (colors['red'], colors['purple'], colors['yellow'], colors['end'])

link_shared_library_message = '%slinking Shared Library %s==> %s$TARGET%s' % \
   (colors['red'], colors['purple'], colors['yellow'], colors['end'])

java_compile_source_message = '%scompiling %s==> %s$SOURCE%s' % \
   (colors['blue'], colors['purple'], colors['yellow'], colors['end'])

java_library_message = '%screating Java Archive %s==> %s$TARGET%s' % \
   (colors['red'], colors['purple'], colors['yellow'], colors['end'])

AddOption("--verbose",action="store_true", dest="verbose_flag",default=False,help="verbose output")
if not GetOption("verbose_flag"):
  env["CXXCOMSTR"] = compile_source_message,
  env["CCCOMSTR"] = compile_source_message,
  env["SHCCCOMSTR"] = compile_shared_source_message,
  env["SHCXXCOMSTR"] = compile_shared_source_message,
  env["ARCOMSTR"] = link_library_message,
  env["RANLIBCOMSTR"] = ranlib_library_message,
  env["SHLINKCOMSTR"] = link_shared_library_message,
  env["LINKCOMSTR"] = link_program_message,
  env["JARCOMSTR"] = java_library_message,
  env["JAVACCOMSTR"] = java_compile_source_message,

# Verbose }}}

# set up compiler and linker from env variables
if os.environ.has_key('CC'):
  env['CC'] = os.environ['CC']
if os.environ.has_key('CFLAGS'):
  env['CCFLAGS'] += SCons.Util.CLVar(os.environ['CFLAGS'])
if os.environ.has_key('CXX'):
  env['CXX'] = os.environ['CXX']
if os.environ.has_key('CXXFLAGS'):
  env['CXXFLAGS'] += SCons.Util.CLVar(os.environ['CXXFLAGS'])
if os.environ.has_key('CPPFLAGS'):
  env['CCFLAGS'] += SCons.Util.CLVar(os.environ['CPPFLAGS'])
if os.environ.has_key('LDFLAGS'):
  env['LINKFLAGS'] += SCons.Util.CLVar(os.environ['LDFLAGS'])

nmenv = env.Copy()

def CheckPKGConfig(context, version):
  context.Message( 'Checking for pkg-config... ' )
  ret = context.TryAction('pkg-config --atleast-pkgconfig-version=%s' % version)[0]
  context.Result( ret )
  return ret

def CheckPKG(context, name):
  context.Message( 'Checking for %s... ' % name )
  ret = context.TryAction('pkg-config --exists \'%s\'' % name)[0]
  context.Result( ret )
  return ret


nm_db_get_revision_test_src = """
# include <notmuch.h>

int main (int argc, char ** argv)
{
  notmuch_database_t * nm_db;
  notmuch_database_get_revision (nm_db);

  return 0;
}
"""

# http://www.scons.org/doc/1.2.0/HTML/scons-user/x4076.html
def check_notmuch_get_revision (ctx):
  ctx.Message ("Checking for C function notmuch_database_get_revision()..")
  result = ctx.TryCompile (nm_db_get_revision_test_src, '.cpp')
  ctx.Result (result)
  return result

conf = Configure(env, custom_tests = { 'CheckPKGConfig' : CheckPKGConfig,
                                       'CheckPKG' : CheckPKG,
                                       'CheckNotmuchGetRev' : check_notmuch_get_revision})

if not conf.CheckPKGConfig('0.15.0'):
  print 'pkg-config >= 0.15.0 not found.'
  Exit(1)

if not conf.CheckPKG('glibmm-2.4'):
  print "glibmm-2.4 not found."
  Exit (1)

if not conf.CheckPKG('gmime-2.6'):
  print "gmime-2.6 not found."
  Exit (1)

if not conf.CheckLibWithHeader ('notmuch', 'notmuch.h', 'c'):
  print "notmuch does not seem to be installed."
  Exit (1)

# external libraries
env.ParseConfig ('pkg-config --libs --cflags glibmm-2.4')
env.ParseConfig ('pkg-config --libs --cflags gmime-2.6')

if not conf.CheckLib ('boost_filesystem', language = 'c++'):
  print "boost_filesystem does not seem to be installed."
  Exit (1)

if not conf.CheckLib ('boost_system', language = 'c++'):
  print "boost_system does not seem to be installed."
  Exit (1)

if not conf.CheckLib ('boost_program_options', language = 'c++'):
  print "boost_program_options does not seem to be installed."
  Exit (1)

if not conf.CheckLib ('boost_date_time', language = 'c++'):
  print "boost_date_time does not seem to be installed."
  Exit (1)

if conf.CheckNotmuchGetRev ():
  have_get_rev = True
else:
  have_get_rev = False
  print "notmuch_database_get_revision() not available. building notmuch_get_revision will be disabled. please get a notmuch with lastmod capabilities to ensure a speedier sync."


libs   = ['notmuch',
          'boost_filesystem',
          'boost_system',
          'boost_program_options',
          'boost_date_time']

env.AppendUnique (LIBS = libs)
cenv = env.Copy (CFLAGS = ['-g', '-Wall'])
env.AppendUnique (CPPFLAGS = ['-g', '-Wall', '-std=c++11', '-pthread'] )

# write version file
print ("writing version.hh..")
vfd = open ('version.hh', 'w')
vfd.write ("# pragma once\n")
vfd.write ("# define GIT_DESC \"%s\"\n\n" % GIT_DESC)
vfd.close ()

env = conf.Finish ()

spruce = cenv.Object ('spruce-imap-utils.c')
env.Program (source = [ 'keywsync.cc', spruce ], target = 'keywsync')
env.Alias ('build', ['keywsync'])

if have_get_rev:
  nmenv.AppendUnique (LIBS = ['notmuch'])
  nmenv.Program ('notmuch_get_revision', source = [ 'notmuch_get_revision.cc' ])
  nmenv.Alias ('build', ['notmuch_get_revision'])

