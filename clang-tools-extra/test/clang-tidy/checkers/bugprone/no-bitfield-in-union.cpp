// RUN: %check_clang_tidy %s bugprone-no-bitfield-in-union %t

#include <stdint.h>

union U1 {
    uint16_t r;
    uint8_t v;
};

struct S {
    uint32_t field:16;
};

union U4 {
    struct {
        uint32_t a:4;
        uint32_t b:4;
    } s;
    uint16_t r;
};

union U5 {
    struct Outer {
        struct Inner {
            uint32_t x:8;
        } inner;
    } outer;
    long dummy;
};

union U6 {
    struct S {
        uint32_t flags:4;
    } arr[10];
    uint32_t word;
};

union U8 {
    struct /*anon*/ {
        uint32_t a:4;
        uint32_t b:4;
    }; 
    uint16_t r;
};

union U2 {
    uint32_t small:8;
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: bit field 'small' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
    uint32_t big;
};

union U3 {
    uint32_t small:8;
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: bit field 'small' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
    uint32_t big:24;
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: bit field 'big' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
};

struct S1 {
    union {
        uint32_t x:8;
        // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: bit field 'x' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
        uint32_t y;
    };
    uint32_t z;
};

union U9 {
    union {
        uint32_t a:8;
        // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: bit field 'a' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
    } inner;
    uint32_t b;
};

union U10 {
    const uint32_t flag:1;
    // CHECK-MESSAGES: :[[@LINE-1]]:20: warning: bit field 'flag' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
    volatile uint32_t reg:8;
    // CHECK-MESSAGES: :[[@LINE-1]]:23: warning: bit field 'reg' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
};

#define BITS 4
union U11 {
    uint32_t low:BITS;
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: bit field 'low' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
    uint32_t high;
};

typedef uint32_t uint;
union U12 {
    uint field:1;
    // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: bit field 'field' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
};

void func(void) {
    union Local {
        uint32_t a:4;
        // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: bit field 'a' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
        uint32_t b;
    } u;
}

static union StaticUnion {
    uint32_t x:1;
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: bit field 'x' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
} su;

union U13 {
    union {
        uint32_t a:8;
        // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: bit field 'a' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
        uint32_t b;
    };
    uint32_t c;
};

#define union_keyword union
union_keyword U14 {
    uint32_t low:4;
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: bit field 'low' is declared as a member of union, which is forbidden by MISRA rule 6.3 [bugprone-no-bitfield-in-union]
    uint32_t high;
};

struct S2 {
    uint32_t a:4;
    uint32_t b:4;
};

union U16 {
    uint32_t x;
    uint16_t y;
};
