#pragma once

#include <inttypes.h>

typedef enum TParseResult_t
{
    TPARSE_OK = 0,
    TPARSE_UNEXPECTED_CHAR = -1,
    TPARSE_VALUE_TOO_BIG = -2
} TParseResult_t;

size_t snprint_speriod(char* where, size_t mleng, uint32_t period);
size_t snprint_hperiod(char* where, size_t mleng, uint32_t period);

TParseResult_t parse_speriod(const char* str, uint32_t* ret);
TParseResult_t parse_hperiod(const char* str, uint32_t* ret);
