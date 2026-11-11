// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tiny C++98 test helpers for the CarPlay->HUD host tests.
#ifndef TEST_UTIL_HPP
#define TEST_UTIL_HPP

#include <cstdio>
#include <string>

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do { ++g_checks; if(!(cond)){ ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while(0)

#define CHECK_EQ(a,b) do { ++g_checks; long _va=(long)(a); long _vb=(long)(b); \
    if(_va!=_vb){ ++g_fail; \
    std::printf("FAIL %s:%d  %s == %s  (got %ld vs %ld)\n", __FILE__,__LINE__,#a,#b,_va,_vb); } } while(0)

#define CHECK_STR(a,b) do { ++g_checks; std::string _sa=(a); std::string _sb=(b); \
    if(_sa!=_sb){ ++g_fail; \
    std::printf("FAIL %s:%d  %s == %s  (got '%s' vs '%s')\n", __FILE__,__LINE__,#a,#b,_sa.c_str(),_sb.c_str()); } } while(0)

#define TEST_RETURN() do { \
    std::printf("%s: %d checks, %d failure(s)\n", __FILE__, g_checks, g_fail); \
    return g_fail ? 1 : 0; } while(0)

#endif
