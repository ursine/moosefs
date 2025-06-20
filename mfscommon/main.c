/*
 * Copyright (C) 2025 Jakub Kruszona-Zawadzki, Saglabs SA
 * 
 * This file is part of MooseFS.
 * 
 * MooseFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 (only).
 * 
 * MooseFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with MooseFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02111-1301, USA
 * or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(_THREAD_SAFE) || defined(_REENTRANT) || defined(_USE_PTHREADS)
#  define USE_PTHREADS 1
#endif

#ifndef MFSMAXFILES
#define MFSMAXFILES 4096
#endif

#if defined(HAVE_MLOCKALL)
#  if defined(HAVE_SYS_MMAN_H)
#    include <sys/mman.h>
#  endif
#  if defined(HAVE_SYS_RESOURCE_H)
#    include <sys/resource.h>
#  endif
#  if defined(RLIMIT_MEMLOCK) && defined(MCL_CURRENT) && defined(MCL_FUTURE)
#    define MFS_USE_MEMLOCK 1
#  endif
#endif

#if defined(HAVE_MALLOC_H)
#  include <malloc.h>
#endif

#if defined(HAVE_LINUX_OOM_H)
#  include <linux/oom.h>
#  if defined(OOM_DISABLE) || defined(OOM_SCORE_ADJ_MIN)
#    define OOM_ADJUSTABLE 1
#  endif
#endif

#if defined(HAVE_SYS_PRCTL_H)
#  include <sys/prctl.h>
#endif

#include <syslog.h>

#ifdef USE_IONICE
#include "ionice.h"
#endif

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/resource.h>
#include <grp.h>
#include <pwd.h>
#ifdef USE_PTHREADS
#include <pthread.h>
#endif

#define STR_AUX(x) #x
#define STR(x) STR_AUX(x)

#include "idstr.h"
#include "main.h"
#include "clocks.h"
#include "sockets.h"
#include "cfg.h"
#include "strerr.h"
#include "crc.h"
#include "init.h"
#include "massert.h"
#include "mfslog.h"
#include "portable.h"
#include "processname.h"

static double lcall_start,lcall_end;
static double lcall_trigger = 0.0;

static double loop_usleep = 0;

#define LOOP_START if (lcall_trigger>0.0) { \
	lcall_start = monotonic_seconds(); \
}

#define LOOP_END(name) if (lcall_trigger>0.0) { \
	lcall_end = monotonic_seconds(); \
	if (lcall_end-lcall_start > lcall_trigger) { \
		mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"long call detected: %s : %.2lfms",name,(lcall_end-lcall_start)*1000.0); \
	} \
}

#define RM_RESTART 0
#define RM_START 1
#define RM_STOP 2
#define RM_RELOAD 3
#define RM_INFO 4
#define RM_TEST 5
#define RM_KILL 6
#define RM_TRY_RESTART 7
#define RM_RESTORE 8

typedef struct deentry {
	void (*fun)(void);
	char *fname;
	struct deentry *next;
} deentry;

static deentry *dehead=NULL;


typedef struct meentry {
	int (*fun)(void);
	char *fname;
	struct meentry *next;
} meentry;

static meentry *mehead=NULL;


typedef struct weentry {
	void (*fun)(void);
	char *fname;
	struct weentry *next;
} weentry;

static weentry *wehead=NULL;


typedef struct ceentry {
	int (*fun)(void);
	char *fname;
	struct ceentry *next;
} ceentry;

static ceentry *cehead=NULL;


typedef struct rlentry {
	void (*fun)(void);
	char *fname;
	struct rlentry *next;
} rlentry;

static rlentry *rlhead=NULL;


typedef struct inentry {
	void (*fun)(FILE *);
	char *fname;
	struct inentry *next;
} inentry;

static inentry *inhead=NULL;


typedef struct kaentry {
	void (*fun)(void);
	char *fname;
	struct kaentry *next;
} kaentry;

static kaentry *kahead=NULL;


typedef struct pollentry {
	void (*desc)(struct pollfd *,uint32_t *);
	void (*serve)(struct pollfd *);
	char *dname;
	char *sname;
	struct pollentry *next;
} pollentry;

static pollentry *pollhead=NULL;


typedef struct eloopentry {
	void (*fun)(void);
	char *fname;
	struct eloopentry *next;
} eloopentry;

static eloopentry *eloophead=NULL;


typedef struct chldentry {
	pid_t pid;
	void (*fun)(pid_t,int);
	char *fname;
	struct chldentry *next;
} chldentry;

static chldentry *chldhead=NULL;


typedef struct timeentry {
	uint64_t nextevent;
	uint64_t useconds;
	uint64_t usecoffset;
	void (*fun)(void);
	char *fname;
	struct timeentry *next;
} timeentry;

static timeentry *timehead=NULL;

#ifdef USE_PTHREADS
static pthread_mutex_t nowlock = PTHREAD_MUTEX_INITIALIZER;
#endif
static uint32_t now;
static uint32_t start_time;
static uint64_t usecnow;
static double loop_start;
//static int alcnt=0;

static int signalpipe[2];

/* interface */

void main_destruct_register_fname (void (*fun)(void),const char *fname) {
	deentry *aux=(deentry*)malloc(sizeof(deentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = dehead;
	dehead = aux;
}

void main_mayexit_register_fname (int (*fun)(void),const char *fname) {
	meentry *aux=(meentry*)malloc(sizeof(meentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = mehead;
	mehead = aux;
}

void main_wantexit_register_fname (void (*fun)(void),const char *fname) {
	weentry *aux=(weentry*)malloc(sizeof(weentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = wehead;
	wehead = aux;
}

void main_canexit_register_fname (int (*fun)(void),const char *fname) {
	ceentry *aux=(ceentry*)malloc(sizeof(ceentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = cehead;
	cehead = aux;
}

void main_reload_register_fname (void (*fun)(void),const char *fname) {
	rlentry *aux=(rlentry*)malloc(sizeof(rlentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = rlhead;
	rlhead = aux;
}

void main_info_register_fname (void (*fun)(FILE *),const char *fname) {
	inentry *aux=(inentry*)malloc(sizeof(inentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = inhead;
	inhead = aux;
}

void main_keepalive_register_fname (void (*fun)(void),const char *fname) {
	kaentry *aux=(kaentry*)malloc(sizeof(kaentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = kahead;
	kahead = aux;
}

void main_poll_register_fname (void (*desc)(struct pollfd *,uint32_t *),void (*serve)(struct pollfd *),const char *dname,const char *sname) {
	pollentry *aux=(pollentry*)malloc(sizeof(pollentry));
	passert(aux);
	aux->desc = desc;
	aux->serve = serve;
	aux->dname = strdup(dname);
	aux->sname = strdup(sname);
	aux->next = pollhead;
	pollhead = aux;
}

void main_eachloop_register_fname (void (*fun)(void),const char *fname) {
	eloopentry *aux=(eloopentry*)malloc(sizeof(eloopentry));
	passert(aux);
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = eloophead;
	eloophead = aux;
}

void main_chld_register_fname (pid_t pid,void (*fun)(pid_t,int),const char *fname) {
	chldentry *aux=(chldentry*)malloc(sizeof(chldentry));
	passert(aux);
	aux->pid = pid;
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = chldhead;
	chldhead = aux;
}

void* main_msectime_register_fname (uint32_t mseconds,uint32_t offset,void (*fun)(void),const char *fname) {
	timeentry *aux;
	uint64_t useconds = UINT64_C(1000) * (uint64_t)mseconds;
	uint64_t usecoffset = UINT64_C(1000) * (uint64_t)offset;
	if (useconds==0 || usecoffset>=useconds) {
		return NULL;
	}
	aux = (timeentry*)malloc(sizeof(timeentry));
	passert(aux);
	aux->nextevent = (((usecnow / useconds) * useconds) + usecoffset);
	while (aux->nextevent<usecnow) {
		aux->nextevent+=useconds;
	}
	aux->useconds = useconds;
	aux->usecoffset = usecoffset;
	aux->fun = fun;
	aux->fname = strdup(fname);
	aux->next = timehead;
	timehead = aux;
	return aux;
}

int main_msectime_change(void* x,uint32_t mseconds,uint32_t offset) {
	timeentry *aux = (timeentry*)x;
	uint64_t useconds = UINT64_C(1000) * (uint64_t)mseconds;
	uint64_t usecoffset = UINT64_C(1000) * (uint64_t)offset;
	if (useconds==0 || usecoffset>=useconds) {
		return -1;
	}
	aux->nextevent = (((usecnow / useconds) * useconds) + usecoffset);
	while (aux->nextevent<usecnow) {
		aux->nextevent+=useconds;
	}
	aux->useconds = useconds;
	aux->usecoffset = usecoffset;
	return 0;
}

void* main_time_register_fname (uint32_t seconds,uint32_t offset,void (*fun)(void),const char *fname) {
	return main_msectime_register_fname(1000*seconds,1000*offset,fun,fname);
}

int main_time_change(void* x,uint32_t seconds,uint32_t offset) {
	return main_msectime_change(x,1000*seconds,1000*offset);
}

/* internal */

void free_all_registered_entries(void) {
	deentry *de,*den;
	ceentry *ce,*cen;
	weentry *we,*wen;
	rlentry *re,*ren;
	inentry *ie,*ien;
	pollentry *pe,*pen;
	eloopentry *ee,*een;
	timeentry *te,*ten;

	for (de = dehead ; de ; de = den) {
		den = de->next;
		free(de->fname);
		free(de);
	}

	for (ce = cehead ; ce ; ce = cen) {
		cen = ce->next;
		free(ce->fname);
		free(ce);
	}

	for (we = wehead ; we ; we = wen) {
		wen = we->next;
		free(we->fname);
		free(we);
	}

	for (re = rlhead ; re ; re = ren) {
		ren = re->next;
		free(re->fname);
		free(re);
	}

	for (ie = inhead ; ie ; ie = ien) {
		ien = ie->next;
		free(ie->fname);
		free(ie);
	}

	for (pe = pollhead ; pe ; pe = pen) {
		pen = pe->next;
		free(pe->dname);
		free(pe->sname);
		free(pe);
	}

	for (ee = eloophead ; ee ; ee = een) {
		een = ee->next;
		free(ee->fname);
		free(ee);
	}

	for (te = timehead ; te ; te = ten) {
		ten = te->next;
		free(te->fname);
		free(te);
	}
}

int canexit(void) {
	int r;
	ceentry *aux;

	for (aux = cehead ; aux!=NULL ; aux=aux->next) {
		LOOP_START;
		r = aux->fun();
		LOOP_END(aux->fname);
		if (r==0) {
			return 0;
		}
	}
	return 1;
}

uint32_t main_time_refresh(void) {
	struct timeval tv;
	uint32_t res;

	gettimeofday(&tv,NULL);
#ifdef USE_PTHREADS
	zassert(pthread_mutex_lock(&nowlock));
#endif
	res = now = tv.tv_sec;
#ifdef USE_PTHREADS
	zassert(pthread_mutex_unlock(&nowlock));
#endif
	return res;
}

uint32_t main_time(void) {
#ifdef USE_PTHREADS
	uint32_t ret;
	zassert(pthread_mutex_lock(&nowlock));
	ret = now;
	zassert(pthread_mutex_unlock(&nowlock));
	return ret;
#else
	return now;
#endif
}

uint64_t main_utime(void) {
	struct timeval tv;
	uint64_t usec;

	gettimeofday(&tv,NULL);
	usec = tv.tv_sec;
	usec *= 1000000;
	usec += tv.tv_usec;
	return usec;
}

uint32_t main_start_time(void) {
#ifdef USE_PTHREADS
	uint32_t ret;
	zassert(pthread_mutex_lock(&nowlock));
	ret = start_time;
	zassert(pthread_mutex_unlock(&nowlock));
	return ret;
#else
	return start_time;
#endif
}

static inline void destruct(void) {
	deentry *deit;

	for (deit = dehead ; deit!=NULL ; deit=deit->next ) {
		LOOP_START;
		deit->fun();
		LOOP_END(deit->fname);
	}
}

void main_keep_alive(void) {
	double loop_end;
	struct timeval tv;
	kaentry *kait;

	loop_end = monotonic_seconds();
	if (loop_end - loop_start > 5.0) {
		mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"long loop detected (%.3lfs)",loop_end-loop_start);
	}
	loop_start = loop_end;
	gettimeofday(&tv,NULL);
	usecnow = tv.tv_sec;
	usecnow *= 1000000;
	usecnow += tv.tv_usec;
#ifdef USE_PTHREADS
	zassert(pthread_mutex_lock(&nowlock));
#endif
	now = tv.tv_sec;
#ifdef USE_PTHREADS
	zassert(pthread_mutex_unlock(&nowlock));
#endif
	for (kait = kahead ; kait!=NULL ; kait=kait->next ) {
		LOOP_START;
		kait->fun();
		LOOP_END(kait->fname);
	}
}

void main_reload(void) {
	static int firstflag = 0;
	char *logminlevelstr;
	char *logelevatetostr;
	int tmp;

	lcall_trigger = cfg_getdouble("LONG_CALL_TRIGGER_MS",0.0) / 1000.0; // debug option
	loop_usleep = cfg_getuint32("DEBUG_LOOP_SLEEP_MS",0) * 1000; // debug option

	logminlevelstr = cfg_getstr("SYSLOG_MIN_LEVEL","INFO");
	logelevatetostr = cfg_getstr("SYSLOG_ELEVATE_TO","NOTICE");

	tmp = mfs_log_str_to_pri(logminlevelstr);
	if (tmp>=0) {
		mfs_log_set_min_level(tmp);
	} else {
		if (firstflag==0) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"error parsing SYSLOG_MIN_LEVEL option - using INFO");
			mfs_log_set_min_level(MFSLOG_INFO);
		} else {
			mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"error parsing SYSLOG_MIN_LEVEL option - left unchanged");
		}
	}
	free(logminlevelstr);

	tmp = mfs_log_str_to_pri(logelevatetostr);
	if (tmp>=0) {
		mfs_log_set_elevate_to(tmp);
	} else {
		if (firstflag==0) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"error parsing SYSLOG_ELEVATE_TO option - using NOTICE");
			mfs_log_set_elevate_to(MFSLOG_NOTICE);
		} else {
			mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"error parsing SYSLOG_ELEVATE_TO option - left unchanged");
		}
	}
	free(logelevatetostr);

	firstflag = 1;
}

int mainloop(void) {
	uint64_t prevtime = 0;
	struct timeval tv;
	pollentry *pollit;
	eloopentry *eloopit;
	timeentry *timeit;
	meentry *meit;
	weentry *weit;
	ceentry *ceit;
	rlentry *rlit;
	inentry *init;
	FILE *infile;
	struct pollfd *pdesc;
	uint32_t ndesc;
	double loop_end;
	int i;
	int t,r;
	int status = 0;

	t = 0;
	r = 0;
	pdesc = malloc(sizeof(struct pollfd)*MFSMAXFILES);
	loop_start = monotonic_seconds();
	while (t!=3) {
		if (loop_usleep) {
			portable_usleep(loop_usleep);
		}
		ndesc=1;
		pdesc[0].fd = signalpipe[0];
		pdesc[0].events = POLLIN;
		pdesc[0].revents = 0;
		for (pollit = pollhead ; pollit != NULL ; pollit = pollit->next) {
			LOOP_START;
			pollit->desc(pdesc,&ndesc);
			LOOP_END(pollit->dname);
		}
		loop_end = monotonic_seconds();
		if (loop_end - loop_start > 5.0) {
			mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"long loop detected (%.3lfs)",loop_end-loop_start);
		}
		i = poll(pdesc,ndesc,10);
		loop_start = monotonic_seconds();
		gettimeofday(&tv,NULL);
		usecnow = tv.tv_sec;
		usecnow *= 1000000;
		usecnow += tv.tv_usec;
#ifdef USE_PTHREADS
		zassert(pthread_mutex_lock(&nowlock));
#endif
		now = tv.tv_sec;
#ifdef USE_PTHREADS
		zassert(pthread_mutex_unlock(&nowlock));
#endif
		if (i<0) {
			if (!ERRNO_ERROR) {
				mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"poll returned EAGAIN");
				portable_usleep(10000);
				continue;
			}
			if (errno!=EINTR) {
				mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"poll error: %s",strerr(errno));
				break;
			}
		} else if (i>0) {
			if ((pdesc[0].revents)&POLLIN) {
				uint8_t sigid;
				if (read(signalpipe[0],&sigid,1)==1) {
					if (sigid=='\001' && t==0) {
						mfs_log(MFSLOG_SYSLOG,MFSLOG_NOTICE,"terminate signal received");
						t = 1;
					} else if (sigid=='\002') {
						mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"reloading config files");
						r = 1;
					} else if (sigid=='\003') {
#ifndef SILENT_SIGCHLD
						mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"child finished");
#endif
						r = 2;
					} else if (sigid=='\004') {
						mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"log extra info");
						r = 3;
					} else if (sigid=='\005') {
						mfs_log(MFSLOG_SYSLOG,MFSLOG_NOTICE,"unexpected alarm/prof signal received - ignoring");
					} else if (sigid=='\006') {
						mfs_log(MFSLOG_SYSLOG,MFSLOG_NOTICE,"internal terminate request");
						t = 1;
						status = 1;
					}
				}
			}
			for (pollit = pollhead ; pollit != NULL ; pollit = pollit->next) {
				LOOP_START;
				pollit->serve(pdesc);
				LOOP_END(pollit->sname);
			}
		}
		for (eloopit = eloophead ; eloopit != NULL ; eloopit = eloopit->next) {
			LOOP_START;
			eloopit->fun();
			LOOP_END(eloopit->fname);
		}
		if (usecnow<prevtime) {
			// time went backward !!! - recalculate "nextevent" time
			// adding previous_time_to_run prevents from running next event too soon.
			for (timeit = timehead ; timeit != NULL ; timeit = timeit->next) {
				uint64_t previous_time_to_run = timeit->nextevent - prevtime;
				if (previous_time_to_run > timeit->useconds) {
					previous_time_to_run = timeit->useconds;
				}
				timeit->nextevent = ((usecnow / timeit->useconds) * timeit->useconds) + timeit->usecoffset;
				while (timeit->nextevent <= usecnow+previous_time_to_run) {
					timeit->nextevent += timeit->useconds;
				}
			}
		} else if (usecnow>prevtime+UINT64_C(5000000)) {
			// time went forward !!! - just recalculate "nextevent" time
			for (timeit = timehead ; timeit != NULL ; timeit = timeit->next) {
				timeit->nextevent = ((usecnow / timeit->useconds) * timeit->useconds) + timeit->usecoffset;
				while (usecnow >= timeit->nextevent) {
					timeit->nextevent += timeit->useconds;
				}
			}
		}
		for (timeit = timehead ; timeit != NULL ; timeit = timeit->next) {
			if (usecnow >= timeit->nextevent) {
				uint32_t eventcounter = 0;
				while (usecnow >= timeit->nextevent && eventcounter<10) { // do not run more than 10 late entries
					LOOP_START;
					timeit->fun();
					LOOP_END(timeit->fname);
					timeit->nextevent += timeit->useconds;
					eventcounter++;
				}
				if (usecnow >= timeit->nextevent) {
					timeit->nextevent = ((usecnow / timeit->useconds) * timeit->useconds) + timeit->usecoffset;
					while (usecnow >= timeit->nextevent) {
						timeit->nextevent += timeit->useconds;
					}
				}
			}
		}
		prevtime = usecnow;
		if (r==1) {
			cfg_reload();
			main_reload();
			for (rlit = rlhead ; rlit!=NULL ; rlit=rlit->next ) {
				LOOP_START;
				rlit->fun();
				LOOP_END(rlit->fname);
			}
			r = 0;
		} else if (r==2) {
			chldentry *chldit,**chldptr;
			pid_t pid;
			int st;

			while ( (pid = waitpid(-1,&st,WNOHANG)) > 0) {
				chldptr = &chldhead;
				while ((chldit = *chldptr)) {
					if (chldit->pid == pid) {
						LOOP_START;
						chldit->fun(pid,st);
						LOOP_END(chldit->fname);
						*chldptr = chldit->next;
						free(chldit);
					} else {
						chldptr = &(chldit->next);
					}
				}
			}
			r = 0;
		} else if (r==3) {
			infile = fopen("." STR(APPNAME) "_info.txt","w");
			if (infile==NULL) {
				mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"can't create info file");
			} else {
				fprintf(infile,"[general]\n");
				fprintf(infile,"version: %s\n",VERSSTR);
				fprintf(infile,"build: %s\n",STR(BUILDNO));
				fprintf(infile,"timestamp: %ld\n",(long int)time(NULL));
				fprintf(infile,"\n");
				cfg_info(infile);
				for (init = inhead ; init!=NULL ; init=init->next ) {
					LOOP_START;
					init->fun(infile);
					LOOP_END(init->fname);
				}
				fclose(infile);
			}
			r = 0;
		}
		if (t==1) {
			i = 1;
			for (meit = mehead ; meit!=NULL && i ; meit=meit->next ) {
				LOOP_START;
				if (meit->fun()==0) {
					i=0;
				}
				LOOP_END(meit->fname);
			}
			if (i==1) {
				for (weit = wehead ; weit!=NULL ; weit=weit->next ) {
					LOOP_START;
					weit->fun();
					LOOP_END(weit->fname);
				}
				t = 2;
			}
		}
		if (t==2) {
			i = 1;
			for (ceit = cehead ; ceit!=NULL && i ; ceit=ceit->next ) {
				LOOP_START;
				if (ceit->fun()==0) {
					i=0;
				}
				LOOP_END(ceit->fname);
			}
			if (i) {
				t = 3;
			}
		}
	}
	free(pdesc);
	return status;
}

int initialize(void) {
	uint32_t i;
	int ok;
	ok = 1;
	for (i=0 ; (long int)(RunTab[i].fn)!=0 && ok ; i++) {
#ifdef USE_PTHREADS
		zassert(pthread_mutex_lock(&nowlock));
#endif
		now = time(NULL);
#ifdef USE_PTHREADS
		zassert(pthread_mutex_unlock(&nowlock));
#endif
		if (RunTab[i].fn()<0) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"init: %s failed !!!",RunTab[i].name);
			ok=0;
		}
	}
	return ok;
}

int restore(void) {
	uint32_t i;
	int ok;
	ok = 1;
	for (i=0 ; (long int)(RestoreRunTab[i].fn)!=0 && ok ; i++) {
#ifdef USE_PTHREADS
		zassert(pthread_mutex_lock(&nowlock));
#endif
		now = time(NULL);
#ifdef USE_PTHREADS
		zassert(pthread_mutex_unlock(&nowlock));
#endif
		if (RestoreRunTab[i].fn()<0) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"restore: %s failed !!!",RestoreRunTab[i].name);
			ok=0;
		}
	}
	return ok;
}

int initialize_late(void) {
	uint32_t i;
	int ok;
	ok = 1;
	for (i=0 ; (long int)(LateRunTab[i].fn)!=0 && ok ; i++) {
#ifdef USE_PTHREADS
		zassert(pthread_mutex_lock(&nowlock));
#endif
		now = time(NULL);
#ifdef USE_PTHREADS
		zassert(pthread_mutex_unlock(&nowlock));
#endif
		if (LateRunTab[i].fn()<0) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"init: %s failed !!!",LateRunTab[i].name);
			ok=0;
		}
	}
#ifdef USE_PTHREADS
	zassert(pthread_mutex_lock(&nowlock));
#endif
	start_time = now = time(NULL);
#ifdef USE_PTHREADS
	zassert(pthread_mutex_unlock(&nowlock));
#endif
	return ok;
}



/* signals */

static int termsignal[]={
	SIGTERM,
	-1
};

static int reloadsignal[]={
	SIGHUP,
	-1
};

static int infosignal[]={
#ifdef SIGINFO
	SIGINFO,
#endif
#ifdef SIGUSR1
	SIGUSR1,
#endif
	-1
};

static int chldsignal[]={
#ifdef SIGCHLD
	SIGCHLD,
#endif
#ifdef SIGCLD
	SIGCLD,
#endif
	-1
};

static int ignoresignal[]={
	SIGQUIT,
#ifdef SIGPIPE
	SIGPIPE,
#endif
#ifdef SIGTSTP
	SIGTSTP,
#endif
#ifdef SIGTTIN
	SIGTTIN,
#endif
#ifdef SIGTTOU
	SIGTTOU,
#endif
#ifdef SIGUSR2
	SIGUSR2,
#endif
	-1
};

static int alarmsignal[]={
#ifndef GPERFTOOLS
#ifdef SIGALRM
	SIGALRM,
#endif
#ifdef SIGVTALRM
	SIGVTALRM,
#endif
#ifdef SIGPROF
	SIGPROF,
#endif
#endif
	-1
};

static int daemonignoresignal[]={
	SIGINT,
	-1
};

void termhandle(int signo) {
	signo = write(signalpipe[1],"\001",1); // killing two birds with one stone - use signo and do something with value returned by write :)
	(void)signo; // and then use this value to calm down compiler ;)
}

void reloadhandle(int signo) {
	signo = write(signalpipe[1],"\002",1); // see above
	(void)signo;
}

void chldhandle(int signo) {
	signo = write(signalpipe[1],"\003",1); // see above
	(void)signo;
}

void infohandle(int signo) {
	signo = write(signalpipe[1],"\004",1); // see above
	(void)signo;
}

void alarmhandle(int signo) {
	signo = write(signalpipe[1],"\005",1); // see above
	(void)signo;
}

void set_signal_handlers(int daemonflag) {
	struct sigaction sa;
	uint32_t i;

	zassert(pipe(signalpipe));

#ifdef SA_RESTART
	sa.sa_flags = SA_RESTART;
#else
	sa.sa_flags = 0;
#endif
	sigemptyset(&sa.sa_mask);

	sa.sa_handler = termhandle;
	for (i=0 ; termsignal[i]>0 ; i++) {
		sigaction(termsignal[i],&sa,(struct sigaction *)0);
	}
	sa.sa_handler = reloadhandle;
	for (i=0 ; reloadsignal[i]>0 ; i++) {
		sigaction(reloadsignal[i],&sa,(struct sigaction *)0);
	}
	sa.sa_handler = infohandle;
	for (i=0 ; infosignal[i]>0 ; i++) {
		sigaction(infosignal[i],&sa,(struct sigaction *)0);
	}
	sa.sa_handler = alarmhandle;
	for (i=0 ; alarmsignal[i]>0 ; i++) {
		sigaction(alarmsignal[i],&sa,(struct sigaction *)0);
	}
	sa.sa_handler = chldhandle;
	for (i=0 ; chldsignal[i]>0 ; i++) {
		sigaction(chldsignal[i],&sa,(struct sigaction *)0);
	}
	sa.sa_handler = SIG_IGN;
	for (i=0 ; ignoresignal[i]>0 ; i++) {
		sigaction(ignoresignal[i],&sa,(struct sigaction *)0);
	}
	sa.sa_handler = daemonflag?SIG_IGN:termhandle;
	for (i=0 ; daemonignoresignal[i]>0 ; i++) {
		sigaction(daemonignoresignal[i],&sa,(struct sigaction *)0);
	}
}

void main_syslog_wrapper(int priority,const char *msg) {
	syslog(priority,"%s",msg);
}

void main_exit(void) {
	int i;
	i = write(signalpipe[1],"\006",1);
	(void)i;
}

void signal_cleanup(void) {
	close(signalpipe[0]);
	close(signalpipe[1]);
}

void changeugid(void) {
	char pwdgrpbuff[16384];
	struct passwd pwd,*pw;
	struct group grp,*gr;
	char *wuser;
	char *wgroup;
	uid_t wrk_uid;
	gid_t wrk_gid;
	int gidok;

	if (geteuid()==0) {
		wuser = cfg_getstr("WORKING_USER",DEFAULT_USER);
		wgroup = cfg_getstr("WORKING_GROUP",DEFAULT_GROUP);

		gidok = 0;
		wrk_gid = -1;
		if (wgroup[0]=='#') {
			wrk_gid = strtol(wgroup+1,NULL,10);
			gidok = 1;
		} else if (wgroup[0]) {
			if (getgrnam_r(wgroup,&grp,pwdgrpbuff,16384,&gr)!=0) {
				gr = NULL;
			}
			if (gr==NULL) {
				mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"%s: no such group !!!",wgroup);
				exit(1);
			}
			wrk_gid = gr->gr_gid;
			gidok = 1;
		}

		if (wuser[0]=='#') {
			wrk_uid = strtol(wuser+1,NULL,10);
			if (gidok==0) {
				getpwuid_r(wrk_uid,&pwd,pwdgrpbuff,16384,&pw);
				if (pw==NULL) {
					mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"%s: no such user id - can't obtain group id",wuser+1);
					exit(1);
				}
				wrk_gid = pw->pw_gid;
			}
		} else {
			if (getpwnam_r(wuser,&pwd,pwdgrpbuff,16384,&pw)!=0) {
				pw = NULL;
			}
			if (pw==NULL) {
				mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"%s: no such user !!!",wuser);
				exit(1);
			}
			wrk_uid = pw->pw_uid;
			if (gidok==0) {
				wrk_gid = pw->pw_gid;
			}
		}
		free(wuser);
		free(wgroup);

		if (setgid(wrk_gid)<0) {
			mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't set gid to %d",(int)wrk_gid);
			exit(1);
		} else {
			mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"set gid to %d",(int)wrk_gid);
		}
		if (setuid(wrk_uid)<0) {
			mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't set uid to %d",(int)wrk_uid);
			exit(1);
		} else {
			mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"set uid to %d",(int)wrk_uid);
		}
	}
}

static int lfd = -1;	// main lock

pid_t mylock(int fd) {
	struct flock fl;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = getpid();
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	for (;;) {
		if (fcntl(fd,F_SETLK,&fl)>=0) {	// lock set
			return 0;	// ok
		}
		if (ERRNO_ERROR) {	// error other than "already locked"
			return -1;	// error
		}
		if (fcntl(fd,F_GETLK,&fl)<0) {	// get lock owner
			return -1;	// error getting lock
		}
		if (fl.l_type!=F_UNLCK) {	// found lock
			return fl.l_pid;	// return lock owner
		}
	}
	return -1;	// pro forma
}

void wdunlock(void) {
	if (lfd>=0) {
		close(lfd);
	}
}

uint8_t wdlock(uint8_t runmode,uint32_t timeout) {
	pid_t ownerpid;
	pid_t newownerpid;
	uint32_t l;

	lfd = open("." STR(APPNAME) ".lock",O_WRONLY|O_CREAT,0666);
	if (lfd<0) {
		mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't create lockfile in working directory");
		return 1;
	}
	ownerpid = mylock(lfd);
	if (ownerpid<0) {
		mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"fcntl error");
		return 1;
	}
	if (ownerpid>0) {
		if (runmode==RM_TEST) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,STR(APPNAME) " pid: %ld",(long)ownerpid);
			return 0;
		}
		if (runmode==RM_START) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"can't start: lockfile is already locked by another process");
			return 1;
		}
		if (runmode==RM_RELOAD) {
			if (kill(ownerpid,SIGHUP)<0) {
				mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't send reload signal to lock owner");
				return 1;
			}
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"reload signal has been sent");
			return 0;
		}
		if (runmode==RM_INFO) {
#if defined(SIGUSR1)
			if (kill(ownerpid,SIGUSR1)<0) {
#elif defined(SIGINFO)
			if (kill(ownerpid,SIGINFO)<0) {
#else
			if (1) {
#endif
				mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't send info signal to lock owner");
				return 1;
			}
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"info signal has been sent");
			return 0;
		}
		if (runmode==RM_KILL) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"sending SIGKILL to lock owner (pid:%ld)",(long int)ownerpid);
			if (kill(ownerpid,SIGKILL)<0) {
				mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't kill lock owner");
				return 1;
			}
		} else {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"sending SIGTERM to lock owner (pid:%ld)",(long int)ownerpid);
			if (kill(ownerpid,SIGTERM)<0) {
				mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't kill lock owner");
				return 1;
			}
		}
		l=0;
		fprintf(stderr,"waiting for termination ...");
		fflush(stderr);
		do {
			newownerpid = mylock(lfd);
			if (newownerpid<0) {
				mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"fcntl error");
				return 1;
			}
			if (newownerpid>0) {
				l++;
				if (l>=timeout) {
					mfs_log(MFSLOG_SYSLOG,MFSLOG_ERR,"about %"PRIu32" seconds passed and lockfile is still locked - giving up",l);
					fprintf(stderr,":giving up\n");
					return 1;
				}
				if (l%10==0) {
					mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"about %"PRIu32" seconds passed and lock still exists",l);
					fprintf(stderr,".");
					fflush(stderr);
				}
				if (newownerpid!=ownerpid) {
					fprintf(stderr,"\nnew lock owner detected\n");
					if (runmode==RM_KILL) {
						fprintf(stderr,":sending SIGKILL to lock owner (pid:%ld):",(long int)newownerpid);
						fflush(stderr);
						if (kill(newownerpid,SIGKILL)<0) {
							mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't kill lock owner");
							return 1;
						}
					} else {
						fprintf(stderr,":sending SIGTERM to lock owner (pid:%ld):",(long int)newownerpid);
						fflush(stderr);
						if (kill(newownerpid,SIGTERM)<0) {
							mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_ERR,"can't kill lock owner");
							return 1;
						}
					}
					ownerpid = newownerpid;
				}
			}
			sleep(1);
		} while (newownerpid!=0);
		fprintf(stderr," terminated\n");
		return 0;
	}
	if (runmode==RM_START || runmode==RM_RESTART) {
		char pidstr[20];
		l = snprintf(pidstr,20,"%ld\n",(long)(getpid()));
		if (ftruncate(lfd,0)<0) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"can't truncate pidfile");
		}
		if (write(lfd,pidstr,l)!=(ssize_t)l) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"can't write pid to pidfile");
		}
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"lockfile created and locked");
	} else if (runmode==RM_TRY_RESTART) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"can't find process to restart");
		return 1;
	} else if (runmode==RM_STOP || runmode==RM_KILL) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"can't find process to terminate");
		return 0;
	} else if (runmode==RM_RELOAD) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"can't find process to send reload signal");
		return 1;
	} else if (runmode==RM_INFO) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"can't find process to send info signal");
		return 1;
	} else if (runmode==RM_TEST) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_NOTICE,STR(APPNAME) " is not running");
		return 1;
	}
	return 0;
}

void makedaemon(void) {
	int f;
	uint8_t pipebuff[1000];
	ssize_t r;
	size_t happy;
	int piped[2];

	fflush(stdout);
	fflush(stderr);
	if (pipe(piped)<0) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"pipe error");
		exit(1);
	}
	f = fork();
	if (f<0) {
		mfs_log(MFSLOG_SYSLOG,MFSLOG_ERR,"first fork error: %s",strerr(errno));
		exit(1);
	}
	if (f>0) {
		wait(&f);	// just get child status - prevents child from being zombie during initialization stage
		if (f) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"Child status: %d",f);
			exit(1);
		}
		close(piped[1]);
//		printf("Starting daemon ...\n");
		while ((r=read(piped[0],pipebuff,1000))) {
			if (r>0) {
				if (pipebuff[r-1]==0) {	// zero as a last char in the pipe means error
					if (r>1) {
						happy = fwrite(pipebuff,1,r-1,stderr);
						(void)happy;
					}
					exit(1);
				}
				happy = fwrite(pipebuff,1,r,stderr);
				(void)happy;
			} else {
				mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"Error reading pipe: %s",strerr(errno));
				exit(1);
			}
		}
		exit(0);
	}
	setsid();
	setpgid(0,getpid());
	f = fork();
	if (f<0) {
		mfs_log(MFSLOG_SYSLOG,MFSLOG_ERR,"second fork error: %s",strerr(errno));
		if (write(piped[1],"fork error\n",11)!=11) {
			mfs_log(MFSLOG_SYSLOG,MFSLOG_ERR,"pipe write error: %s",strerr(errno));
		}
		close(piped[1]);
		exit(1);
	}
	if (f>0) {
		exit(0);
	}
	set_signal_handlers(1);

	f = open("/dev/null", O_RDWR, 0);
	close(STDIN_FILENO);
	sassert(dup(f)==STDIN_FILENO);
	close(STDOUT_FILENO);
	sassert(dup(f)==STDOUT_FILENO);
	close(STDERR_FILENO);
	sassert(dup(piped[1])==STDERR_FILENO);
	close(piped[1]);
	close(f); // this stupid construction with open/dup/close is used due to cppcheck false negatives
	mfs_log_detach_stderr();
//	setvbuf(stderr,(char *)NULL,_IOLBF,0);
}

void close_msg_channel(void) {
	int f;
	fflush(stderr);
	f = open("/dev/null", O_RDWR, 0);
	close(STDERR_FILENO);
	sassert(dup(f)==STDERR_FILENO);
	close(f); // this stupid construction with open/dup/close is used due to cppcheck false negatives
}

void createpath(const char *filename) {
	char pathbuff[1024];
	const char *src = filename;
	char *dst = pathbuff;
	if (*src=='/') *dst++=*src++;

	while (*src) {
		while (*src!='/' && *src) {
			*dst++=*src++;
		}
		if (*src=='/') {
			*dst='\0';
			if (mkdir(pathbuff,(mode_t)0777)<0) {
				if (errno!=EEXIST) {
					mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_INFO,"creating directory %s",pathbuff);
				}
			} else {
				mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"directory %s has been created",pathbuff);
			}
			*dst++=*src++;
		}
	}
}

void usage(const char *appname) {
	printf(
"usage: %s [-vhfdun] [-t locktimeout] [-c cfgfile] " MODULE_OPTIONS_SYNOPIS "[start|stop|restart|reload|info|test|kill|restore]\n"
"\n"
"-v : print version number and exit\n"
"-h : print this info and exit\n"
"-f : run in foreground\n"
"-d : run with dangerous options (names: DANGEROUS_*)\n"
"-u : log undefined config variables\n"
"-n : do not attempt to increase limit of core dump size\n"
"-t locktimeout : how long wait for lockfile\n"
"-c cfgfile : use given config file\n"
MODULE_OPTIONS_DESC
	,appname);
	exit(1);
}

int main(int argc,char **argv) {
	char *logappname;
//	char *lockfname;
	char *wrkdir;
	char *cfgfile;
	char *ocfgfile;
	char *appname;
	int ch;
	uint8_t runmode;
	uint8_t allowdangerousoptions;
	int rundaemon,logundefined;
	int lockmemory;
	int forcecoredump;
	int32_t nicelevel;
	uint32_t locktimeout;
	int fd;
	uint8_t movewarning;
	uint8_t userconfig;
	struct rlimit rls;
#if defined(USE_PTHREADS) && defined(M_ARENA_MAX) && defined(M_ARENA_TEST) && defined(HAVE_MALLOPT)
	uint32_t limit_glibc_arenas;
#endif
	int argc_back;
	char **argv_back;

	argc_back = argc;
	argv_back = argv;

	strerr_init();
	mycrc32_init();

	movewarning = 0;
	cfgfile=strdup(ETC_PATH "/mfs/" STR(APPNAME) ".cfg");
	passert(cfgfile);
	if ((fd = open(cfgfile,O_RDONLY))<0 && errno==ENOENT) {
		ocfgfile=strdup(ETC_PATH "/" STR(APPNAME) ".cfg");
		passert(ocfgfile);
		if ((fd = open(ocfgfile,O_RDONLY))>=0) {
			free(cfgfile);
			cfgfile = ocfgfile;
			movewarning = 1;
		} else {
			free(ocfgfile);
		}
	}
	if (fd>=0) {
		close(fd);
	}
	locktimeout = 1800;
	rundaemon = 1;
	allowdangerousoptions = 0;
	runmode = RM_START;
	logundefined = 0;
	lockmemory = 0;
	forcecoredump = 1;
	userconfig = 0;
	appname = argv[0];

	while ((ch = getopt(argc, argv, "nuvfdc:t:h?" MODULE_OPTIONS_GETOPT)) != -1) {
		switch(ch) {
			case 'v':
				printf("version: %s ; build: %s\n",VERSSTR,STR(BUILDNO));
				free(cfgfile);
				return 0;
			case 'f':
				rundaemon = 0;
				break;
			case 'd':
				allowdangerousoptions = 1;
				break;
			case 't':
				locktimeout=strtoul(optarg,NULL,10);
				break;
			case 'c':
				free(cfgfile);
				cfgfile = strdup(optarg);
				passert(cfgfile);
				movewarning = 0;
				userconfig = 1;
				break;
			case 'u':
				logundefined = 1;
				break;
			case 'n':
				forcecoredump = 0;
				break;
			MODULE_OPTIONS_SWITCH
			default:
				usage(appname);
				free(cfgfile);
				return 1;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc==1) {
		if (strcasecmp(argv[0],"start")==0) {
			runmode = RM_START;
		} else if (strcasecmp(argv[0],"stop")==0) {
			runmode = RM_STOP;
		} else if (strcasecmp(argv[0],"restart")==0) {
			runmode = RM_RESTART;
		} else if (strcasecmp(argv[0],"try-restart")==0) {
			runmode = RM_TRY_RESTART;
		} else if (strcasecmp(argv[0],"reload")==0) {
			runmode = RM_RELOAD;
		} else if (strcasecmp(argv[0],"info")==0) {
			runmode = RM_INFO;
		} else if (strcasecmp(argv[0],"test")==0 || strcasecmp(argv[0],"status")==0) {
			runmode = RM_TEST;
		} else if (strcasecmp(argv[0],"kill")==0) {
			runmode = RM_KILL;
		} else if (strcasecmp(argv[0],"restore")==0) {
			runmode = RM_RESTORE;
		} else {
			usage(appname);
			free(cfgfile);
			return 1;
		}
	} else if (argc!=0) {
		usage(appname);
		free(cfgfile);
		return 1;
	}

	if (movewarning) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"default sysconf path has changed - please move " STR(APPNAME) ".cfg from "ETC_PATH"/ to "ETC_PATH"/mfs/");
	}

	if (cfg_load(cfgfile,logundefined)==0) {
		if (userconfig) {
			if (rundaemon) {
				fputc(0,stderr);
				close_msg_channel();
			}
			free(cfgfile);
			return 1;
		}
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"can't load config file: %s - using defaults",cfgfile);
	}

	if (cfg_dangerous_options() && allowdangerousoptions==0 && (runmode==RM_RELOAD || runmode==RM_START || runmode==RM_RESTART || runmode==RM_TRY_RESTART)) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"Dangerous option(s) detected in config file: %s - use '-d' option to force %s",cfgfile,(runmode==RM_RELOAD)?"reload":"start");
		free(cfgfile);
		return 1;
	}
	free(cfgfile);

	if (runmode==RM_START || runmode==RM_RESTART || runmode==RM_TRY_RESTART) {
		if (rundaemon) {
			makedaemon();
		} else {
			set_signal_handlers(0);
		}
	}

	processname_init(argc_back,argv_back); // prepare everything for 'processname_set'

	logappname = cfg_getstr("SYSLOG_IDENT",STR(APPNAME));

	if (logappname[0]) {
		mfs_log_init(logappname,rundaemon);
	} else {
		mfs_log_init(STR(APPNAME),rundaemon);
	}

/* syslog test
	mfs_log_set_min_level(MFSLOG_DEBUG);
	mfs_log_set_elevate_to(MFSLOG_DEBUG);
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_DEBUG,"This is an example debug message");
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"This is simple info message");
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_NOTICE,"This is message that should be noticed");
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"This is a warning message - it means that something went wrong");
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"This is an error message - usually it means that module has to abort");
*/

	main_reload();

	if (runmode==RM_START || runmode==RM_RESTART || runmode==RM_TRY_RESTART) {
		rls.rlim_cur = MFSMAXFILES;
		rls.rlim_max = MFSMAXFILES;
		if (setrlimit(RLIMIT_NOFILE,&rls)<0) {
			mfs_log(MFSLOG_SYSLOG,MFSLOG_NOTICE,"can't change open files limit to: %u (trying to set smaller value)",(unsigned)(MFSMAXFILES));
			if (getrlimit(RLIMIT_NOFILE,&rls)>=0) {
				uint32_t limit;
				if (rls.rlim_max > MFSMAXFILES) {
					limit = MFSMAXFILES;
				} else {
					limit = rls.rlim_max;
				}
				while (limit>1024) {
					rls.rlim_cur = limit;
					if (setrlimit(RLIMIT_NOFILE,&rls)>=0) {
						mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"open files limit has been set to: %"PRIu32,limit);
						break;
					}
					limit *= 3;
					limit /= 4;
				}
			}
		} else {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"open files limit has been set to: %u",(unsigned)(MFSMAXFILES));
		}

		lockmemory = cfg_getnum("LOCK_MEMORY",0);
#ifdef MFS_USE_MEMLOCK
		if (lockmemory) {
			rls.rlim_cur = RLIM_INFINITY;
			rls.rlim_max = RLIM_INFINITY;
			setrlimit(RLIMIT_MEMLOCK,&rls);
		}
#endif
		nicelevel = cfg_getint32("NICE_LEVEL",-19);
		setpriority(PRIO_PROCESS,getpid(),nicelevel);

#if defined(__linux__) && defined(OOM_ADJUSTABLE)
		if (cfg_getuint8("DISABLE_OOM_KILLER",1)==1) {
			FILE *oomfd;
			int oomdis;
			oomdis = 0;
#  if defined(OOM_SCORE_ADJ_MIN)
			oomfd = fopen("/proc/self/oom_score_adj","w");
			if (oomfd!=NULL) {
				fprintf(oomfd,"%d\n",OOM_SCORE_ADJ_MIN);
				if (fclose(oomfd)>=0) {
					oomdis = 1;
				}
#    if defined(OOM_DISABLE)
			} else {
				oomfd = fopen("/proc/self/oom_adj","w");
				if (oomfd!=NULL) {
					fprintf(oomfd,"%d\n",OOM_DISABLE);
					if (fclose(oomfd)>=0) {
						oomdis = 1;
					}
				}
#    endif
			}
#  elif defined(OOM_DISABLE)
			oomfd = fopen("/proc/self/oom_adj","w");
			if (oomfd!=NULL) {
				fprintf(oomfd,"%d\n",OOM_DISABLE);
				if (fclose(oomfd)>=0) {
					oomdis = 1;
				}
			}
#  endif
			if (oomdis) {
				mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"out of memory killer disabled");
			} else {
				mfs_log(MFSLOG_SYSLOG,MFSLOG_WARNING,"can't disable out of memory killer");
			}
		}
#endif
	}

#ifdef USE_IONICE

#ifdef RLIMIT_NICE
	rls.rlim_cur = RLIM_INFINITY;
	rls.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_NICE,&rls);
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"nice limit set to infinity");
#endif

	ionice_test(); // print using method
	ionice_high(); // start with high priority - as not privileged user later we can only lower priority
#endif

	changeugid();

	wrkdir = cfg_getstr("DATA_PATH",DATA_PATH);
	if (runmode==RM_START || runmode==RM_RESTART || runmode==RM_TRY_RESTART) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"working directory: %s",wrkdir);
	}

	if (chdir(wrkdir)<0) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"can't set working directory to %s",wrkdir);
		if (rundaemon) {
			fputc(0,stderr);
			close_msg_channel();
		}
		mfs_log_term();
		free(logappname);
		return 1;
	}
	free(wrkdir);

	umask(cfg_getuint32("FILE_UMASK",027)&077);

	ch = wdlock(runmode,locktimeout);
	if (ch) {
		if (rundaemon) {
			fputc(0,stderr);
			close_msg_channel();
		}
		signal_cleanup();
		cfg_term();
		strerr_term();
		wdunlock();
		mfs_log_term();
		free(logappname);
		return ch;
	}

	ch = 0; // practically not needed after above condition - left for code elegance
	if (runmode==RM_RESTORE) {
		if (restore()==0) {
			ch = 1;
		}
	}

	if (runmode==RM_STOP || runmode==RM_KILL || runmode==RM_RELOAD || runmode==RM_INFO || runmode==RM_TEST || runmode==RM_RESTORE) {
		if (rundaemon) {
			close_msg_channel();
		}
		signal_cleanup();
		cfg_term();
		strerr_term();
		wdunlock();
		mfs_log_term();
		free(logappname);
		return ch;
	}

#ifdef MFS_USE_MEMLOCK
	if (lockmemory) {
		if (getrlimit(RLIMIT_MEMLOCK,&rls)<0) {
			mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_WARNING,"error getting memory lock limits");
		} else {
			if (rls.rlim_cur!=RLIM_INFINITY && rls.rlim_max==RLIM_INFINITY) {
				rls.rlim_cur = RLIM_INFINITY;
				rls.rlim_max = RLIM_INFINITY;
				if (setrlimit(RLIMIT_MEMLOCK,&rls)<0) {
					mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_WARNING,"error setting memory lock limit to unlimited");
				}
			}
			if (getrlimit(RLIMIT_MEMLOCK,&rls)<0) {
				mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_WARNING,"error getting memory lock limits");
			} else {
				if (rls.rlim_cur!=RLIM_INFINITY) {
					mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_WARNING,"can't set memory lock limit to unlimited");
				} else {
					if (mlockall(MCL_CURRENT|MCL_FUTURE)<0) {
						mfs_log(MFSLOG_ERRNO_SYSLOG_STDERR,MFSLOG_WARNING,"memory lock error");
					} else {
						mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"process memory was successfully locked in RAM");
					}
			}	}
		}
	}
#else
	if (lockmemory) {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_WARNING,"memory lock not supported !!!");
	}
#endif

	if (forcecoredump) {
		rls.rlim_cur = RLIM_INFINITY;
		rls.rlim_max = RLIM_INFINITY;
		setrlimit(RLIMIT_CORE,&rls);
#if defined(HAVE_PRCTL) && defined(PR_SET_DUMPABLE)
		prctl(PR_SET_DUMPABLE,1);
#endif
	}

/* glibc malloc tuning */
#if defined(USE_PTHREADS) && defined(M_ARENA_MAX) && defined(M_ARENA_TEST) && defined(HAVE_MALLOPT)
	limit_glibc_arenas = cfg_getuint8("LIMIT_GLIBC_MALLOC_ARENAS",4);
	if (limit_glibc_arenas) {
		if (!getenv("MALLOC_ARENA_MAX")) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"setting glibc malloc arena max to %"PRIu32,limit_glibc_arenas);
			mallopt(M_ARENA_MAX, limit_glibc_arenas);
		}
		if (!getenv("MALLOC_ARENA_TEST")) {
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"setting glibc malloc arena test to %"PRIu32,limit_glibc_arenas);
			mallopt(M_ARENA_TEST, limit_glibc_arenas);
		}
	} else {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"setting glibc malloc arenas turned off");
	}
#endif /* glibc malloc tuning */


	mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"monotonic clock function: %s",monotonic_method());
	mfs_log(MFSLOG_SYSLOG,MFSLOG_INFO,"monotonic clock speed: %"PRIu32" ops / 10 mili seconds",monotonic_speed());

	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"initializing %s modules ...",logappname);

	if (initialize()) {
//		if (getrlimit(RLIMIT_NOFILE,&rls)==0) {
//			mfs_log(MFSLOG_SYSLOG,MFSLOG_NOTICE,"open files limit: %lu",(unsigned long)(rls.rlim_cur));
//		}
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"%s daemon initialized properly",logappname);
		if (rundaemon) {
			close_msg_channel();
		}
		if (initialize_late()) {
			ch = mainloop();
			mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"exited from main loop");
		} else {
			ch=1;
		}
	} else {
		mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_ERR,"error occurred during initialization - exiting");
		if (rundaemon) {
			fputc(0,stderr);
			close_msg_channel();
		}
		ch=1;
	}
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"exititng ...");
	destruct();
	free_all_registered_entries();
	signal_cleanup();
	cfg_term();
	strerr_term();
	wdunlock();
	mfs_log(MFSLOG_SYSLOG_STDERR,MFSLOG_INFO,"process exited successfully (status:%d)",ch);
	mfs_log_term();
	free(logappname);
	return ch;
}
