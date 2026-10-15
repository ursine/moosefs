cmake_minimum_required(VERSION 3.20)

project(MooseFS VERSION 4.59.2 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CTest)
include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(ZLIB QUIET)
find_package(PkgConfig QUIET)

option(MFS_BUILD_TESTS "Build MooseFS unit tests" ON)
option(MFS_BUILD_MASTER "Build metadata master and metadata tools" ON)
option(MFS_BUILD_METALOGGER "Build the metalogger" ON)
option(MFS_BUILD_CHUNKSERVER "Build chunkserver tools and daemon" ON)
option(MFS_BUILD_GUI "Build the MooseFS status GUI daemon" ON)
option(MFS_BUILD_CLIENT "Build client tools" ON)
option(MFS_BUILD_MOUNT "Build mfsmount when FUSE is available" ON)
option(MFS_BUILD_BDEV "Build mfsbdev when Linux NBD headers are available" ON)
option(MFS_BUILD_NETDUMP "Build mfsnetdump when libpcap is available" ON)
option(MFS_BUILD_SCRIPTS "Generate the Python CLI script" ON)

set(MFS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(MFS_COMMON_DIR "${MFS_SOURCE_DIR}/mfscommon")
set(MFS_TEST_DIR "${MFS_SOURCE_DIR}/mfstests")
set(MFS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MFS_GENERATED_DIR}")

# Derive target source lists from the same Automake declarations used by the
# primary build, so the two build descriptions do not keep separate copies.
function(mfs_automake_sources out makefile variable)
	file(READ "${MFS_SOURCE_DIR}/${makefile}" contents)
	string(REPLACE "\\\n" " " contents "${contents}")
	string(REGEX MATCH "(^|\n)[ \t]*${variable}[ \t]*=[ \t]*([^\n]*)" assignment "${contents}")
	if(NOT assignment)
		message(FATAL_ERROR "Could not find ${variable} in ${makefile}")
	endif()
	set(words "${CMAKE_MATCH_2}")
	separate_arguments(words UNIX_COMMAND "${words}")
	get_filename_component(am_dir "${makefile}" DIRECTORY)
	set(sources)
	foreach(word IN LISTS words)
		if(word MATCHES "\\.(c|h)$")
			cmake_path(ABSOLUTE_PATH word BASE_DIRECTORY "${MFS_SOURCE_DIR}/${am_dir}" NORMALIZE OUTPUT_VARIABLE source)
			list(APPEND sources "${source}")
		endif()
	endforeach()
	set(${out} "${sources}" PARENT_SCOPE)
endfunction()

function(mfs_add_automake_executable target makefile)
	mfs_automake_sources(sources "${makefile}" "${target}_SOURCES")
	if(NOT sources)
		message(FATAL_ERROR "No sources found for ${target}")
	endif()
	add_executable("${target}" ${sources})
	get_filename_component(am_dir "${makefile}" DIRECTORY)
	target_include_directories("${target}" PRIVATE "${MFS_SOURCE_DIR}/${am_dir}" "${MFS_COMMON_DIR}" "${MFS_GENERATED_DIR}")
	target_compile_definitions("${target}" PRIVATE HAVE_CONFIG_H)
endfunction()

# Autoconf feature probes represented in config.h. The template is retained
# from the original build; detected definitions are appended after its #undefs.
set(feature_headers
	HAVE_ARPA_INET_H=arpa/inet.h HAVE_FCNTL_H=fcntl.h HAVE_INTTYPES_H=inttypes.h
	HAVE_LIMITS_H=limits.h HAVE_NETDB_H=netdb.h HAVE_NETINET_IN_H=netinet/in.h
	HAVE_STDDEF_H=stddef.h HAVE_STDLIB_H=stdlib.h HAVE_STRING_H=string.h
	HAVE_SYS_SOCKET_H=sys/socket.h HAVE_SYS_STATVFS_H=sys/statvfs.h
	HAVE_SYS_TIME_H=sys/time.h HAVE_SYSLOG_H=syslog.h HAVE_UNISTD_H=unistd.h
	HAVE_EXECINFO_H=execinfo.h HAVE_SYS_RESOURCE_H=sys/resource.h HAVE_SYS_MMAN_H=sys/mman.h
	HAVE_SYS_STAT_H=sys/stat.h HAVE_SYS_TYPES_H=sys/types.h HAVE_POLL_H=poll.h
	HAVE_ATTR_XATTR_H=attr/xattr.h HAVE_SYS_XATTR_H=sys/xattr.h HAVE_SYS_FILE_H=sys/file.h
	HAVE_SYS_UTSNAME_H=sys/utsname.h HAVE_MALLOC_H=malloc.h HAVE_SYS_PRCTL_H=sys/prctl.h
	HAVE_LINUX_NBD_H=linux/nbd.h HAVE_LINUX_FS_H=linux/fs.h HAVE_LINUX_OOM_H=linux/oom.h
)
set(config_definitions)
foreach(pair IN LISTS feature_headers)
	string(REPLACE "=" ";" fields "${pair}")
	list(GET fields 0 macro)
	list(GET fields 1 header)
	string(MAKE_C_IDENTIFIER "${macro}" check_name)
	check_include_file("${header}" "${check_name}")
	if(${check_name})
		list(APPEND config_definitions "#define ${macro} 1")
	endif()
endforeach()

set(feature_symbols
	HAVE_ASPRINTF=asprintf HAVE_ATEXIT=atexit HAVE_BACKTRACE=backtrace
	HAVE_BACKTRACE_SYMBOLS=backtrace_symbols HAVE_BZERO=bzero HAVE_CLOCK_GETTIME=clock_gettime
	HAVE_DLCLOSE=dlclose HAVE_DLOPEN=dlopen HAVE_DLSYM=dlsym HAVE_DUP2=dup2
	HAVE_FSEEKO=fseeko HAVE_FTELLO=ftello HAVE_FTRUNCATE=ftruncate
	HAVE_GETADDRINFO=getaddrinfo HAVE_GETCWD=getcwd HAVE_GETPASS=getpass
	HAVE_GETRUSAGE=getrusage HAVE_GETTIMEOFDAY=gettimeofday HAVE_LCHMOD=lchmod
	HAVE_LCHOWN=lchown HAVE_LUTIMES=lutimes HAVE_MALLOPT=mallopt HAVE_MEMMOVE=memmove
	HAVE_MEMSET=memset HAVE_MKDIR=mkdir HAVE_MKSTEMP=mkstemp HAVE_MKTEMP=mktemp
	HAVE_MLOCKALL=mlockall HAVE_MMAP=mmap HAVE_NANOSLEEP=nanosleep HAVE_PERROR=perror
	HAVE_POLL=poll HAVE_POSIX_FADVISE=posix_fadvise HAVE_PRCTL=prctl HAVE_PREAD=pread
	HAVE_PWRITE=pwrite HAVE_READV=readv HAVE_REALPATH=realpath HAVE_SETITIMER=setitimer
	HAVE_SETPROCTITLE=setproctitle HAVE_SOCKET=socket HAVE_STRCHR=strchr HAVE_STRDUP=strdup
	HAVE_STRERROR=strerror HAVE_STRTOL=strtol HAVE_STRTOUL=strtoul HAVE_WRITEV=writev
)
foreach(pair IN LISTS feature_symbols)
	string(REPLACE "=" ";" fields "${pair}")
	list(GET fields 0 macro)
	list(GET fields 1 symbol)
	check_function_exists("${symbol}" "${macro}")
	if(${macro})
		list(APPEND config_definitions "#define ${macro} 1")
	endif()
endforeach()
if(ZLIB_FOUND)
	list(APPEND config_definitions "#define HAVE_ZLIB_H 1")
endif()
check_symbol_exists(mmap "sys/mman.h" MFS_HAVE_MMAP)
check_symbol_exists(readv "sys/uio.h" MFS_HAVE_READV)
check_symbol_exists(writev "sys/uio.h" MFS_HAVE_WRITEV)
check_symbol_exists(malloc "stdlib.h" MFS_HAVE_MALLOC)
check_symbol_exists(realloc "stdlib.h" MFS_HAVE_REALLOC)
check_symbol_exists(reallocf "stdlib.h" MFS_HAVE_REALLOCF)
foreach(pair IN ITEMS "MFS_HAVE_MMAP;HAVE_MMAP" "MFS_HAVE_READV;HAVE_READV" "MFS_HAVE_WRITEV;HAVE_WRITEV" "MFS_HAVE_MALLOC;HAVE_MALLOC" "MFS_HAVE_REALLOC;HAVE_REALLOC" "MFS_HAVE_REALLOCF;HAVE_REALLOCF")
	list(GET pair 0 detected)
	list(GET pair 1 macro)
	if(${detected})
		list(APPEND config_definitions "#define ${macro} 1")
	endif()
endforeach()

configure_file("${MFS_SOURCE_DIR}/config.h.in" "${MFS_GENERATED_DIR}/config.h" COPYONLY)
file(APPEND "${MFS_GENERATED_DIR}/config.h" "\n/* CMake-detected configuration */\n")
foreach(definition IN LISTS config_definitions)
	file(APPEND "${MFS_GENERATED_DIR}/config.h" "${definition}\n")
endforeach()
file(APPEND "${MFS_GENERATED_DIR}/config.h"
	"#define PACKAGE_NAME \"MooseFS\"\n#define PACKAGE_TARNAME \"moosefs\"\n"
	"#define PACKAGE_VERSION \"${PROJECT_VERSION}\"\n#define VERSION \"${PROJECT_VERSION}\"\n"
	"#define VERSMAJ 4\n#define VERSMID 59\n#define VERSMIN (2*2)\n"
	"#define VERSHEX ((4*0x10000)+(59*0x100)+(2*2))\n#define RELEASE 1\n"
	"#define VERSSTR \"${PROJECT_VERSION}-1\"\n#define VERSION2INT(maj,med,min) ((maj)*0x10000+(med)*0x100+(((maj)>1)?((min)*2):(min)))\n"
	"#define PROTO_BASE 0\n#define LIGHT_MFS 0\n#define BUILDNO 0\n"
	"#define DEFAULT_USER \"nobody\"\n#define DEFAULT_GROUP \"\"\n#define DEFAULT_MASTERNAME \"mfsmaster\"\n"
	"#define DEFAULT_PORTBASE 9400\n#define DEFAULT_MASTER_CONTROL_PORT \"9419\"\n"
	"#define DEFAULT_MASTER_CS_PORT \"9420\"\n#define DEFAULT_MASTER_CLIENT_PORT \"9421\"\n"
	"#define DEFAULT_CS_DATA_PORT \"9422\"\n#define DEFAULT_GUI_HTTP_PORT \"9425\"\n#define DEFAULT_CGIDIR \"${CMAKE_INSTALL_FULL_DATADIR}/mfscgi\"\n"
	"#define PREFIX \"${CMAKE_INSTALL_PREFIX}\"\n#define ETC_PATH \"${CMAKE_INSTALL_FULL_SYSCONFDIR}\"\n"
	"#define DATA_PATH \"${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/mfs\"\n#define RUN_PATH \"/var/run/mfs\"\n")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	file(APPEND "${MFS_GENERATED_DIR}/config.h" "#define nobreak __attribute__((fallthrough))\n")
else()
	file(APPEND "${MFS_GENERATED_DIR}/config.h" "#define nobreak\n")
endif()

# Autoconf also materializes these configured data and script files before
# compiling. Keep equivalent build-tree copies for the CMake build.
set(DEFAULT_USER nobody)
set(DEFAULT_GROUP "")
set(DEFAULT_MASTERNAME mfsmaster)
set(DEFAULT_MASTER_CONTROL_PORT 9419)
set(DEFAULT_MASTER_CS_PORT 9420)
set(DEFAULT_MASTER_CLIENT_PORT 9421)
set(DEFAULT_CS_DATA_PORT 9422)
set(DEFAULT_GUI_HTTP_PORT 9425)
set(PROTO_BASE 0)
set(BIN_PATH "${CMAKE_INSTALL_FULL_BINDIR}")
set(SBIN_PATH "${CMAKE_INSTALL_FULL_SBINDIR}")
set(ETC_PATH "${CMAKE_INSTALL_FULL_SYSCONFDIR}")
set(DATA_PATH "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/mfs")
set(DEFAULT_CGIDIR "${CMAKE_INSTALL_FULL_DATADIR}/mfscgi")
foreach(config IN ITEMS mfschunkserver mfsmaster mfsmetalogger mfsmount mfsgui)
	configure_file("${MFS_SOURCE_DIR}/mfsdata/${config}.cfg.in" "${MFS_GENERATED_DIR}/${config}.cfg" @ONLY)
	list(APPEND configured_files "${MFS_GENERATED_DIR}/${config}.cfg")
endforeach()
foreach(script IN ITEMS chart.cgi chartdata.cgi)
	configure_file("${MFS_SOURCE_DIR}/mfsscripts/${script}.in" "${MFS_GENERATED_DIR}/${script}" @ONLY)
	list(APPEND configured_files "${MFS_GENERATED_DIR}/${script}")
endforeach()
configure_file("${MFS_SOURCE_DIR}/mfsscripts/common/constants_ac.py.in" "${MFS_GENERATED_DIR}/constants_ac.py" @ONLY)
list(APPEND configured_files "${MFS_GENERATED_DIR}/constants_ac.py")
foreach(unit IN ITEMS
	moosefs-chunkserver.service moosefs-chunkserver@.service
	moosefs-master.service moosefs-master@.service
	moosefs-metalogger.service moosefs-metalogger@.service
	moosefs-gui.service moosefs-gui@.service)
	configure_file("${MFS_SOURCE_DIR}/systemd/${unit}.in" "${MFS_GENERATED_DIR}/${unit}" @ONLY)
	list(APPEND configured_files "${MFS_GENERATED_DIR}/${unit}")
endforeach()
add_custom_target(mfs_configured_data ALL DEPENDS ${configured_files})

# Tests remain available as a focused CTest group.
if(MFS_BUILD_TESTS)
	mfs_add_automake_executable(mfstest_datapack mfstests/Makefile.am)
	mfs_add_automake_executable(mfstest_clocks mfstests/Makefile.am)
	mfs_add_automake_executable(mfstest_crc32 mfstests/Makefile.am)
	mfs_add_automake_executable(mfstest_bitops mfstests/Makefile.am)
	mfs_add_automake_executable(mfstest_delayrun mfstests/Makefile.am)
	mfs_add_automake_executable(mfstest_timeparser mfstests/Makefile.am)
	foreach(target IN ITEMS mfstest_datapack mfstest_clocks mfstest_crc32 mfstest_bitops mfstest_delayrun mfstest_timeparser)
		target_include_directories(${target} PRIVATE "${MFS_TEST_DIR}")
		if(BUILD_TESTING)
			add_test(NAME ${target} COMMAND ${target})
			set_tests_properties(${target} PROPERTIES SKIP_RETURN_CODE 77)
		endif()
	endforeach()
	target_compile_definitions(mfstest_delayrun PRIVATE _USE_PTHREADS)
	target_link_libraries(mfstest_delayrun PRIVATE Threads::Threads)
endif()

if(MFS_BUILD_MASTER)
	mfs_add_automake_executable(mfsmaster mfsmaster/Makefile.am)
	mfs_add_automake_executable(mfsstatsdump mfsmaster/Makefile.am)
	mfs_add_automake_executable(mfssupervisor mfsmaster/Makefile.am)
	mfs_add_automake_executable(mfsmetadump mfsmetatools/Makefile.am)
	mfs_add_automake_executable(mfsmetadirinfo mfsmetatools/Makefile.am)
	mfs_add_automake_executable(mfsmetasearch mfsmetatools/Makefile.am)
	foreach(target IN ITEMS mfsmaster mfsstatsdump)
		if(ZLIB_FOUND)
			target_link_libraries(${target} PRIVATE ZLIB::ZLIB)
		else()
			message(FATAL_ERROR "${target} requires zlib, as does the Autotools build")
		endif()
	endforeach()
	target_compile_definitions(mfsmaster PRIVATE MFSMAXFILES=16384 APPNAME=mfsmaster)
	target_compile_definitions(mfssupervisor PRIVATE MFSSUPERVISOR=1)
endif()

if(MFS_BUILD_METALOGGER)
	mfs_add_automake_executable(mfsmetalogger mfsmetalogger/Makefile.am)
	target_compile_definitions(mfsmetalogger PRIVATE APPNAME=mfsmetalogger)
endif()

if(MFS_BUILD_CHUNKSERVER)
	foreach(target IN ITEMS mfschunkserver mfschunktool mfscsstatsdump mfschunkdbdump)
		mfs_add_automake_executable(${target} mfschunkserver/Makefile.am)
	endforeach()
	if(ZLIB_FOUND)
		target_link_libraries(mfscsstatsdump PRIVATE ZLIB::ZLIB)
	endif()
	target_compile_definitions(mfschunkserver PRIVATE MFSMAXFILES=16384 _USE_PTHREADS APPNAME=mfschunkserver USE_CONNCACHE USE_IONICE)
	target_link_libraries(mfschunkserver PRIVATE Threads::Threads)
	if(UNIX)
		target_link_libraries(mfschunkserver PRIVATE m)
	endif()
	if(ZLIB_FOUND)
		target_link_libraries(mfschunkserver PRIVATE ZLIB::ZLIB)
	else()
		message(FATAL_ERROR "mfschunkserver requires zlib, as does the Autotools build")
	endif()
endif()

if(MFS_BUILD_GUI)
	mfs_add_automake_executable(mfsgui mfsgui/Makefile.am)
	target_compile_definitions(mfsgui PRIVATE APPNAME=mfsgui)
endif()

if(MFS_BUILD_CLIENT)
	# Shared client command implementation and its front-end programs.
	mfs_automake_sources(tool_sources mfsclient/Makefile.am libmfstools_a_SOURCES)
	add_library(mfstools STATIC ${tool_sources})
	target_include_directories(mfstools PRIVATE "${MFS_COMMON_DIR}" "${MFS_GENERATED_DIR}")
	target_compile_definitions(mfstools PRIVATE HAVE_CONFIG_H)
	foreach(target IN ITEMS mfstrashtool mfstrashtime mfseattr mfsarchive mfsdiagtools mfsquota mfssnapshots mfstrashretention mfssclass mfsscadmin mfspatadmin mfsfacl)
		mfs_add_automake_executable(${target} mfsclient/Makefile.am)
		target_link_libraries(${target} PRIVATE mfstools)
	endforeach()
	target_compile_definitions(mfstrashtool PRIVATE GLOB_TOOLS)

	if(MFS_BUILD_BDEV AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND HAVE_LINUX_NBD_H AND HAVE_LINUX_FS_H)
		mfs_automake_sources(io_sources mfsclient/Makefile.am libmfsio_la_SOURCES)
		add_library(mfsio SHARED ${io_sources})
		set_target_properties(mfsio PROPERTIES VERSION 1.0.0 SOVERSION 1 OUTPUT_NAME mfsio)
		target_include_directories(mfsio PUBLIC "${MFS_SOURCE_DIR}/mfsclient" PRIVATE "${MFS_COMMON_DIR}" "${MFS_GENERATED_DIR}")
		target_compile_definitions(mfsio PRIVATE HAVE_CONFIG_H _USE_PTHREADS LIBMFSIO=1)
		target_link_libraries(mfsio PRIVATE Threads::Threads)
		mfs_add_automake_executable(mfsbdev mfsclient/Makefile.am)
		target_compile_definitions(mfsbdev PRIVATE _USE_PTHREADS MFSNBD=1)
		target_link_libraries(mfsbdev PRIVATE mfsio Threads::Threads)
	elseif(MFS_BUILD_BDEV)
		message(STATUS "mfsbdev disabled: Linux NBD headers were not found")
	endif()

	if(MFS_BUILD_MOUNT AND PkgConfig_FOUND)
		pkg_check_modules(FUSE3 QUIET IMPORTED_TARGET fuse3>=3.2.1)
		if(FUSE3_FOUND)
			file(APPEND "${MFS_GENERATED_DIR}/config.h" "#define HAVE_FUSE3 1\n")
			mfs_add_automake_executable(mfsmount mfsclient/Makefile.am)
			target_compile_definitions(mfsmount PRIVATE _USE_PTHREADS APPNAME=mfsmount MFSMOUNT=1 HAVE_FUSE3)
			target_link_libraries(mfsmount PRIVATE PkgConfig::FUSE3 Threads::Threads)
		else()
			pkg_check_modules(FUSE QUIET IMPORTED_TARGET fuse>=2.6)
			if(FUSE_FOUND)
				mfs_add_automake_executable(mfsmount mfsclient/Makefile.am)
				target_compile_definitions(mfsmount PRIVATE _USE_PTHREADS APPNAME=mfsmount MFSMOUNT=1)
				target_link_libraries(mfsmount PRIVATE PkgConfig::FUSE Threads::Threads)
			else()
				message(STATUS "mfsmount disabled: FUSE 2.6+ or FUSE3 was not found")
			endif()
		endif()
	endif()
endif()

if(MFS_BUILD_NETDUMP AND PkgConfig_FOUND)
	pkg_check_modules(PCAP QUIET IMPORTED_TARGET libpcap)
	if(PCAP_FOUND)
		mfs_add_automake_executable(mfsnetdump mfsnetdump/Makefile.am)
		target_link_libraries(mfsnetdump PRIVATE PkgConfig::PCAP)
		check_symbol_exists(pcap_findalldevs "pcap.h" HAVE_PCAP_FINDALLDEVS)
		if(HAVE_PCAP_FINDALLDEVS)
			target_compile_definitions(mfsnetdump PRIVATE HAVE_PCAP_FINDALLDEVS)
		endif()
	else()
		message(STATUS "mfsnetdump disabled: libpcap was not found")
	endif()
endif()

# Generated Python CLI is an Automake all target when Python is enabled.
if(MFS_BUILD_SCRIPTS)
	find_package(Python3 3.4 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		file(GLOB mfscli_modules CONFIGURE_DEPENDS "${MFS_SOURCE_DIR}/mfsscripts/common/*.py")
		add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mfscli"
			COMMAND "${Python3_EXECUTABLE}" "${MFS_SOURCE_DIR}/cmake/GenerateMfsCli.py"
				"${MFS_SOURCE_DIR}/mfsscripts/mfscli.py" "${CMAKE_CURRENT_BINARY_DIR}/mfscli"
			DEPENDS "${MFS_SOURCE_DIR}/mfsscripts/mfscli.py" "${MFS_SOURCE_DIR}/cmake/GenerateMfsCli.py" ${mfscli_modules}
			VERBATIM)
		add_custom_target(mfscli_script ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/mfscli")
	endif()
endif()
