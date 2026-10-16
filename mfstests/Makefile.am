TESTS = mfstest_datapack mfstest_clocks mfstest_crc32 mfstest_bitops mfstest_delayrun mfstest_timeparser mfstest_tools_common

AM_CPPFLAGS = -I$(top_srcdir)/mfscommon

noinst_PROGRAMS = $(TESTS)

mfstest_datapack_SOURCES = \
	mfstest_datapack.c mfstest.h \
	../mfscommon/datapack.h

mfstest_datapack_CFLAGS =

mfstest_clocks_SOURCES = \
	mfstest_clocks.c mfstest.h \
	../mfscommon/portable.h \
	../mfscommon/clocks.h ../mfscommon/clocks.c

mfstest_clocks_CFLAGS =

mfstest_crc32_SOURCES = \
	mfstest_crc32.c mfstest.h \
	../mfscommon/crc.h ../mfscommon/crc.c \
	../mfscommon/clocks.h ../mfscommon/clocks.c

mfstest_crc32_CFLAGS =

mfstest_bitops_SOURCES = \
	mfstest_bitops.c mfstest.h \
	../mfscommon/bitops.h \
	../mfscommon/clocks.h ../mfscommon/clocks.c

mfstest_bitops_CFLAGS =

mfstest_delayrun_SOURCES = \
	mfstest_delayrun.c mfstest.h \
	../mfscommon/portable.h \
	../mfscommon/delayrun.h ../mfscommon/delayrun.c \
	../mfscommon/mfslog.h ../mfscommon/mfslog.c \
	../mfscommon/clocks.h ../mfscommon/clocks.c \
	../mfscommon/strerr.h ../mfscommon/strerr.c

mfstest_delayrun_LDADD = $(PTHREAD_LIBS)
mfstest_delayrun_CFLAGS = $(PTHREAD_CFLAGS) -D_USE_PTHREADS
mfstest_delayrun_CPPFLAGS = $(PTHREAD_CPPFLAGS) -I$(top_srcdir)/mfscommon

mfstest_timeparser_SOURCES = \
	mfstest_timeparser.c mfstest.h \
	../mfscommon/timeparser.h ../mfscommon/timeparser.c

mfstest_tools_common_SOURCES = \
	mfstest_tools_common.c mfstest.h \
	../mfsclient/tools_common.c ../mfsclient/tools_common.h \
	../mfsclient/tools_packetserializer.c ../mfsclient/tools_packetserializer.h \
	../mfscommon/labelparser.c ../mfscommon/labelparser.h \
	../mfscommon/timeparser.c ../mfscommon/timeparser.h \
	../mfscommon/clocks.c ../mfscommon/clocks.h \
	../mfscommon/sockets.c ../mfscommon/sockets.h \
	../mfscommon/strerr.c ../mfscommon/strerr.h \
	../mfscommon/mfslog.c ../mfscommon/mfslog.h \
	../mfscommon/md5.c ../mfscommon/md5.h \
	../mfscommon/datapack.h ../mfscommon/mfsstrerr.h \
	../mfscommon/idstr.h ../mfscommon/MFSCommunication.h

mfstest_tools_common_CPPFLAGS = -I$(top_srcdir)/mfsclient

distclean-local: distclean-am
	-rm -rf ./$(DEPDIR)
	-rm -f Makefile
