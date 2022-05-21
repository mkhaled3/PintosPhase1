#ifndef __THREAD_FIXED_POINT_H
#define __THREAD_FIXED_POINT_H


#define SHIFT 16

//Casting and rounding operations
int cast_fp(int x);
int fp_chop(int x);
int fp_round(int x);


//Basic operations on two fixed point parameters
int add_fp_fp(int x, int y);
int sub_fp_fp(int x, int y);
int multiply_fp_fp(int x, int y);
int divide_fp_fp(int x, int y);

//Basic operations on two parameters, first one is fixedpoint second is int
int add_fp_int(int x, int y);
int sub_fp_int(int x, int y);
int multiply_fp_int(int x, int y);
int divide_fp_int(int x, int y);

#endif /* threads/fixed-point.h */