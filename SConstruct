# SCons build system file

# get operating system info
import os

os.umask(022)

# determine the os and machine names
uname    = os.uname();
platform = uname[0]
machine  = uname[4]
osname   = platform + '_' +  machine
archDir  = '.' + osname
print "OSNAME = ", osname

# Create an environment while importing the user's PATH.
# This allows us to get to the vxworks compiler for example.
env = Environment(ENV = {'PATH' : os.environ['PATH']})

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
doVX = GetOption('doVX')
print "doVX =", doVX
Help('--vx                cross compile for vxworks\n')

# 64 bit option
AddOption('--64bits',
           dest='use64bits',
           default=False,
           action='store_true')
use64bits = GetOption('use64bits')
print "use64bits =", use64bits
Help('--64bits            compile 64 bit libs and executables\n')

# install directory option
AddOption('--prefix',
           dest='prefix',
           nargs=1,
           default='',
           action='store')
prefix = GetOption('prefix')
Help('--prefix=<dir>      use base directory <dir> when doing install\n')

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

# Specifying vxworks overrides local operating system build
if doVX:
    osname  = platform + '_VX'
    archDir = '.' + osname
    print "\nDoing vxworks\n"

# set our install directories
libDir = prefix + "/" + osname + '/lib'
binDir = prefix + "/" + osname + '/bin'
incDir = prefix + '/include'
print 'binDir = ', binDir
print 'libDir = ', libDir
print 'incDir = ', incDir

# use "install" on command line to install libs & headers
Help('install             install libs & headers\n')

# use "examples" on command line to install executable examples
Help('examples            install executable examples\n')

# create needed install directories
if not os.path.exists(incDir):
    Execute(Mkdir(incDir))
if not os.path.exists(libDir):
    Execute(Mkdir(libDir))
if not os.path.exists(binDir):
    Execute(Mkdir(binDir))

###############
# COMPILE FLAGS
###############
# debug/optimization flags
if debug:
    env.Append(CCFLAGS = '-g')

# platform dependent quantities
execLibs = ['pthread', 'dl', 'rt']  # default to standard Unix libs
if platform == 'SunOS':
    env.Append(CCFLAGS = '-mt')    
    if doVX:
        vxbase = os.getenv('WIND_BASE', '/site/vxworks/5.5/ppc')
        vxbin = vxbase + '/sun4-solaris/bin'
#    env.Append(CCFLAGS = '{-mt}')    
elif platform == 'Darwin':
    env.Append(CPPDEFINES = 'Darwin', SHLINKFLAGS = '-multiply_defined suppress -flat_namespace -undefined suppress')
elif platform == 'Linux':
    if doVX:
        vxbase = os.getenv('WIND_BASE', '/site/vxworks/5.5/ppc')
        vxbin = vxbase + '/x86-linux/bin'


vxInc = ''
if doVX:
    env.Append(CPPDEFINES = ['CPU=PPC604', 'VXWORKS', '_GNU_TOOL', 'VXWORKSPPC', 'POSIX_MISTAKE'])
    env.Append(CCFLAGS = '-fno-for-scope -fno-builtin -fvolatile -fstrength-reduce -mlongcall -mcpu=604')
    vxInc = vxbase + '/target/h'
    env['CC']     = 'ccppc'
    env['CXX']    = 'g++ppc'
    env['SHLINK'] = 'ldppc'
    env['AR']     = 'arppc'
    env['RANLIB'] = 'ranlibppc'
    
#    env.Append(CCFLAGS = '-O3')
#if not use64bits:
#    env.Append(CCFLAGS = '-m32', LINKFLAGS = '-m32')
# SHCCFLAGS = '-m32'

#########################
# lower level scons files
#########################

# make available to lower level scons files
Export('env incDir libDir binDir archDir execLibs doVX vxInc')

# run lower level build files
env.SConscript('src/regexp/SConscript',   variant_dir='src/regexp/'+archDir,   duplicate=0)
env.SConscript('src/libsrc/SConscript',   variant_dir='src/libsrc/'+archDir,   duplicate=0)
env.SConscript('src/libsrc++/SConscript', variant_dir='src/libsrc++/'+archDir, duplicate=0)
env.SConscript('src/examples/SConscript', variant_dir='src/examples/'+archDir, duplicate=0)
env.SConscript('src/execsrc/SConscript',  variant_dir='src/execsrc/'+archDir,  duplicate=0)
