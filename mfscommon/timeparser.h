#pragma once

#include <inttypes.h>
#include <stdlib.h>

uint32_t snprint_speriod(char* where, size_t mleng, uint32_t period);
uint32_t snprint_hperiod(char* where, size_t mleng, uint32_t period);

#define TPARSE_OK 0
#define TPARSE_UNEXPECTED_CHAR -1
#define TPARSE_VALUE_TOO_BIG -2

uint32_t parse_speriod(const char* str, uint32_t* ret);
uint32_t parse_hperiod(const char* str, uint32_t* ret);
