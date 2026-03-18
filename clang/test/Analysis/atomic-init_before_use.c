// // RUN: %clang_analyze_cc1 -analyzer-checker=core.uninitialized.AtomicsInitBeforeUse -verify %s

typedef _Atomic(int) atomic;

extern int extern_f(void *);
extern void* malloc(unsigned long _);
extern int atomic_init(void *, int);

struct S {
    atomic a; 
    atomic b; 
};


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
    

    atomic a5;
    extern_f(&a5); // expected-warning{{Uninitialized atomic object escapes}} 

    atomic a6 = 0;
    extern_f(&a6);

    atomic a7 = 0;
    f(&a7); 

    atomic a8;
    f(&a8); // warn

    atomic a07 = 0;
    init(&a07); // warn

    atomic a08;
    init(&a08);

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
    
    atomic arr2[2] = {0, 0};
    arr2[0] = 1; 
    a10 = arr2[1];
    

    atomic* arr3 = malloc(sizeof(*arr2) * 2);
    arr3[0] = 1; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    a10 = arr3[1]; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

    atomic* arr4 = malloc(sizeof(*arr2) * 2);
    atomic_init(arr4, 0);
    arr4[0] = 1; 
    a10 = arr4[1]; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

    atomic arr5[2];
    atomic_init(&arr5[0], 0);

    arr5[0] = 0;
    arr5[1] = 0; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

    struct S s1;
    s1.a = 0; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    a10 = s1.b; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

    struct S s2;
    atomic_init(&s2.a, 0);
    s2.a = 1;
    a10 = s2.b; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

    struct S s3 = {0, 0};
    s3.a = 0;
    a10 = s3.b;

    struct {
        struct {
            struct S i;
        } i;
    } s4 = {0};
    a10 = s4.i.i.b;

    
    atomic arr6[5]; // IDK what to do
    for (int i = 0; i < cond; i++) {
        atomic_init(&arr6[i], 0);
    }
    a10 = arr6[3]; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}
    a10 = arr6[4]; // expected-warning{{Access to uninitialized atomic object [core.uninitialized.AtomicsInitBeforeUse]}}

}


atomic* ret_escape1(void) {
    atomic* a = malloc(sizeof(*a)); 
    extern_f(a); // expected-warning{{Uninitialized atomic object escapes}} 
    return a; // expected-warning{{Uninitialized atomic object escapes}} 
}

atomic* ret_escape2(void) {
    atomic* a = malloc(sizeof(*a)); 
    atomic_init(a, 0);
    extern_f(a);
    return a;
}

atomic* ret_escape3(void) {
    atomic* a = malloc(sizeof(*a)); 
    init(a);
    extern_f(a);
    return a;
}

int* false_positives(int *p) {
    int g;
    g = 0;
    g = *p;

    int* m = malloc(sizeof(*m));

    int arr[2];
    arr[0] = 0;
    *p = arr[1];
    return g ? m : p;        
}
