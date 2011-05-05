import os
from glob import glob
from os import unlink, symlink, popen, uname, environ
from os.path import exists
from shutil import copy2 as copy
from subprocess import call

# node-wafadmin
import Options
import Utils

TARGET = '_mapnik'
TARGET_FILE = '%s.node' % TARGET
built = 'build/default/%s' % TARGET_FILE
dest = 'lib/%s' % TARGET_FILE
settings = 'lib/mapnik_settings.js'

# only works with Mapnik2/trunk..
# make False to guess at Mapnik 0.7.x configuration (your mileage may vary)
AUTOCONFIGURE = True

# detect this install: http://dbsgeo.com/downloads/#mapnik200
HAS_OSX_FRAMEWORK = False

# this goes into a mapnik_settings.js file beside the C++ _mapnik.node
settings_template = """
module.exports.paths = {
    'fonts': '%s',
    'input_plugins': '%s',
};
"""

# number of parallel compile jobs
jobs=1
if os.environ.has_key('JOBS'):
  jobs = int(os.environ['JOBS'])

def write_mapnik_settings(fonts='',input_plugins=''):
    open(settings,'w').write(settings_template % (fonts,input_plugins))

def ensure_min_mapnik_revision(conf,revision=2397):
    # mapnik-config was basically written for node-mapnik
    # so a variety of kinks mean that we need a very
    # recent version for things to work properly
    # http://trac.mapnik.org/log/trunk/utils/mapnik-config

    #TODO - if we require >=2503 then we can check return type not "Usage" string...
    if popen("%s --libs" % conf.env['MAPNIK_CONFIG']).read().startswith('Usage') \
      or popen("%s --input-plugins" % conf.env['MAPNIK_CONFIG']).read().startswith('Usage') \
      or popen("%s --svn-revision" % conf.env['MAPNIK_CONFIG']).read().startswith('Usage'):
        Utils.pprint('YELLOW', 'mapnik-config version is too old, mapnik > %s is required for auto-configuring build' % revision)
        conf.fatal('please upgrade to mapnik trunk')

    failed = False
    found_ver = None

    try:
        found_ver = int(popen("%s --svn-revision" % conf.env['MAPNIK_CONFIG']).readline().strip())
        if not found_ver >= revision:
            failed = True
            print found_ver,revision
        else:
            Utils.pprint('GREEN', 'Sweet, found viable mapnik svn-revision r%s (via mapnik-config)' % (found_ver))
    except Exception,e:
        print e
        failed = True

    if failed:
        if found_ver:
            msg = 'mapnik-config version is too old, mapnik > r%s is required for auto-configuring build, found only r%s' % (revision,found_ver)
        else:
            msg = 'mapnik-config version is too old, mapnik > r%s is required for auto-configuring build' % revision

        Utils.pprint('YELLOW', msg)
        conf.fatal('please upgrade to mapnik trunk')


def set_options(opt):
    opt.tool_options("compiler_cxx")
    #opt.add_option('-D', '--debug', action='store_true', default=False, dest='debug')

def configure(conf):
    global AUTOCONFIGURE
    global HAS_OSX_FRAMEWORK

    conf.check_tool("compiler_cxx")
    conf.check_tool("node_addon")
    settings_dict = {}
    cairo_cxxflags = []
    grid_cxxflags = []

    # use mapnik-config to build against mapnik2/trunk
    if AUTOCONFIGURE:

        # future auto-support for mapnik frameworks..
        path_list = environ.get('PATH', '').split(os.pathsep)
        if os.path.exists('/Library/Frameworks/Mapnik.framework'):
            path_list.append('/Library/Frameworks/Mapnik.framework/Programs')
            HAS_OSX_FRAMEWORK = True

        mapnik_config = conf.find_program('mapnik-config', var='MAPNIK_CONFIG', path_list=path_list)
        if not mapnik_config:
            conf.fatal('\n\nSorry, the "mapnik-config" program was not found.\nOnly Mapnik Trunk (future Mapnik 2.0 release) provides this tool, and therefore node-mapnik requires Mapnik trunk.\n\nSee http://trac.mapnik.org/wiki/Mapnik2 for more info.\n')
            
        # this breaks with git cloned mapnik repos, so skip it
        #ensure_min_mapnik_revision(conf)

        # todo - check return value of popen otherwise we can end up with
        # return of 'Usage: mapnik-config [OPTION]'
        all_ldflags = popen("%s --libs" % mapnik_config).readline().strip().split(' ')

        # only link to libmapnik, which should be in first two flags
        linkflags = all_ldflags[:2]

        # add prefix to linkflags if it is unique
        prefix_lib = os.path.join(conf.env['PREFIX'],'lib')
        if not '/usr/local' in prefix_lib:
            linkflags.insert(0,'-L%s' % prefix_lib)

        #linkflags.append('-F/Library/')
        #linkflags.append('-framework Mapnik')
        #linkflags.append('-Z')

        conf.env.append_value("LINKFLAGS", linkflags)

        # uneeded currently as second item from mapnik-config is -lmapnik2
        #conf.env.append_value("LIB_MAPNIK", "mapnik2")

        if '-lcairo' in all_ldflags:

            if HAS_OSX_FRAMEWORK and os.path.exists('/Library/Frameworks/Mapnik.framework/Headers/cairo'):
                # prep for this specific install of mapnik 1.0: http://dbsgeo.com/downloads/#mapnik200
                cairo_cxxflags.append('-I/Library/Frameworks/Mapnik.framework/Headers/cairomm-1.0')
                cairo_cxxflags.append('-I/Library/Frameworks/Mapnik.framework/Headers/cairo')
                cairo_cxxflags.append('-I/Library/Frameworks/Mapnik.framework/Headers/sigc++-2.0')
                cairo_cxxflags.append('-I/Library/Frameworks/Mapnik.framework/unix/lib/sigc++-2.0/include')
                cairo_cxxflags.append('-I/Library/Frameworks/Mapnik.framework/Headers') #fontconfig
                Utils.pprint('GREEN','Sweet, found cairo library, will attempt to compile with cairo support for pdf/svg output')
            else:
                pkg_config = conf.find_program('pkg-config', var='PKG_CONFIG', path_list=path_list, mandatory=False)
                if not pkg_config:
                    Utils.pprint('YELLOW','pkg-config not found, building Cairo support into Mapnik is not available')
                else:
                    cmd = '%s cairomm-1.0' %  pkg_config
                    if not int(call(cmd.split(' '))) >= 0:
                        Utils.pprint('YELLOW','"pkg-config --cflags cairomm-1.0" failed, building Cairo support into Mapnik is not available')
                    else:
                        Utils.pprint('GREEN','Sweet, found cairo library, will attempt to compile with cairo support for pdf/svg output')
                        cairo_cxxflags.extend(popen("pkg-config --cflags cairomm-1.0").readline().strip().split(' '))
        else:
            Utils.pprint('YELLOW','Notice: "mapnik-config --libs" is not reporting Cairo support in your mapnik version, so node-mapnik will not be built with Cairo support (pdf/svg output)')


        # TODO - too much potential pollution here, need to limit this upstream
        cxxflags = popen("%s --cflags" % mapnik_config).readline().strip().split(' ')
        if os.path.exists('/Library/Frameworks/Mapnik.framework'):
            cxxflags.insert(0,'-I/Library/Frameworks/Mapnik.framework/Versions/2.0/unix/include/freetype2')
        
        # if cairo is available
        if cairo_cxxflags:
            cxxflags.append('-DHAVE_CAIRO')
            cxxflags.extend(cairo_cxxflags)
        
        # add prefix to includes if it is unique
        prefix_inc = os.path.join(conf.env['PREFIX'],'include/node')
        if not '/usr/local' in prefix_inc:
            cxxflags.insert(0,'-I%s' % prefix_inc)

        conf.env.append_value("CXXFLAGS_MAPNIK", cxxflags)

        #ldflags = []
        #conf.env.append_value("LDFLAGS", ldflags)

        # settings for fonts and input plugins
        settings_dict['input_plugins'] = popen("%s --input-plugins" % mapnik_config).readline().strip()
        settings_dict['fonts'] = popen("%s --fonts" % mapnik_config).readline().strip()

    else:

        prefix_guess = "/usr/local"

        import platform
        if platform.dist()[0] in ('Ubuntu','debian'):
            LIBDIR_SCHEMA='lib'
        elif platform.uname()[4] == 'x86_64' and platform.system() == 'Linux':
            LIBDIR_SCHEMA='lib64'
        elif platform.uname()[4] == 'ppc64':
            LIBDIR_SCHEMA='lib64'
        else:
            LIBDIR_SCHEMA='lib'

        MAPNIK2 = False
        # manual configure of Mapnik 0.7.x, as only trunk provides a mapnik-config

        # mapnik 0.7.x
        if MAPNIK2:
            # libmapnik2
            conf.env.append_value("LIB_MAPNIK", "mapnik2")
        else:
            conf.env.append_value("LIB_MAPNIK", "mapnik")

        # add path of mapnik lib
        conf.env.append_value("LIBPATH_MAPNIK", "%s/%s" % (prefix_guess,LIBDIR_SCHEMA))
        ldflags = []
        if platform.uname()[0] == "Darwin":
            ldflags.append('-L/usr/X11/lib/')
        #libs = ['-lboost_thread','-lboost_regex','-libpng12','-libjpeg']
        #if uname()[0] == 'Darwin':
        #   pass #libs.append('-L/usr/X11/lib/','-libicuuc')
        if ldflags:
            conf.env.append_value("LDFLAGS", ldflags)

        # add path of mapnik headers
        conf.env.append_value("CPPPATH_MAPNIK", "%s/include" % prefix_guess)

        # add paths for freetype, boost, icu, as needed
        cxxflags = popen("freetype-config --cflags").readline().strip().split(' ')
        cxxflags.append('-I/usr/include')
        cxxflags.append('-I/usr/local/include')
        conf.env.append_value("CXXFLAGS_MAPNIK", cxxflags)

        # settings for fonts and input plugins
        if MAPNIK2:
            settings_dict['input_plugins'] = "%s/%s/mapnik2/input" % (prefix_guess,LIBDIR_SCHEMA)
            settings_dict['fonts'] = "%s/%s/mapnik2/fonts" % (prefix_guess,LIBDIR_SCHEMA)
        else:
            settings_dict['input_plugins'] = "%s/lib/mapnik/input" % prefix_guess
            settings_dict['fonts'] = "%s/lib/mapnik/fonts" % prefix_guess

    write_mapnik_settings(**settings_dict)

def build(bld):
    Options.options.jobs = jobs;
    obj = bld.new_task_gen("cxx", "shlib", "node_addon", install_path=None)
    obj.cxxflags = ["-DNDEBUG", "-O3", "-g", "-Wall", "-ansi","-finline-functions","-Wno-inline","-DHAVE_JPEG","-DBOOST_SPIRIT_THREADSAFE","-DMAPNIK_THREADSAFE","-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE"]
    obj.target = TARGET
    obj.source = "src/_mapnik.cc "
    obj.source += "src/grid/renderer.cpp "
    obj.source += "src/mapnik_js_datasource.cpp "
    obj.source += "src/mapnik_memory_datasource.cpp "
    obj.source += "src/mapnik_map.cpp "
    obj.source += "src/mapnik_projection.cpp "
    obj.source += "src/mapnik_layer.cpp "
    obj.source += "src/mapnik_datasource.cpp "
    obj.source += "src/mapnik_featureset.cpp "
    obj.uselib = "MAPNIK"
    # install 'mapnik' module
    lib_dir = bld.path.find_dir('./lib')
    bld.install_files('${LIBPATH_NODE}/node/mapnik', lib_dir.ant_glob('**/*'), cwd=lib_dir, relative_trick=True)
    # install command line programs
    bin_dir = bld.path.find_dir('./bin')
    bld.install_files('${PREFIX_NODE}/bin', bin_dir.ant_glob('*'), cwd=bin_dir, relative_trick=True, chmod=0755)


def shutdown():
    if Options.commands['clean']:
        if exists(TARGET): unlink(TARGET)
    if Options.commands['clean']:
        if exists(dest):
            unlink(dest)
    else:
        if exists(built):
            copy(built,dest)
