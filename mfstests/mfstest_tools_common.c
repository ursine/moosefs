/*
 * Copyright (C) 2026 Jakub Kruszona-Zawadzki, Saglabs SA
 *
 * This file is part of MooseFS and is distributed under the GNU General
 * Public License, version 2 only.
 */

#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools_common.h"
#include "mfstest.h"

static void check_number(const char *input, uint8_t bytesflag, int expected_result, uint64_t expected_value) {
	uint64_t value = UINT64_C(0xA5A5A5A5A5A5A5A5);
	int result = my_get_number(input, &value, UINT64_MAX, bytesflag);
	mfstest_assert_int32_eq(result, expected_result);
	if (expected_result > 0) {
		mfstest_assert_uint64_eq(value, expected_value);
	} else {
		mfstest_assert_uint64_eq(value, UINT64_C(0xA5A5A5A5A5A5A5A5));
	}
}

static void test_number_parser(void) {
	uint64_t value;

	mfstest_start(my_get_number);
	check_number("0", 0, 1, 0);
	check_number("123", 0, 1, 123);
	check_number(".5", 0, 1, 1);
	check_number(".5k", 0, 1, 500);
	check_number("1.25", 0, 1, 1);
	check_number("1.5", 0, 1, 2);
	check_number("1.5k", 0, 1, 1500);
	check_number("1.5KiB", 1, 1, 1536);
	check_number("1k", 0, 1, 1000);
	check_number("1KB", 1, -1, 0);
	check_number("1KiB", 1, 1, 1024);
	check_number("1EB", 1, 1, UINT64_C(1000000000000000000));
	check_number("1EiB", 1, 1, UINT64_C(1152921504606846976));
	check_number("1B", 1, 1, 1);
	check_number("1B", 0, -1, 0);
	check_number("", 0, -1, 0);
	check_number(".", 0, -1, 0);
	check_number("1.", 0, -1, 0);
	check_number("1x", 0, -1, 0);
	check_number("1ki", 0, -1, 0);
	check_number("1 k", 0, -1, 0);
	check_number("1.2.3", 0, -1, 0);
	check_number("18446744073709551616", 0, -2, 0);
	check_number("18446744073709551615", 0, 1, UINT64_MAX);
	check_number("18446744073709551615k", 0, -2, 0);
	check_number("1E", 0, 1, UINT64_C(1000000000000000000));
	check_number("1E", 1, 1, UINT64_C(1000000000000000000));
	value = 0;
	mfstest_assert_int32_eq(my_get_number("100", &value, 99, 0), -2);
	value = 0;
	mfstest_assert_int32_eq(my_get_number("99", &value, 99, 0), 1);
	mfstest_assert_uint64_eq(value, 99);
	mfstest_end();
}

static void check_path_result(int (*helper)(const char *, char *), const char *input, const char *expected) {
	char output[PATH_MAX + 1];
	memset(output, 0xA5, sizeof(output));
	mfstest_assert_int32_eq(helper(input, output), 0);
	mfstest_assert_int32_eq(strcmp(output, expected), 0);
}

static void test_path_helpers(void) {
	char path[32];

	mfstest_start(path_helpers);
	check_path_result(bsd_basename, NULL, ".");
	check_path_result(bsd_basename, "", ".");
	check_path_result(bsd_basename, "/", "/");
	check_path_result(bsd_basename, "////", "/");
	check_path_result(bsd_basename, "file", "file");
	check_path_result(bsd_basename, "/a/b///", "b");
	check_path_result(bsd_basename, "a//b", "b");
	check_path_result(bsd_dirname, NULL, ".");
	check_path_result(bsd_dirname, "", ".");
	check_path_result(bsd_dirname, "/", "/");
	check_path_result(bsd_dirname, "////", "/");
	check_path_result(bsd_dirname, "file", ".");
	check_path_result(bsd_dirname, "/a/b///", "/a");
	check_path_result(bsd_dirname, "a//b", "a");
	check_path_result(bsd_dirname, "a/b/", "a");
	{
		char long_path[PATH_MAX + 4];
		char output[PATH_MAX + 1];
		memset(long_path, 'x', PATH_MAX);
		long_path[PATH_MAX] = '\0';
		memset(output, 'y', sizeof(output));
		mfstest_assert_int32_eq(bsd_basename(long_path, output), -1);
		mfstest_assert_int32_eq(output[0], 'y');
		long_path[0] = '/';
		long_path[PATH_MAX] = '/';
		long_path[PATH_MAX + 1] = 'f';
		long_path[PATH_MAX + 2] = '\0';
		memset(output, 'y', sizeof(output));
		mfstest_assert_int32_eq(bsd_dirname(long_path, output), -1);
		mfstest_assert_int32_eq(output[0], 'y');
	}
	strcpy(path, "file");
	dirname_inplace(path);
	mfstest_assert_int32_eq(strcmp(path, "."), 0);
	strcpy(path, "/file");
	dirname_inplace(path);
	mfstest_assert_int32_eq(strcmp(path, "/"), 0);
	strcpy(path, "/a/b///");
	dirname_inplace(path);
	mfstest_assert_int32_eq(strcmp(path, "/a"), 0);
	strcpy(path, "");
	dirname_inplace(path);
	mfstest_assert_int32_eq(strcmp(path, "."), 0);
	dirname_inplace(NULL);
	mfstest_end();
}

static void check_match(const char *pattern, const char *value, uint8_t insensitive, int expected) {
	mfstest_assert_int32_eq(mystrmatch(pattern, value, insensitive), expected);
}

static void test_string_matcher(void) {
	mfstest_start(mystrmatch);
	check_match("", "", 0, 0);
	check_match("abc", "abc", 0, 0);
	check_match("abc", "abcd", 0, -1);
	check_match("a?c", "abc", 0, 0);
	check_match("a?c", "ac", 0, -1);
	check_match("a*c", "abbbc", 0, 0);
	check_match("a**c", "ac", 0, 0);
	check_match("a*?c", "abc", 0, 0);
	check_match("a*?c", "ac", 0, -1);
	check_match("[abc]", "b", 0, 0);
	check_match("[a-c]", "c", 0, 0);
	check_match("[a-c]", "d", 0, -1);
	check_match("[!a-c]", "z", 0, 0);
	check_match("[!a-c]", "b", 0, -1);
	check_match("[A-C]", "b", 1, 0);
	check_match("a\\*b", "a*b", 0, 0);
	check_match("ABC", "abc", 1, 0);
	check_match("[z-a]", "a", 0, -1);
	check_match("[abc", "a", 0, -1);
	check_match("[a-]", "a", 0, -1);
	check_match("[!abc]", "z", 0, 0);
	check_match("a\\?b", "a?b", 0, 0);
	check_match("[a-c]*z", "bxxz", 0, 0);
	mfstest_end();
}

static void test_number_format_widths(void) {
	mfstest_start(print_number_size);
	set_hu_flags('n');
	mfstest_assert_uint8_eq(print_number_size(PNUM_NONE), 20);
	mfstest_assert_uint8_eq(print_number_size(PNUM_32BIT), 10);
	set_hu_flags('k');
	mfstest_assert_uint8_eq(print_number_size(PNUM_NONE), 17);
	mfstest_assert_uint8_eq(print_number_size(PNUM_32BIT), 7);
	set_hu_flags('m');
	mfstest_assert_uint8_eq(print_number_size(PNUM_NONE), 14);
	mfstest_assert_uint8_eq(print_number_size(PNUM_32BIT), 4);
	set_hu_flags('g');
	mfstest_assert_uint8_eq(print_number_size(PNUM_NONE), 11);
	mfstest_assert_uint8_eq(print_number_size(PNUM_32BIT), 1);
	set_hu_flags('h');
	mfstest_assert_uint8_eq(print_number_size(PNUM_NONE), 6);
	mfstest_assert_uint8_eq(print_number_size(PNUM_32BIT), 6);
	set_hu_flags('H');
	mfstest_assert_uint8_eq(print_number_size(PNUM_NONE), 5);
	mfstest_assert_uint8_eq(print_number_size(PNUM_32BIT), 5);
	mfstest_end();
}

static int handler_argc;
static char **handler_argv;

static int test_command_handler(int argc, char *argv[]) {
	handler_argc = argc;
	handler_argv = argv;
	return 23;
}

static void test_command_dispatch(void) {
	static const command commands[] = {
		{"alpha|a,beta;gamma", test_command_handler},
		{NULL, NULL}
	};
	char *subcommand_argv[] = {"tool", "beta", "argument", NULL};
	char *binary_argv[] = {"/usr/bin/mfs-gamma-tool", "argument", NULL};

	unsetenv("MFSHRFORMAT");
	mfstest_start(tcomm_find_and_execute);
	handler_argc = -1;
	handler_argv = NULL;
	mfstest_assert_int32_eq(tcomm_find_and_execute(3, subcommand_argv, commands), 23);
	mfstest_assert_int32_eq(handler_argc, 2);
	mfstest_assert_int32_eq(strcmp(handler_argv[0], "beta"), 0);
	mfstest_assert_int32_eq(strcmp(handler_argv[1], "argument"), 0);
	handler_argc = -1;
	handler_argv = NULL;
	mfstest_assert_int32_eq(tcomm_find_and_execute(2, binary_argv, commands), 23);
	mfstest_assert_int32_eq(handler_argc, 2);
	mfstest_assert_int32_eq(strcmp(handler_argv[0], "/usr/bin/mfs-gamma-tool"), 0);
	mfstest_end();
}

static void test_time_wrappers(void) {
	uint32_t seconds = 0xA5A5A5A5;
	uint16_t hours = 0xA5A5;

	mfstest_start(time_parser_wrappers);
	mfstest_assert_int32_eq(tparse_seconds("1h2m3s", &seconds), 0);
	mfstest_assert_uint32_eq(seconds, 3723);
	mfstest_assert_int32_eq(tparse_seconds("1x", &seconds), -1);
	mfstest_assert_uint32_eq(seconds, 3723);
	mfstest_assert_int32_eq(tparse_seconds("4294967296", &seconds), -1);
	mfstest_assert_uint32_eq(seconds, 3723);
	mfstest_assert_int32_eq(tparse_hours("1d2h", &hours), 0);
	mfstest_assert_uint16_eq(hours, 26);
	mfstest_assert_int32_eq(tparse_hours("2731d", &hours), -1);
	mfstest_assert_uint16_eq(hours, 26);
	mfstest_end();
}

int main(void) {
	mfstest_init();
	test_number_parser();
	test_path_helpers();
	test_string_matcher();
	test_number_format_widths();
	test_command_dispatch();
	test_time_wrappers();
	mfstest_return();
}
