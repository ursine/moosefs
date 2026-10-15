/*
 * Copyright (C) 2026 Jakub Kruszona-Zawadzki, Saglabs SA
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
 * along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "timeparser.h"
#include "mfstest.h"

static void check_speriod(const char *input,int expected_result,uint32_t expected_value) {
	uint32_t value = 0xA5A5A5A5;
	mfstest_assert_int32_eq(parse_speriod(input,&value),expected_result);
	if (expected_result==TPARSE_OK) {
		mfstest_assert_uint32_eq(value,expected_value);
	}
}

static void check_hperiod(const char *input,int expected_result,uint32_t expected_value) {
	uint32_t value = 0xA5A5A5A5;
	mfstest_assert_int32_eq(parse_hperiod(input,&value),expected_result);
	if (expected_result==TPARSE_OK) {
		mfstest_assert_uint32_eq(value,expected_value);
	}
}

static void check_format(int (*formatter)(char*,size_t,uint32_t),uint32_t value,const char *expected) {
	char buffer[32];
	int length;
	memset(buffer,0xA5,sizeof(buffer));
	length = formatter(buffer,sizeof(buffer),value);
	mfstest_assert_int32_eq(length,(int)strlen(expected));
	mfstest_assert_int32_eq(strcmp(buffer,expected),0);
}

int main(void) {
	mfstest_init();
	mfstest_start(parse_seconds_periods);
	check_speriod("0",TPARSE_OK,0);
	check_speriod("123",TPARSE_OK,123);
	check_speriod("1w",TPARSE_OK,604800);
	check_speriod("2D3h",TPARSE_OK,183600);
	check_speriod("1h 2m 3s",TPARSE_OK,3723);
	check_speriod("1.5h",TPARSE_OK,5400);
	check_speriod("1.25h",TPARSE_OK,4500);
	check_speriod("1.4s",TPARSE_OK,1);
	check_speriod("1.5s",TPARSE_OK,2);
	check_speriod("0.5s",TPARSE_OK,1);
	check_speriod("1.5h2m",TPARSE_UNEXPECTED_CHAR,0);
	char long_fraction[68] = "1.";
	memset(long_fraction + 2, '0', 63);
	long_fraction[65] = '5';
	long_fraction[66] = 'h';
	long_fraction[67] = '\0';
	check_speriod(long_fraction,TPARSE_OK,3600);
	check_speriod("  2m\t",TPARSE_OK,120);
	check_speriod("1W2D3H4M5S",TPARSE_OK,788645);
	check_speriod("4294967295",TPARSE_OK,UINT32_MAX);
	check_speriod("4294967296",TPARSE_VALUE_TOO_BIG,0);
	check_speriod("4294967295w",TPARSE_VALUE_TOO_BIG,0);
	check_speriod("1m60s",TPARSE_VALUE_TOO_BIG,0);
	check_speriod("1h2h",TPARSE_UNEXPECTED_CHAR,0);
	check_speriod("1s1m",TPARSE_UNEXPECTED_CHAR,0);
	check_speriod("1h1m1m",TPARSE_UNEXPECTED_CHAR,0);
	check_speriod("1x",TPARSE_UNEXPECTED_CHAR,0);
	check_speriod("1.2.3h",TPARSE_UNEXPECTED_CHAR,0);
	check_speriod("1..5h",TPARSE_UNEXPECTED_CHAR,0);
	check_speriod("1h ",TPARSE_OK,3600);
	check_speriod("1 h",TPARSE_OK,3600);
	check_speriod("1h2",TPARSE_UNEXPECTED_CHAR,0);
	check_speriod("1h\n",TPARSE_OK,3600);
	check_speriod("",TPARSE_UNEXPECTED_CHAR,0);
	mfstest_end();

	mfstest_start(parse_hour_periods);
	check_hperiod("0",TPARSE_OK,0);
	check_hperiod("36",TPARSE_OK,36);
	check_hperiod("1w2d3h",TPARSE_OK,219);
	check_hperiod("2.5d",TPARSE_OK,60);
	check_hperiod("1.4h",TPARSE_OK,1);
	check_hperiod("1.5h",TPARSE_OK,2);
	check_hperiod("4294967295",TPARSE_OK,UINT32_MAX);
	check_hperiod("4294967296",TPARSE_VALUE_TOO_BIG,0);
	check_hperiod("1d24h",TPARSE_VALUE_TOO_BIG,0);
	check_hperiod("1h1d",TPARSE_UNEXPECTED_CHAR,0);
	check_hperiod("1w1w",TPARSE_UNEXPECTED_CHAR,0);
	check_hperiod("1m",TPARSE_UNEXPECTED_CHAR,0);
	check_hperiod("1s",TPARSE_UNEXPECTED_CHAR,0);
	check_hperiod("1h2h",TPARSE_UNEXPECTED_CHAR,0);
	check_hperiod("168h",TPARSE_OK,168);
	check_hperiod("  2d\t",TPARSE_OK,48);
	mfstest_end();

	mfstest_start(format_seconds_periods);
	check_format(snprint_speriod,0,"0s");
	check_format(snprint_speriod,1,"1s");
	check_format(snprint_speriod,60,"1m");
	check_format(snprint_speriod,90,"1.5m");
	check_format(snprint_speriod,604800,"1w");
	check_format(snprint_speriod,604801,"~1w");

	char buffer[8];
	mfstest_assert_int32_eq(snprint_speriod(buffer,0,60),0);
	memset(buffer,0xA5,sizeof(buffer));
	const uint32_t value = snprint_speriod(buffer, 1, 60);
	mfstest_assert_uint32_eq(value,2);
	mfstest_assert_uint8_eq(buffer[0],0);

	mfstest_end();

	mfstest_start(format_hour_periods);
	check_format(snprint_hperiod,0,"0h");
	check_format(snprint_hperiod,1,"1h");
	check_format(snprint_hperiod,24,"1d");
	check_format(snprint_hperiod,36,"1.5d");
	check_format(snprint_hperiod,168,"1w");
	check_format(snprint_hperiod,169,"~1w");
	mfstest_end();

	mfstest_return();
}
