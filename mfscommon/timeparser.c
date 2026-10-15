#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>

#include "timeparser.h"

static size_t snprint_time_common(char *where, size_t mleng, uint32_t period, uint32_t *timetab,
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

typedef enum {
	HMODE_SECONDS,
	HMODE_HOURS
} HMode_t;

typedef struct {
	char suffix;
	uint8_t order;
	uint32_t multiplier;
	uint32_t maximum_component;
} PeriodUnit_t;

static const PeriodUnit_t seconds_units[] = {
	{'w', 4, 7 * 24 * 3600, UINT32_MAX},
	{'d', 3, 24 * 3600, 7 * 24 * 3600},
	{'h', 2, 3600, 24 * 3600},
	{'m', 1, 60, 3600},
	{'s', 0, 1, 60}
};

static const PeriodUnit_t hours_units[] = {
	{'w', 2, 7 * 24, UINT32_MAX},
	{'d', 1, 24, 7 * 24},
	{'h', 0, 1, 24}
};

static const char *skip_horizontal_space(const char *str) {
	while (*str == ' ' || *str == '\t') {
		str++;
	}
	return str;
}

static TParseResult_t parse_error(uint32_t *ret, const char *at, TParseResult_t result) {
	*ret = (result == TPARSE_VALUE_TOO_BIG) ? 0 : (uint8_t)*at;
	return result;
}

static const PeriodUnit_t *find_period_unit(char suffix, HMode_t hmode) {
	const PeriodUnit_t *units = (hmode == HMODE_HOURS) ? hours_units : seconds_units;
	size_t count = (hmode == HMODE_HOURS) ?
		sizeof(hours_units) / sizeof(hours_units[0]) :
		sizeof(seconds_units) / sizeof(seconds_units[0]);

	if (suffix >= 'A' && suffix <= 'Z') {
		suffix = (char)(suffix - 'A' + 'a');
	}
	for (size_t i = 0; i < count; i++) {
		if (units[i].suffix == suffix) {
			return &units[i];
		}
	}
	return NULL;
}

static TParseResult_t parse_period_common(const char *str, uint32_t * const ret, const HMode_t hmode) {
	uint64_t total = 0;
	uint8_t previous_order = UINT8_MAX;
	int have_component = 0;
	int saw_fraction = 0;

	while (isspace(*str)) {
		str++;
	}

	for (;;) {
		char *end;
		uint64_t fraction = 0;
		uint64_t denominator = 1;
		int has_fraction = 0;

		if (!isdigit(*str)) {
			return parse_error(ret, str, TPARSE_UNEXPECTED_CHAR);
		}

		errno = 0;
		uint64_t whole = strtoull(str, &end, 10);
		if (errno == ERANGE || whole > UINT32_MAX) {
			return parse_error(ret, str, TPARSE_VALUE_TOO_BIG);
		}
		str = skip_horizontal_space(end);

		if (*str == '.') {
			const char *fraction_start = ++str;
			has_fraction = 1;
			if (have_component) {
				return parse_error(ret, str - 1, TPARSE_UNEXPECTED_CHAR);
			}
			while (isdigit(*str)) {
				const uint32_t digit = (uint32_t)(*str - '0');
				if (fraction > (UINT32_MAX - digit) / 10) {
					return parse_error(ret, str, TPARSE_VALUE_TOO_BIG);
				}
				fraction = fraction * 10 + digit;
				if (denominator <= UINT64_MAX / 10) {
					denominator *= 10;
				}
				str++;
			}
			if (str == fraction_start) {
				return parse_error(ret, str, TPARSE_UNEXPECTED_CHAR);
			}
			str = skip_horizontal_space(str);
		}

		if (*str == '\0' || isspace(*str)) {
			const char *tail = str;
			while (isspace(*tail)) {
				tail++;
			}
			if (*tail != '\0' || has_fraction || have_component) {
				return parse_error(ret, str, TPARSE_UNEXPECTED_CHAR);
			}
			*ret = (uint32_t)whole;
			return TPARSE_OK;
		}

		const PeriodUnit_t *unit = find_period_unit(*str, hmode);
		if (unit == NULL) {
			return parse_error(ret, str, TPARSE_UNEXPECTED_CHAR);
		}
		if (have_component && (has_fraction || unit->order >= previous_order)) {
			return parse_error(ret, str, TPARSE_UNEXPECTED_CHAR);
		}
		if (have_component && saw_fraction) {
			return parse_error(ret, str, TPARSE_UNEXPECTED_CHAR);
		}

		uint64_t component = whole * unit->multiplier;
		if (has_fraction) {
			component += (fraction * unit->multiplier + denominator / 2) / denominator;
		} else if (have_component && component >= unit->maximum_component) {
			return parse_error(ret, str, TPARSE_VALUE_TOO_BIG);
		}
		if (component > UINT32_MAX - total) {
			return parse_error(ret, str, TPARSE_VALUE_TOO_BIG);
		}
		total += component;
		have_component = 1;
		saw_fraction = has_fraction;
		previous_order = unit->order;
		str = skip_horizontal_space(str + 1);

		if (*str == '\0') {
			*ret = (uint32_t)total;
			return TPARSE_OK;
		}
		if (*str == '\r' || *str == '\n') {
			const char *tail = str;
			while (isspace((unsigned char)*tail)) {
				tail++;
			}
			if (*tail == '\0') {
				*ret = (uint32_t)total;
				return TPARSE_OK;
			}
			return parse_error(ret, str, TPARSE_UNEXPECTED_CHAR);
		}
	}
}

size_t snprint_speriod(char* where, size_t mleng, uint32_t period) {
	static uint32_t timetab[5] = {604800,86400,3600,60,1};
	static char timesym[5] = {'w','d','h','m','s'};
	return snprint_time_common(where,mleng,period,timetab,timesym,5);
}

TParseResult_t parse_speriod(const char* str, uint32_t* ret) {
	return parse_period_common(str,ret,HMODE_SECONDS);
}

size_t snprint_hperiod(char* where, size_t mleng, uint32_t period) { //show period as days or hours; returns count of characters
	static uint32_t timetab[3] = {168,24,1};
	static char timesym[3] = {'w','d','h'};
	return snprint_time_common(where,mleng,period,timetab,timesym,3);
}

TParseResult_t parse_hperiod(const char* str, uint32_t* ret) {
	return parse_period_common(str,ret,HMODE_HOURS);
}
