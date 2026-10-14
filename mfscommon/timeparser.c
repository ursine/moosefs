#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>

#include "timeparser.h"

static uint32_t snprint_time_common(char *where, size_t mleng, uint32_t period, uint32_t *timetab,
	const char *timesym, const uint32_t timecount)
{
	uint32_t resultLen = 0;
	if (mleng<=0) {
		return resultLen;
	}

	if (period==0) {
		return snprintf(where, mleng, "0%c", timesym[timecount-1]);
	}

	for (uint32_t i = 0 ; i<timecount ; i++) {
		if (period >= timetab[i]) {
			const uint32_t timebase = timetab[i];
			uint64_t n = period * 10;
			const uint64_t p10 = n;
			if (timebase > 1) {
				n += timebase / 2;
				n /= timebase;
			}
			if (n * timebase != p10) {
				where[0] = '~';
				where++;
				mleng--;
				resultLen = 1;
				if (mleng==0) {
					return resultLen;
				}
			}
			const uint32_t intpart = n / 10;
			const uint32_t decpart = n % 10;
			if (decpart) {
				resultLen += snprintf(where,mleng,"%u.%u%c",intpart,decpart,timesym[i]);
			} else {
				resultLen += snprintf(where,mleng,"%u%c",intpart,timesym[i]);
			}
			return resultLen;
		}
	}

	if (resultLen<mleng) {
		where[resultLen]=0;
	}

	return resultLen;
}

/* formats (seconds mode):
 * #
 * #.#[s,m,h,d,w]
 * [#w][#d][#h][#m][#s]
 */

/* formats (hours mode):
 * #
 * #.#[h,d,w]
 * [#w][#d][#h]
 */

int parse_period_common(const char *str, uint32_t * const ret, const uint8_t hmode) {
	uint64_t cur,ndiv,max;
	uint32_t mul;
	uint8_t cmask;

	uint8_t mask = 0;
	uint64_t res = 0;
	while ((*str)==' ' || (*str)=='\t') {
		str++;
	}
	while (1) {
		if ((*str)>='0' && (*str<='9')) {
			if ((mask&0xC0)==0xC0) {
				*ret = *str;
				return TPARSE_UNEXPECTED_CHAR;
			}
			cur = 0;
			ndiv = 1;
			while ((*str)>='0' && (*str)<='9') {
				const uint32_t digit = (*str)-'0';
				if (cur > (UINT32_MAX-digit)/10) {
					*ret = 0;
					return TPARSE_VALUE_TOO_BIG;
				}
				cur = cur*10 + digit;
				ndiv *= 10;
				str++;
			}
			if (cur > UINT32_MAX) {
				*ret = 0;
				return TPARSE_VALUE_TOO_BIG;
			}
			while ((*str)==' ' || (*str)=='\t') {
				str++;
			}
		} else {
			*ret = *str;
			return TPARSE_UNEXPECTED_CHAR;
		}
		switch (*str) {
			case '.':
				if (mask!=0) {
					*ret = *str;
					return TPARSE_UNEXPECTED_CHAR;
				}
				mask |= 0x80;
				res = cur;
				str++;
				cmask = 0x80;
				break;
			case 'w':
			case 'W':
				if (hmode) {
					mul = 7*24;
				} else {
					mul = 7*24*3600;
				}
				max = UINT32_MAX;
				cmask = 0x10;
				break;
			case 'd':
			case 'D':
				if (hmode) {
					mul = 24;
					max = 7*24;
				} else {
					mul = 24*3600;
					max = 7*24*3600;
				}
				cmask = 0x08;
				break;
			case 'h':
			case 'H':
				if (hmode) {
					mul = 1;
					max = 24;
				} else {
					mul = 3600;
					max = 24*3600;
				}
				cmask = 0x04;
				break;
			case 'm':
			case 'M':
				if (hmode) {
					*ret = *str;
					return TPARSE_UNEXPECTED_CHAR;
				}
				mul = 60;
				max = 3600;
				cmask = 0x02;
				break;
			case 's':
			case 'S':
				if (hmode) {
					*ret = *str;
					return TPARSE_UNEXPECTED_CHAR;
				}
				mul = 1;
				max = 60;
				cmask = 0x01;
				break;
			case '\0':
				if (mask==0) {
					*ret = cur;
					return TPARSE_OK;
				}
				*ret = 0;
				return TPARSE_UNEXPECTED_CHAR;
			default:
				*ret = *str;
				return TPARSE_UNEXPECTED_CHAR;
		}
		if (cmask!=0x80) {
			cur *= mul;
			if (mask==0x80) {
				res *= mul;
				cur = (cur * 10) / ndiv;
				cur = (cur/10) + (((cur%10)>=5)?1:0);
				res += cur;
				mask |= 0x40;
				str++;
			} else if (mask==0) {
				mask |= cmask;
				res = cur;
				str++;
			} else if ((mask & (0xC0 | ((cmask<<1)-1)))==0) {
				if (cur < max) {
					mask |= cmask;
					res += cur;
					str++;
				} else {
					*ret = *str;
					return TPARSE_VALUE_TOO_BIG;
				}
			} else {
				*ret = *str;
				return TPARSE_UNEXPECTED_CHAR;
			}
			if (res > UINT32_MAX) {
				*ret = 0;
				return TPARSE_VALUE_TOO_BIG;
			}
			while ((*str)==' ' || (*str)=='\t') {
				str++;
			}
			if (*str=='\0' || *str=='\r' || *str=='\n') {
				*ret = res;
				return TPARSE_OK;
			}
		}
	}
}

uint32_t snprint_speriod(char* where, size_t mleng, uint32_t period) {
	static uint32_t timetab[5] = {604800,86400,3600,60,1};
	static char timesym[5] = {'w','d','h','m','s'};
	return snprint_time_common(where,mleng,period,timetab,timesym,5);
}

uint32_t parse_speriod(const char* str, uint32_t* ret) {
	return parse_period_common(str,ret,0);
}

uint32_t snprint_hperiod(char* where, size_t mleng, uint32_t period) { //show period as days or hours; returns count of characters
	static uint32_t timetab[3] = {168,24,1};
	static char timesym[3] = {'w','d','h'};
	return snprint_time_common(where,mleng,period,timetab,timesym,3);
}

uint32_t parse_hperiod(const char* str, uint32_t* ret) {
	return parse_period_common(str,ret,1);
}

