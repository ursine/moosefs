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

#include <stdio.h>
#include <stdlib.h>

#include "clocks.h"
#include "bitops.h"

#include "mfstest.h"

/* Independent references scan individual bits without using lookup tables. */

static uint8_t bitcount_ref(uint32_t b) {
	uint8_t ret = 0;
	uint32_t i,mask;
	for (i=0,mask=1 ; i<32 ; i++,mask<<=1) {
		if (b&mask) {
			ret++;
		}
	}
	return ret;
}

static uint8_t bitindex_ref(uint32_t b,uint8_t j) {
	uint32_t i,mask;
	for (i=0,mask=1 ; i<32 ; i++,mask<<=1) {
		if (b&mask) {
			if (j==0) {
				return i;
			} else {
				j--;
			}
		}
	}
	return 0xFF;
}

static uint8_t bitrank_ref(uint32_t b,uint8_t j) {
	uint8_t ret = 0;
	uint32_t i,mask;
	for (i=0,mask=1 ; i<32 && i<j ; i++,mask<<=1) {
		if (b&mask) {
			ret++;
		}
	}
	return ret;
}

static uint32_t simple_pseudo_random(void) {
	static uint32_t u=1249853491;
	static uint32_t v=3456394786;

	v = 36969*(v & 65535) + (v >> 16);
	u = 18000*(u & 65535) + (u >> 16);

	return (v << 16) + u;
}

/* Include the input and argument in failures from generated test cases. */
static void check_result(const char *operation,uint32_t value,unsigned argument,uint8_t expected,uint8_t actual) {
	if (expected!=actual) {
		printf("%s(0x%08" PRIX32 ",%u): expected %u, got %u\n",
			operation,value,argument,(unsigned)expected,(unsigned)actual);
	}
	mfstest_assert_uint8_eq(expected,actual);
}

/* Rank counts bits strictly below b; index selects the zero-based b-th set bit.
 * Bulk cases cover 0..32 and 255. Edge cases cover the entire uint8_t domain. */
static void check_word(uint32_t value,int all_arguments) {
	unsigned b,limit;

	check_result("bitcount",value,0,bitcount_ref(value),bitcount(value));
	limit = all_arguments ? 255 : 32;
	for (b=0 ; b<=limit ; b++) {
		check_result("bitrank",value,b,bitrank_ref(value,b),bitrank(value,b));
		check_result("bitindex",value,b,bitindex_ref(value,b),bitindex(value,b));
	}
	if (!all_arguments) {
		check_result("bitrank",value,255,bitcount_ref(value),bitrank(value,255));
		check_result("bitindex",value,255,0xFF,bitindex(value,255));
	}
}

static void test_edge_patterns(void) {
	static const uint32_t patterns[] = {
		0x00000000,0xFFFFFFFF,0xAAAAAAAA,0x55555555,
		0x33333333,0xCCCCCCCC,0x0F0F0F0F,0xF0F0F0F0,
		0x01010101,0x80808080,0x80000001,0x01800180,
		0x0000FFFF,0xFFFF0000,0x00FFFF00,0xFF0000FF,
		0x01234567,0x89ABCDEF
	};
	unsigned i,j;
	uint32_t mask;

	mfstest_start(bitops_edge_patterns);
	for (i=0 ; i<sizeof(patterns)/sizeof(patterns[0]) ; i++) {
		check_word(patterns[i],1);
	}
	for (i=0 ; i<32 ; i++) {
		mask = UINT32_C(1)<<i;
		check_word(mask,1);
		check_word(~mask,1);
		/* Powers of two and their immediate neighbors, including bit 31. */
		check_word(mask-1,1);
		check_word(mask+1,1);
		for (j=i+1 ; j<32 ; j++) {
			check_word(mask | (UINT32_C(1)<<j),1);
			check_word(~(mask | (UINT32_C(1)<<j)),1);
		}
	}
	/* Every contiguous run of set bits and its complement. */
	for (i=0 ; i<32 ; i++) {
		mask = 0;
		for (j=i ; j<32 ; j++) {
			mask |= UINT32_C(1)<<j;
			check_word(mask,1);
			check_word(~mask,1);
		}
	}
	mfstest_end();
}

static void test_exhaustive_bytes(void) {
	uint32_t value,mask;
	unsigned shift;

	mfstest_start(bitops_exhaustive_bytes);
	for (value=0 ; value<256 ; value++) {
		for (shift=0 ; shift<32 ; shift+=8) {
			mask = value<<shift;
			/* Empty surrounding bytes exercise skipping; full surrounding bytes
			 * exercise subtracting earlier populations before selecting a bit. */
			check_word(mask,1);
			check_word(mask | ~(UINT32_C(0xFF)<<shift),1);
		}
		check_word(value*UINT32_C(0x01010101),1);
	}
	mfstest_end();
}

static void test_exhaustive_halfwords(void) {
	uint32_t value;
	unsigned shift;

	mfstest_start(bitops_exhaustive_halfwords);
	/* All adjacent byte pairs, including the middle 8/16-bit boundary. */
	for (value=0 ; value<65536 ; value++) {
		for (shift=0 ; shift<=16 ; shift+=8) {
			check_word(value<<shift,0);
		}
	}
	mfstest_end();
}

static void test_random_words(void) {
	uint32_t i,value;
	unsigned b;
	uint8_t count,index;

	mfstest_start(bitops_random_words);
	/* Fixed PRNG seeds make failures reproducible on every platform. */
	for (i=0 ; i<65536 ; i++) {
		value = simple_pseudo_random();
		check_word(value,0);
		check_result("complement count",value,0,32,bitcount(value)+bitcount(~value));
		count = bitcount_ref(value);
		for (b=0 ; b<count ; b++) {
			index = bitindex(value,b);
			check_result("rank(index)",value,b,b,bitrank(value,index));
			/* Guard the shift so a broken index implementation cannot cause UB. */
			mfstest_assert_uint8_lt(index,32);
			if (index<32) {
				check_result("selected bit",value,b,1,(value>>index)&1);
				check_result("rank(index+1)",value,b,b+1,bitrank(value,index+1));
			}
		}
	}
	mfstest_end();
}

int main(void) {
	uint32_t i,b,bc1,bc2;
	uint8_t c;
	uint32_t *rblock;
	uint8_t *iblock;
	double st,en,corr,mybctime,refbctime;

	rblock = malloc(sizeof(uint32_t)*65536);
	iblock = malloc(sizeof(uint8_t)*65536);

	if (rblock==NULL || iblock==NULL) {
		free(iblock);
		free(rblock);
		return 99;
	}

	mfstest_init();

	test_edge_patterns();
	test_exhaustive_bytes();
	test_exhaustive_halfwords();
	test_random_words();

	mfstest_start(bitops_sampled_and_speed);

	printf("bitops - bit count function\n");

	for (i=0 ; i<65536 ; i++) {
		b = simple_pseudo_random();
		mfstest_assert_uint8_eq(bitcount_ref(b),bitcount(b));
	}

	printf("bitops - bit index function\n");

	for (i=0 ; i<65536 ; i++) {
		b = simple_pseudo_random();
		c = bitcount_ref(b);
		c = simple_pseudo_random() % (c+1);
		rblock[i] = b;
		iblock[i] = c;
		mfstest_assert_uint8_eq(bitindex_ref(b,c),bitindex(b,c));
	}

	printf("bitops - bit rank function\n");

	for (i=0 ; i<65536 ; i++) {
		b = simple_pseudo_random();
		c = simple_pseudo_random()&0x1F;
		mfstest_assert_uint8_eq(bitrank_ref(b,c),bitrank(b,c));
	}

	printf("bitops speed\n");

	st = monotonic_seconds();
	corr = monotonic_seconds();
	corr -= st;

	bc1 = 0;
	st = monotonic_seconds();
	for (i=0 ; i<1024*1024*2 ; i++) {
		bc1 += bitcount(rblock[i&0xFFFF]);
	}
	en = monotonic_seconds();
	mybctime = (en-st)-corr;

	bc2 = 0;
	st = monotonic_seconds();
	for (i=0 ; i<1024*1024*2 ; i++) {
		bc2 += bitcount_ref(rblock[i&0xFFFF]);
	}
	en = monotonic_seconds();
	refbctime = (en-st)-corr;

	mfstest_assert_uint32_eq(bc1,bc2);
	printf("2M * bitcount ; mfs bitcount: %.2lfM/s ; simple bitcount: %.2lfM/s ; speedup: %.2lf\n",2.0/mybctime,2.0/refbctime,refbctime / mybctime);

	bc1 = 0;
	st = monotonic_seconds();
	for (i=0 ; i<1024*1024*2 ; i++) {
		bc1 += bitindex(rblock[i&0xFFFF],iblock[i&0xFFFF]);
	}
	en = monotonic_seconds();
	mybctime = (en-st)-corr;

	bc2 = 0;
	st = monotonic_seconds();
	for (i=0 ; i<1024*1024*2 ; i++) {
		bc2 += bitindex_ref(rblock[i&0xFFFF],iblock[i&0xFFFF]);
	}
	en = monotonic_seconds();
	refbctime = (en-st)-corr;

	mfstest_assert_uint32_eq(bc1,bc2);
	printf("2M * bitindex ; mfs bitindex: %.2lfM/s ; simple bitindex: %.2lfM/s ; speedup: %.2lf\n",2.0/mybctime,2.0/refbctime,refbctime / mybctime);

	mfstest_end();
	free(iblock);
	free(rblock);
	mfstest_return();
}
