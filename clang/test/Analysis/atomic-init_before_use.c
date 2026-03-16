// RUN: %clang_analyze_cc1 -analyzer-checker=core.uninitialized.AtomicsInitBeforeUse -verify %s

typedef _Atomic(int) atomic;

extern int extern_f(void *);
extern void* malloc(unsigned long _);
extern int atomic_init(void *, int);


atomic g_a0; 
atomic g_a1 = 0; 
atomic* g_p;

void f(atomic* a) {
    *a = 5; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
}

void init(atomic* a) {
    atomic_init(a, 0);  // expected-warning{{Atomic object already initialized; double initialization is undefined [core.uninitialized.AtomicsInitBeforeUse]}} 
}


void tests(int cond) {
    g_a0 = g_a1;
    g_a1 = g_a0;

    atomic a0;
    g_a0 = a0; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

    atomic a1 = 0; 
    a1 = 1;
    atomic b1 = a1; 
    b1 = 1;


    atomic a2;
    a2 = 1; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    
    atomic b3;
    atomic* p3 = &b3;
    atomic_init(p3, 0);
    b3 = 1;

    atomic a3;
    atomic_init(&a3, 0);
    a3 = 1;
    
    atomic a4 = 0;
    atomic_init(&a4, 0); // expected-warning{{Atomic object already initialized; double initialization is undefined [core.uninitialized.AtomicsInitBeforeUse]}} 
    

    atomic a5;     // TODO should fail
    extern_f(&a5); // expected-warning{{Uninitialized atomic object escapes}} 

    atomic a6 = 0;
    extern_f(&a6);

    atomic a7 = 0;
    f(&a7); 

    atomic a8;
    f(&a8); // warning

    atomic a07 = 0;
    init(&a7); // warning

    atomic a08;
    init(&a8);

    atomic a09; // TODO should fail
    g_p = &a09; // expected-warning{{Uninitialized atomic object escapes}} 


    atomic a9;
    if (cond) {
        a9 = 1; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    } 

    atomic a10;
    if (cond) {
        atomic_init(&a10, 0);
    } else {
        a10 = 2; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
        atomic_init(&a10, 1);
    }
    a10 = 2;

    atomic arr1[2];
    arr1[0] = 1; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    a10 = arr1[1]; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    
    atomic arr2[2] = {0, 0}; // TODO should not fail
    arr2[0] = 1; 
    a10 = arr2[1];
    

    atomic* arr3 = malloc(sizeof(*arr2));
    arr3[0] = 1; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    a10 = arr3[0]; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

    atomic* arr4 = malloc(sizeof(*arr2));
    atomic_init(arr4, 0);
    arr4[0] = 1; 
    a10 = arr4[0];

    
}


atomic* ret_escape1(void) {
    atomic* a = malloc(sizeof(*a)); // TODO should fail
    return a; // expected-warning{{Uninitialized atomic object escapes}} 
}

atomic* ret_escape2(void) {
    atomic* a = malloc(sizeof(*a)); 
    atomic_init(a, 0);
    return a;
}
