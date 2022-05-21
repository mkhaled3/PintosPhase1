#include <threads/fixed-point.h>

int f = 1 << 14;
//Casting and rounding operations 
int cast_fp(int n){
    return n * f;
}

int fp_chop(int n){
    return n / f;
}

int fp_round(int n){
    if(n >= 0)
        return (n + f / 2) / f;
    else
        return (n - f / 2) / f;
}


//Basic operations on two fixed point parameters
int add_fp_fp(int x, int y){
    return x + y;
}

int sub_fp_fp(int x, int y){
    return x - y;
}

int multiply_fp_fp(int x, int y){
    return ((int64_t)x) * y / f;
}

int divide_fp_fp(int x, int y){
    return ((int64_t)x) * f / y;
}


//Basic operations on two parameters, first one is fixedpoint second is int
int add_fp_int(int x, int n){
    return x + n*f;
}

int sub_fp_int(int x, int n){
    return x - n*f;
}

int multiply_fp_int(int x, int n){
    return x * n;
}

int divide_fp_int(int x, int n){
    return x / n;
}


