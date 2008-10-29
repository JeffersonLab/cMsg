# SCons build system file

# get operating system info
import os
import string
import SCons.Node.FS

os.umask(022)

# cMsg version
versionMajor = '3'
versionMinor = '0'

# determine the os and machine names
uname    = os.uname();
platform = uname[0]
machine  = uname[4]
osname   = platform + '-' +  machine

# Create an environment while importing the user's PATH.
# This allows us to get to the vxworks compiler for example,
# so for vxworks, make sure the tools are in your PATH
env = Environment(ENV = {'PATH' : os.environ['PATH']})

################################
# 64 or 32 bit operating system?
################################
    
# Define configure-type test function to
# see if operating system is 64 or 32 bits
def CheckHas64Bits(context, flags):
    context.Message( 'Checking for 64/32 bits ...' )
    lastCCFLAGS = context.env['CCFLAGS']
    lastLFLAGS  = context.env['LINKFLAGS']
    context.env.Append(CCFLAGS = flags, LINKFLAGS = flags)
    # (C) program to run to check for bits
    ret = context.TryRun("""
#include <stdio.h>
int main(int argc, char **argv) {
  printf(\"%d\", 8*sizeof(0L));
  return 0;
}
""", '.c')
    # restore original flags
    context.env.Replace(CCFLAGS = lastCCFLAGS, LINKFLAGS = lastLFLAGS)
    # if program successfully ran ...
    if ret[0]:
        context.Result(ret[1])
        if ret[1] == '64':
            return 64
        return 32
    # else if program did not run ...
    else:
        # Don't know if it's a 32 or 64 bit operating system
        context.Result('failed')
        return 0

# How many bits is the operating system?
# For Linux 64 bit x86 machines, the "machine' variable is x86_64,
# but for Darwin or Solaris there is no obvious check so run
# a configure-type test.
is64bits = False
if platform == 'Linux':
    if machine == 'x86_64':
        is64bits = True
elif platform == 'Darwin' or platform == 'SunOS':
    ccflags = '-xarch=amd64'
    if platform == 'Darwin':
        ccflags = '-arch x86_64'
    # run the test
    conf = Configure( env, custom_tests = { 'CheckBits' : CheckHas64Bits } )
    ret = conf.CheckBits(ccflags)
    env = conf.Finish()
    if ret < 1:
        print 'Cannot run test, assume 32 bit system'
    elif ret == 64:
        print 'Found 64 bit system'
        is64bits = True;
    else:
        print 'Found 32 bit system'


#########################################
# add command line options (try scons -h)
#########################################

# debug option
AddOption('--dbg',
           dest='cmsgDebug',
           default=False,
           action='store_true')
debug = GetOption('cmsgDebug')
print "debug =", debug
Help('\ncmsg scons OPTIONS:\n')
Help('--dbg               compile with debug flag\n')

# vxworks option
AddOption('--vx',
           dest='doVX',
           default=False,
           action='store_true')
useVxworks = GetOption('doVX')
print "useVxworks =", useVxworks
Help('--vx                cross compile for vxworks\n')

# 32 bit option
AddOption('--32bits',
           dest='use32bits',
           default=False,
           action='store_true')
use32bits = GetOption('use32bits')
print "use32bits =", use32bits
Help('--32bits            compile 32bit libs & executables on 64bit system\n')

# install directory option
AddOption('--prefix',
           dest='prefix',
           nargs=1,
           default='',
           action='store')
prefix = GetOption('prefix')
Help('--prefix=<dir>      use base directory <dir> when doing install\n')

###############
# COMPILE FLAGS
###############
# debug/optimization flags
if debug:
    env.Append(CCFLAGS = '-g')
elif platform == 'SunOS':
    env.Append(CCFLAGS = '-xO3')
else:
    env.Append(CCFLAGS = '-O3')

vxInc = ''
execLibs = ['']

# If using vxworks
if useVxworks:
    print "\nDoing vxworks\n"
    osname = platform + '-vxppc'

    if platform == 'Linux':
        vxbase = os.getenv('WIND_BASE', '/site/vxworks/5.5/ppc')
        vxbin = vxbase + '/x86-linux/bin'
    elif platform == 'SunOS':
        vxbase = os.getenv('WIND_BASE', '/site/vxworks/5.5/ppc')
        print "WIND_BASE = ", vxbase
        vxbin = vxbase + '/sun4-solaris/bin'
        if machine == 'i86pc':
            print '\nVxworks compilation not allowed on x86 solaris\n'
            raise SystemExit
    else:
        print '\nVxworks compilation not allowed on ' + platform + '\n'
        raise SystemExit
                    
    env.Append(CPPPATH = vxbase + '/target/h')
    env.Append(CCFLAGS = '-fno-for-scope -fno-builtin -fvolatile -fstrength-reduce -mlongcall -mcpu=604')
    env.Append(CPPDEFINES = ['CPU=PPC604', 'VXWORKS', '_GNU_TOOL', 'VXWORKSPPC', 'POSIX_MISTAKE'])
    env['CC']     = 'ccppc'
    env['CXX']    = 'g++ppc'
    env['SHLINK'] = 'ldppc'
    env['AR']     = 'arppc'
    env['RANLIB'] = 'ranlibppc'
    use32bits = True

# else if NOT using vxworks
else:
    # platform dependent quantities
    execLibs = ['pthread', 'dl', 'rt']  # default to standard Linux libs
    if platform == 'SunOS':
        env.Append(CCFLAGS = '-mt')
        env.Append(CPPDEFINES = ['_REENTRANT', '_POSIX_PTHREAD_SEMANTICS', '_GNU_SOURCE', 'SunOS'])
        execLibs = ['m', 'posix4', 'pthread', 'socket', 'dl']
        if is64bits and not use32bits:
            if machine == 'sun4u':
                env.Append(CCFLAGS = '-xarch=native64 -xcode=pic32',
                           #LIBPATH = '/lib/64', # to do this we need to pass LIBPATH to lower level
                           LINKFLAGS = '-xarch=native64 -xcode=pic32')
            else:
                env.Append(CCFLAGS = '-xarch=amd64',
                           #LIBPATH = ['/lib/64', '/usr/ucblib/amd64'],
                           LINKFLAGS = '-xarch=amd64')
    
    elif platform == 'Darwin':
        execLibs = ['pthread', 'dl']
        env.Append(CPPDEFINES = 'Darwin', SHLINKFLAGS = '-multiply_defined suppress -flat_namespace -undefined suppress')
        env.Append(CCFLAGS = '-fmessage-length=0')
        if is64bits and not use32bits:
            env.Append(CCFLAGS = '-arch x86_64',
                       LINKFLAGS = '-arch x86_64 -Wl,-bind_at_load')
    
    elif platform == 'Linux':
        if is64bits and use32bits:
            env.Append(CCFLAGS = '-m32', LINKFLAGS = '-m32')
    
    if not is64bits and not use32bits:
        use32bits = True

if not use32bits:
    osname = osname + '-64'

print "OSNAME = ", osname
archDir  = '.' + osname

###############
# INSTALL STUFF
###############
    
# Any user specifed command line installation path overrides default
if prefix == '':
    # determine install directories since nothing on command line
    codaDirEnv    = os.getenv('CODA_HOME',"")
    installDirEnv = os.getenv('INSTALL_DIR', "")    
    if installDirEnv == "":
        if codaDirEnv == "":
            print "Need to define either CODA_HOME or INSTALL_DIR"
            raise SystemExit
        else:
            prefix = codaDirEnv
    else:
        prefix = installDirEnv
    print "Default install directory = ", prefix
else:
    print 'Cmdline install directory = ', prefix

# set our install directories
libDir = prefix + "/" + osname + '/lib'
binDir = prefix + "/" + osname + '/bin'
incDir = prefix + '/include'
print 'binDir = ', binDir
print 'libDir = ', libDir
print 'incDir = ', incDir

# use "install" on command line to install libs & headers
Help('install             install libs & headers\n')

# use "uninstall" on command line to uninstall libs, headers, and executables
#Help('uninstall           uninstall everything that was installed\n')

# use "examples" on command line to install executable examples
Help('examples            install executable examples\n')

# create needed install directories
if not os.path.exists(incDir):
    Execute(Mkdir(incDir))
if not os.path.exists(libDir):
    Execute(Mkdir(libDir))
if not os.path.exists(binDir):
    Execute(Mkdir(binDir))

#########################
# Tar file
#########################

# function that does the tar
def tarballer(target, source, env):
    dirname = os.path.basename(os.path.abspath('.'))
    cmd = 'tar -X tar/tarexclude -C .. -c -z -f ' + str(target[0]) + ' ./' + dirname
    p = os.popen(cmd)
    return p.close()

# name of tarfile
tarfile = 'tar/cMsg-' + versionMajor + '.' + versionMinor + '.tgz'

# tarfile builder
tarBuild = Builder(action = tarballer)
env.Append(BUILDERS = {'Tarball' : tarBuild})
env.Alias('tar', env.Tarball(target = tarfile, source = []))

# use "tar" on command line to create tar file
Help('tar                 create tar file (in cmsg/tar)\n')

#########################
# lower level scons files
#########################

# make available to lower level scons files
Export('env incDir libDir binDir archDir execLibs tarfile')

# run lower level build files
env.SConscript('src/regexp/SConscript',   variant_dir='src/regexp/'+archDir,   duplicate=0)
env.SConscript('src/libsrc/SConscript',   variant_dir='src/libsrc/'+archDir,   duplicate=0)
env.SConscript('src/libsrc++/SConscript', variant_dir='src/libsrc++/'+archDir, duplicate=0)

# for vxworks only make libs and examples
if useVxworks:
    env.SConscript('src/examples/sconscript', variant_dir='src/examples/'+archDir, duplicate=0)
else:
    env.SConscript('src/examples/SConscript', variant_dir='src/examples/'+archDir, duplicate=0)
    env.SConscript('src/execsrc/SConscript',  variant_dir='src/execsrc/'+archDir,  duplicate=0)
