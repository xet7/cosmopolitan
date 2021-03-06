#include "libc/math.h"
#include "libc/mem/mem.h"
#include "libc/str/str.h"
#include "third_party/gdtoa/gdtoa.h"

asm(".ident\t\"\\n\\n\
gdtoa (MIT License)\\n\
The author of this software is David M. Gay\\n\
Kudos go to Guy L. Steele, Jr. and Jon L. White\\n\
Copyright (C) 1997, 1998, 2000 by Lucent Technologies\"");
asm(".include \"libc/disclaimer.inc\"");

#define IEEE_Arith 1
#define IEEE_8087  1
#define f_QNAN     0x7fc00000
#define d_QNAN0    0x7ff80000
#define d_QNAN1    0x0

#if __NO_MATH_ERRNO__ + 0
#define NO_ERRNO 1
#endif

#if __FINITE_MATH_ONLY__ + 0
#define NO_INFNAN_CHECK 1
#endif

/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 1998-2000 by Lucent Technologies
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of Lucent or any of its entities
not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.

****************************************************************/

/* This is a variation on dtoa.c that converts arbitary binary
   floating-point formats to and from decimal notation.  It uses
   double-precision arithmetic internally, so there are still
   various #ifdefs that adapt the calculations to the native
   double-precision arithmetic (any of IEEE, VAX D_floating,
   or IBM mainframe arithmetic).

   Please send bug reports to David M. Gay (dmg at acm dot org,
   with " at " changed at "@" and " dot " changed to ".").
 */

/* On a machine with IEEE extended-precision registers, it is
 * necessary to specify double-precision (53-bit) rounding precision
 * before invoking strtod or dtoa.  If the machine uses (the equivalent
 * of) Intel 80x87 arithmetic, the call
 *	_control87(PC_53, MCW_PC);
 * does this with many compilers.  Whether this or another call is
 * appropriate depends on the compiler; for this to work, it may be
 * necessary third_party/gdtoa/to #include "float.h" or another system-dependent
 *header file.
 */

/* strtod for IEEE-, VAX-, and IBM-arithmetic machines.
 *
 * This strtod returns a nearest machine number to the input decimal
 * string (or sets errno to ERANGE).  With IEEE arithmetic, ties are
 * broken by the IEEE round-even rule.  Otherwise ties are broken by
 * biased rounding (add half and chop).
 *
 * Inspired loosely by William D. Clinger's paper "How to Read Floating
 * Point Numbers Accurately" [Proc. ACM SIGPLAN '90, pp. 112-126].
 *
 * Modifications:
 *
 *	1. We only require IEEE, IBM, or VAX double-precision
 *		arithmetic (not IEEE double-extended).
 *	2. We get by with floating-point arithmetic in a case that
 *		Clinger missed -- when we're computing d * 10^n
 *		for a small integer d and the integer n is not too
 *		much larger than 22 (the maximum integer k for which
 *		we can represent 10^k exactly), we may be able to
 *		compute (d*10^k) * 10^(e-k) with just one roundoff.
 *	3. Rather than a bit-at-a-time adjustment of the binary
 *		result in the hard case, we use floating-point
 *		arithmetic to determine the adjustment to within
 *		one bit; only in really hard cases do we need to
 *		compute a second residual.
 *	4. Because of 3., we don't need a large table of powers of 10
 *		for ten-to-e (just some small tables, e.g. of 10^k
 *		for 0 <= k <= 22).
 */

/*
 * #define IEEE_8087 for IEEE-arithmetic machines where the least
 *	significant byte has the lowest address.
 * #define IEEE_MC68k for IEEE-arithmetic machines where the most
 *	significant byte has the lowest address.
 * #define Long int on machines with 32-bit ints and 64-bit longs.
 * #define Sudden_Underflow for IEEE-format machines without gradual
 *	underflow (i.e., that flush to zero on underflow).
 * #define IBM for IBM mainframe-style floating-point arithmetic.
 * #define VAX for VAX-style floating-point arithmetic (D_floating).
 * #define No_leftright to omit left-right logic in fast floating-point
 *	computation of dtoa and gdtoa.  This will cause modes 4 and 5 to be
 *	treated the same as modes 2 and 3 for some inputs.
 * #define Check_FLT_ROUNDS if FLT_ROUNDS can assume the values 2 or 3.
 * #define RND_PRODQUOT to use rnd_prod and rnd_quot (assembly routines
 *	that use extended-precision instructions to compute rounded
 *	products and quotients) with IBM.
 * #define ROUND_BIASED for IEEE-format with biased rounding and arithmetic
 *	that rounds toward +Infinity.
 * #define ROUND_BIASED_without_Round_Up for IEEE-format with biased
 *	rounding when the underlying floating-point arithmetic uses
 *	unbiased rounding.  This prevent using ordinary floating-point
 *	arithmetic when the result could be computed with one rounding error.
 * #define Inaccurate_Divide for IEEE-format with correctly rounded
 *	products but inaccurate quotients, e.g., for Intel i860.
 * #define NO_LONG_LONG on machines that do not have a "long long"
 *	integer type (of >= 64 bits).  On such machines, you can
 *	#define Just_16 to store 16 bits per 32-bit Long when doing
 *	high-precision integer arithmetic.  Whether this speeds things
 *	up or slows things down depends on the machine and the number
 *	being converted.  If long long is available and the name is
 *	something other than "long long", #define Llong to be the name,
 *	and if "unsigned Llong" does not work as an unsigned version of
 *	Llong, #define #ULLong to be the corresponding unsigned type.
 * #define Bad_float_h if your system lacks a float.h or if it does not
 *	define some or all of DBL_DIG, DBL_MAX_10_EXP, DBL_MAX_EXP,
 *	FLT_RADIX, FLT_ROUNDS, and DBL_MAX.
 * #define MALLOC your_malloc, where your_malloc(n) acts like malloc(n)
 *	if memory is available and otherwise does something you deem
 *	appropriate.  If MALLOC is undefined, malloc will be invoked
 *	directly -- and assumed always to succeed.  Similarly, if you
 *	want something other than the system's free() to be called to
 *	recycle memory acquired from MALLOC, #define FREE to be the
 *	name of the alternate routine.  (FREE or free is only called in
 *	pathological cases, e.g., in a gdtoa call after a gdtoa return in
 *	mode 3 with thousands of digits requested.)
 * #define Omit_Private_Memory to omit logic (added Jan. 1998) for making
 *	memory allocations from a private pool of memory when possible.
 *	When used, the private pool is PRIVATE_MEM bytes long:  2304 bytes,
 *	unless #defined to be a different length.  This default length
 *	suffices to get rid of MALLOC calls except for unusual cases,
 *	such as decimal-to-binary conversion of a very long string of
 *	digits.  When converting IEEE double precision values, the
 *	longest string gdtoa can return is about 751 bytes long.  For
 *	conversions by strtod of strings of 800 digits and all gdtoa
 *	conversions of IEEE doubles in single-threaded executions with
 *	8-byte pointers, PRIVATE_MEM >= 7400 appears to suffice; with
 *	4-byte pointers, PRIVATE_MEM >= 7112 appears adequate.
 * #define NO_INFNAN_CHECK if you do not wish to have INFNAN_CHECK
 *	#defined automatically on IEEE systems.  On such systems,
 *	when INFNAN_CHECK is #defined, strtod checks
 *	for Infinity and NaN (case insensitively).
 *	When INFNAN_CHECK is #defined and No_Hex_NaN is not #defined,
 *	strtodg also accepts (case insensitively) strings of the form
 *	NaN(x), where x is a string of hexadecimal digits (optionally
 *	preceded by 0x or 0X) and spaces; if there is only one string
 *	of hexadecimal digits, it is taken for the fraction bits of the
 *	resulting NaN; if there are two or more strings of hexadecimal
 *	digits, each string is assigned to the next available sequence
 *	of 32-bit words of fractions bits (starting with the most
 *	significant), right-aligned in each sequence.
 *	Unless GDTOA_NON_PEDANTIC_NANCHECK is #defined, input "NaN(...)"
 *	is consumed even when ... has the wrong form (in which case the
 *	"(...)" is consumed but ignored).
 * #define MULTIPLE_THREADS if the system offers preemptively scheduled
 *	multiple threads.  In this case, you must provide (or suitably
 *	#define) two locks, acquired by ACQUIRE_DTOA_LOCK(n) and freed
 *	by FREE_DTOA_LOCK(n) for n = 0 or 1.  (The second lock, accessed
 *	in pow5mult, ensures lazy evaluation of only one copy of high
 *	powers of 5; omitting this lock would introduce a small
 *	probability of wasting memory, but would otherwise be harmless.)
 *	You must also invoke freedtoa(s) to free the value s returned by
 *	dtoa.  You may do so whether or not MULTIPLE_THREADS is #defined.

 *	When MULTIPLE_THREADS is #defined, source file misc.c provides
 *		void set_max_gdtoa_threads(unsigned int n);
 *	and expects
 *		unsigned int dtoa_get_threadno(void);
 *	to be available (possibly provided by
 *		#define dtoa_get_threadno omp_get_thread_num
 *	if OpenMP is in use or by
 *		#define dtoa_get_threadno pthread_self
 *	if Pthreads is in use), to return the current thread number.
 *	If set_max_dtoa_threads(n) was called and the current thread
 *	number is k with k < n, then calls on ACQUIRE_DTOA_LOCK(...) and
 *	FREE_DTOA_LOCK(...) are avoided; instead each thread with thread
 *	number < n has a separate copy of relevant data structures.
 *	After set_max_dtoa_threads(n), a call set_max_dtoa_threads(m)
 *	with m <= n has has no effect, but a call with m > n is honored.
 *	Such a call invokes REALLOC (assumed to be "realloc" if REALLOC
 *	is not #defined) to extend the size of the relevant array.

 * #define IMPRECISE_INEXACT if you do not care about the setting of
 *	the STRTOG_Inexact bits in the special case of doing IEEE double
 *	precision conversions (which could also be done by the strtod in
 *	dtoa.c).
 * #define NO_HEX_FP to disable recognition of C9x's hexadecimal
 *	floating-point constants.
 * #define -DNO_ERRNO to suppress setting errno (in strtod.c and
 *	strtodg.c).
 * #define NO_STRING_H to use private versions of memcpy.
 *	On some K&R systems, it may also be necessary to
 *	#define DECLARE_SIZE_T in this case.
 * #define USE_LOCALE to use the current locale's decimal_point value.
 */

#ifndef Long
#define Long int
#endif
#ifndef ULong
typedef unsigned Long ULong;
#endif
#ifndef UShort
typedef unsigned short UShort;
#endif

#ifndef CONST
#define CONST const
#endif /* CONST */

#ifdef DEBUG
#define Bug(x)                  \
  {                             \
    fprintf(stderr, "%s\n", x); \
    exit(1);                    \
  }
#endif

/* #ifdef KR_headers */
/* #define Char char */
/* #else */
#define Char void
/* #endif */

#ifdef MALLOC
extern Char *MALLOC(size_t);
#else
#define MALLOC malloc
#endif

#ifdef REALLOC
extern Char *REALLOC(Char *, size_t);
#else
#define REALLOC realloc
#endif

#undef IEEE_Arith
#undef Avoid_Underflow
#ifdef IEEE_MC68k
#define IEEE_Arith
#endif
#ifdef IEEE_8087
#define IEEE_Arith
#endif

#ifdef Bad_float_h

#else /* ifndef Bad_float_h */
#endif /* Bad_float_h */

#ifdef IEEE_Arith
#define Scale_Bit 0x10
#define n_bigtens 5
#endif

#ifdef IBM
#define n_bigtens 3
#endif

#ifdef VAX
#define n_bigtens 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef union {
  double d;
  ULong L[2];
} U;

#ifdef IEEE_8087
#define word0(x) (x)->L[1]
#define word1(x) (x)->L[0]
#else
#define word0(x) (x)->L[0]
#define word1(x) (x)->L[1]
#endif
#define dval(x) (x)->d

/* The following definition of Storeinc is appropriate for MIPS processors.
 * An alternative that might be better on some machines is
 * #define Storeinc(a,b,c) (*a++ = b << 16 | c & 0xffff)
 */
#if defined(IEEE_8087) + defined(VAX)
#define Storeinc(a, b, c)                        \
  (((unsigned short *)a)[1] = (unsigned short)b, \
   ((unsigned short *)a)[0] = (unsigned short)c, a++)
#else
#define Storeinc(a, b, c)                        \
  (((unsigned short *)a)[0] = (unsigned short)b, \
   ((unsigned short *)a)[1] = (unsigned short)c, a++)
#endif

/* #define P DBL_MANT_DIG */
/* Ten_pmax = floor(P*log(2)/log(5)) */
/* Bletch = (highest power of 2 < DBL_MAX_10_EXP) / 16 */
/* Quick_max = floor((P-1)*log(FLT_RADIX)/log(10) - 1) */
/* Int_max = floor(P*log(FLT_RADIX)/log(10) - 1) */

#ifdef IEEE_Arith
#define Exp_shift   20
#define Exp_shift1  20
#define Exp_msk1    0x100000
#define Exp_msk11   0x100000
#define Exp_mask    0x7ff00000
#define P           53
#define Bias        1023
#define Emin        (-1022)
#define Exp_1       0x3ff00000
#define Exp_11      0x3ff00000
#define Ebits       11
#define Frac_mask   0xfffff
#define Frac_mask1  0xfffff
#define Ten_pmax    22
#define Bletch      0x10
#define Bndry_mask  0xfffff
#define Bndry_mask1 0xfffff
#define LSB         1
#define Sign_bit    0x80000000
#define Log2P       1
#define Tiny0       0
#define Tiny1       1
#define Quick_max   14
#define Int_max     14

#ifndef Flt_Rounds
#ifdef FLT_ROUNDS
#define Flt_Rounds FLT_ROUNDS
#else
#define Flt_Rounds 1
#endif
#endif /*Flt_Rounds*/

#else /* ifndef IEEE_Arith */
#undef Sudden_Underflow
#define Sudden_Underflow
#ifdef IBM
#undef Flt_Rounds
#define Flt_Rounds  0
#define Exp_shift   24
#define Exp_shift1  24
#define Exp_msk1    0x1000000
#define Exp_msk11   0x1000000
#define Exp_mask    0x7f000000
#define P           14
#define Bias        65
#define Exp_1       0x41000000
#define Exp_11      0x41000000
#define Ebits       8 /* exponent has 7 bits, but 8 is the right value in b2d */
#define Frac_mask   0xffffff
#define Frac_mask1  0xffffff
#define Bletch      4
#define Ten_pmax    22
#define Bndry_mask  0xefffff
#define Bndry_mask1 0xffffff
#define LSB         1
#define Sign_bit    0x80000000
#define Log2P       4
#define Tiny0       0x100000
#define Tiny1       0
#define Quick_max   14
#define Int_max     15
#else /* VAX */
#undef Flt_Rounds
#define Flt_Rounds  1
#define Exp_shift   23
#define Exp_shift1  7
#define Exp_msk1    0x80
#define Exp_msk11   0x800000
#define Exp_mask    0x7f80
#define P           56
#define Bias        129
#define Exp_1       0x40800000
#define Exp_11      0x4080
#define Ebits       8
#define Frac_mask   0x7fffff
#define Frac_mask1  0xffff007f
#define Ten_pmax    24
#define Bletch      2
#define Bndry_mask  0xffff007f
#define Bndry_mask1 0xffff007f
#define LSB         0x10000
#define Sign_bit    0x8000
#define Log2P       1
#define Tiny0       0x80
#define Tiny1       0
#define Quick_max   15
#define Int_max     15
#endif /* IBM, VAX */
#endif /* IEEE_Arith */

#ifndef IEEE_Arith
#define ROUND_BIASED
#else
#ifdef ROUND_BIASED_without_Round_Up
#undef ROUND_BIASED
#define ROUND_BIASED
#endif
#endif

#ifdef RND_PRODQUOT
#define rounded_product(a, b)  a = rnd_prod(a, b)
#define rounded_quotient(a, b) a = rnd_quot(a, b)
extern double rnd_prod(double, double), rnd_quot(double, double);
#else
#define rounded_product(a, b)  a *= b
#define rounded_quotient(a, b) a /= b
#endif

#define Big0 (Frac_mask1 | Exp_msk1 * (DBL_MAX_EXP + Bias - 1))
#define Big1 0xffffffff

#undef Pack_16
#ifndef Pack_32
#define Pack_32
#endif

#ifdef NO_LONG_LONG
#undef ULLong
#ifdef Just_16
#undef Pack_32
#define Pack_16
/* When Pack_32 is not defined, we store 16 bits per 32-bit Long.
 * This makes some inner loops simpler and sometimes saves work
 * during multiplications, but it often seems to make things slightly
 * slower.  Hence the default is now to store 32 bits per Long.
 */
#endif
#else /* long long available */
#ifndef Llong
#define Llong long long
#endif
#ifndef ULLong
#define ULLong unsigned Llong
#endif
#endif /* NO_LONG_LONG */

#ifdef Pack_32
#define ULbits 32
#define kshift 5
#define kmask  31
#define ALL_ON 0xffffffff
#else
#define ULbits 16
#define kshift 4
#define kmask  15
#define ALL_ON 0xffff
#endif

#ifdef MULTIPLE_THREADS /*{{*/
#define MTa , PTI
#define MTb , &TI
#define MTd , ThInfo **PTI
#define MTk ThInfo **PTI;
extern void ACQUIRE_DTOA_LOCK(unsigned int);
extern void FREE_DTOA_LOCK(unsigned int);
extern unsigned int dtoa_get_threadno(void);
#else /*}{*/
#define ACQUIRE_DTOA_LOCK(n) /*nothing*/
#define FREE_DTOA_LOCK(n)    /*nothing*/
#define MTa                  /*nothing*/
#define MTb                  /*nothing*/
#define MTd                  /*nothing*/
#define MTk                  /*nothing*/
#endif /*}}*/

#define Kmax 9

struct Bigint {
  struct Bigint *next;
  int k, maxwds, sign, wds;
  ULong x[1];
};

typedef struct Bigint Bigint;

typedef struct ThInfo {
  Bigint *Freelist[Kmax + 1];
  Bigint *P5s;
} ThInfo;

#ifdef NO_STRING_H
#ifdef DECLARE_SIZE_T
typedef unsigned int size_t;
#endif
extern void __gdtoa_memcpy(void *, const void *, size_t);
#define Bcopy(x, y) \
  __gdtoa_memcpy(&x->sign, &y->sign, y->wds * sizeof(ULong) + 2 * sizeof(int))
#else /* !NO_STRING_H */
#define Bcopy(x, y) \
  memcpy(&x->sign, &y->sign, y->wds * sizeof(ULong) + 2 * sizeof(int))
#endif /* NO_STRING_H */

#define Balloc      __gdtoa_Balloc
#define Bfree       __gdtoa_Bfree
#define InfName     __gdtoa_InfName
#define NanName     __gdtoa_NanName
#define ULtoQ       __gdtoa_ULtoQ
#define ULtof       __gdtoa_ULtof
#define ULtod       __gdtoa_ULtod
#define ULtodd      __gdtoa_ULtodd
#define ULtox       __gdtoa_ULtox
#define ULtoxL      __gdtoa_ULtoxL
#define add_nanbits __gdtoa_add_nanbits
#define any_on      __gdtoa_any_on
#define b2d         __gdtoa_b2d
#define bigtens     __gdtoa_bigtens
#define cmp         __gdtoa_cmp
#define copybits    __gdtoa_copybits
#define d2b         __gdtoa_d2b
#define decrement   __gdtoa_decrement
#define diff        __gdtoa_diff
#define dtoa_result __gdtoa_dtoa_result
#define g__fmt      __gdtoa_g__fmt
#define gethex      __gdtoa_gethex
#define hexdig      __gdtoa_hexdig
#define hexnan      __gdtoa_hexnan
#define hi0bits(x)  __gdtoa_hi0bits((ULong)(x))
#define i2b         __gdtoa_i2b
#define increment   __gdtoa_increment
#define lo0bits     __gdtoa_lo0bits
#define lshift      __gdtoa_lshift
#define match       __gdtoa_match
#define mult        __gdtoa_mult
#define multadd     __gdtoa_multadd
#define nrv_alloc   __gdtoa_nrv_alloc
#define pow5mult    __gdtoa_pow5mult
#define quorem      __gdtoa_quorem
#define ratio       __gdtoa_ratio
#define rshift      __gdtoa_rshift
#define rv_alloc    __gdtoa_rv_alloc
#define s2b         __gdtoa_s2b
#define set_ones    __gdtoa_set_ones
#define strcp       __gdtoa_strcp
#define strtoIg     __gdtoa_strtoIg
#define sum         __gdtoa_sum
#define tens        __gdtoa_tens
#define tinytens    __gdtoa_tinytens
#define tinytens    __gdtoa_tinytens
#define trailz      __gdtoa_trailz
#define ulp         __gdtoa_ulp

extern char *add_nanbits(char *, size_t, ULong *, int);

hidden extern char *dtoa_result;
hidden extern CONST double bigtens[];
hidden extern CONST double tens[];
hidden extern CONST double tinytens[];
hidden extern const unsigned char hexdig[];
hidden extern const char *const InfName[6];
hidden extern const char *const NanName[3];

extern Bigint *Balloc(int MTd);
extern void Bfree(Bigint *MTd);
extern void ULtof(ULong *, ULong *, Long, int);
extern void ULtod(ULong *, ULong *, Long, int);
extern void ULtodd(ULong *, ULong *, Long, int);
extern void ULtoQ(ULong *, ULong *, Long, int);
extern void ULtox(UShort *, ULong *, Long, int);
extern void ULtoxL(ULong *, ULong *, Long, int);
extern ULong any_on(Bigint *, int);
extern double b2d(Bigint *, int *);
extern int cmp(Bigint *, Bigint *);
extern void copybits(ULong *, int, Bigint *);
extern Bigint *d2b(double, int *, int *MTd);
extern void decrement(Bigint *);
extern Bigint *diff(Bigint *, Bigint *MTd);
extern char *g__fmt(char *, char *, char *, int, ULong, size_t);
extern int gethex(CONST char **, CONST FPI *, Long *, Bigint **, int MTd);
extern void __gdtoa_hexdig_init(void);
extern int hexnan(CONST char **, CONST FPI *, ULong *);
extern int __gdtoa_hi0bits(ULong);
extern Bigint *i2b(int MTd);
extern Bigint *increment(Bigint *MTd);
extern int lo0bits(ULong *);
extern Bigint *lshift(Bigint *, int MTd);
extern int match(CONST char **, char *);
extern Bigint *mult(Bigint *, Bigint *MTd);
extern Bigint *multadd(Bigint *, int, int MTd);
extern char *nrv_alloc(char *, char **, int MTd);
extern Bigint *pow5mult(Bigint *, int MTd);
extern int quorem(Bigint *, Bigint *);
extern double ratio(Bigint *, Bigint *);
extern void rshift(Bigint *, int);
extern char *rv_alloc(int MTd);
extern Bigint *s2b(CONST char *, int, int, ULong, int MTd);
extern Bigint *set_ones(Bigint *, int MTd);
extern char *strcp(char *, const char *);
extern int strtoIg(CONST char *, char **, CONST FPI *, Long *, Bigint **,
                   int *);
extern Bigint *sum(Bigint *, Bigint *MTd);
extern int trailz(Bigint *);
extern double ulp(U *);

#ifdef __cplusplus
}
#endif
/*
 * NAN_WORD0 and NAN_WORD1 are only referenced in strtod.c.  Prior to
 * 20050115, they used to be hard-wired here (to 0x7ff80000 and 0,
 * respectively), but now are determined by compiling and running
 * qnan.c to generate gd_qnan.h, which specifies d_QNAN0 and d_QNAN1.
 * Formerly gdtoaimp.h recommended supplying suitable -DNAN_WORD0=...
 * and -DNAN_WORD1=...  values if necessary.  This should still work.
 * (On HP Series 700/800 machines, -DNAN_WORD0=0x7ff40000 works.)
 */
#ifdef IEEE_Arith
#ifndef NO_INFNAN_CHECK
#undef INFNAN_CHECK
#define INFNAN_CHECK
#endif
#ifdef IEEE_MC68k
#define _0 0
#define _1 1
#ifndef NAN_WORD0
#define NAN_WORD0 d_QNAN0
#endif
#ifndef NAN_WORD1
#define NAN_WORD1 d_QNAN1
#endif
#else
#define _0 1
#define _1 0
#ifndef NAN_WORD0
#define NAN_WORD0 d_QNAN1
#endif
#ifndef NAN_WORD1
#define NAN_WORD1 d_QNAN0
#endif
#endif
#else
#undef INFNAN_CHECK
#endif

#undef SI
#ifdef Sudden_Underflow
#define SI 1
#else
#define SI 0
#endif
