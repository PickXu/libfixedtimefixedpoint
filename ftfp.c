#include "ftfp.h"
#include <math.h>

// ensure that we never divide by 0. Caller is responsible for checking.
#define FIX_UNSAFE_DIV_32(op1, op2) \
  (ROUND_TO_EVEN(((FIX_SIGN_TO_64(FIX_DATA_BITS(op1))<<32) / \
                   FIX_SIGN_TO_64((op2) | (FIX_DATA_BITS(op2) == 0))),FIX_POINT_BITS)<<2)

// Note that you will lose the bottom bit of op2 for overflow safety
// Shift op2 right by 2 to gain 2 extra overflow bits
#define FIX_DIV_32(op1, op2, overflow) \
  ({ \
    uint64_t fdiv32tmp = FIX_UNSAFE_DIV_32(op1, \
      SIGN_EX_SHIFT_RIGHT_32(op2, 1)); \
    uint64_t masked = fdiv32tmp & 0xFFFFFFFF00000000; \
    overflow = !((masked == 0xFFFFFFFF00000000) | (masked == 0)); \
    (fdiv32tmp >> 1) & 0xffffffff; \
  })

// Sign extend it all, this will help us correctly catch overflow
#define FIX_UNSAFE_MUL_32(op1, op2) \
  (ROUND_TO_EVEN(FIX_SIGN_TO_64(op1) * FIX_SIGN_TO_64(op2),17))

#define FIX_MUL_32(op1, op2, overflow) \
  ({ \
    uint64_t tmp = FIX_UNSAFE_MUL_32(op1, op2); \
    /* inf only if overflow, and not a sign thing */ \
    overflow |= \
      !(((tmp & 0xFFFFFFFF80000000) == 0xFFFFFFFF80000000) \
       | ((tmp & 0xFFFFFFFF80000000) == 0)); \
    tmp; \
   })


inline uint32_t uint32_log2(uint32_t o) {
  uint32_t scratch = o;
  uint32_t log2;
  uint32_t shift;

  log2 =  (scratch > 0xFFFF) << 4; scratch >>= log2;
  shift = (scratch >   0xFF) << 3; scratch >>= shift; log2 |= shift;
  shift = (scratch >    0xF) << 2; scratch >>= shift; log2 |= shift;
  shift = (scratch >    0x3) << 1; scratch >>= shift; log2 |= shift;
  log2 |= (scratch >> 1);
  return log2;
}

fixed fix_neg(fixed op1){
  uint8_t isinfpos;
  uint8_t isinfneg;
  uint8_t isnan;

  fixed tempresult;

  // Flip our infs
  isinfpos = FIX_IS_INF_NEG(op1);
  isinfneg = FIX_IS_INF_POS(op1);

  // NaN is still NaN
  isnan = FIX_IS_NAN(op1);

  // 2s comp negate the data bits
  tempresult = FIX_DATA_BITS(((~op1) + 4));

  //TODO: Check overflow? other issues?


  // Combine
  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(tempresult);
}

fixed fix_abs(fixed op1){
  uint8_t isinfpos;
  uint8_t isinfneg;
  uint8_t isnan;

  isinfpos = FIX_IS_INF_POS(op1);
  isinfneg = FIX_IS_INF_NEG(op1);
  isnan = FIX_IS_NAN(op1);

  fixed tempresult = MASK_UNLESS(FIX_TOP_BIT(~op1), op1) |
    MASK_UNLESS(  FIX_TOP_BIT(op1), FIX_DATA_BITS(((~op1) + 4)));

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS((isinfpos | isinfneg) & (!isnan)) |
    FIX_DATA_BITS(tempresult);
}

fixed fix_sub(fixed op1, fixed op2) {
  return fix_add(op1,fix_neg(op2));
}

fixed fix_div(fixed op1, fixed op2) {
  uint8_t isnan;
  uint8_t isinf;
  uint8_t isinfpos;
  uint8_t isinfneg;

  uint8_t isinfop1;
  uint8_t isinfop2;
  uint8_t isnegop1;
  uint8_t isnegop2;

  fixed tempresult;

  isnan = FIX_IS_NAN(op1) | FIX_IS_NAN(op2) | (op2 == 0);

  // Take advantage of the extra bits we get out from doing this in uint64_t
  tempresult = FIX_DIV_32(op1, op2, isinf);

  isinfop1 = (FIX_IS_INF_NEG(op1) | FIX_IS_INF_POS(op1));
  isinfop2 = (FIX_IS_INF_NEG(op2) | FIX_IS_INF_POS(op2));
  isnegop1 = FIX_IS_INF_NEG(op1) | (FIX_IS_NEG(op1) & !isinfop1);
  isnegop2 = FIX_IS_INF_NEG(op2) | (FIX_IS_NEG(op2) & !isinfop2);

  //Update isinf
  isinf = (isinf | isinfop1 | isinfop2) & (!isnan);

  isinfpos = isinf & !(isnegop1 ^ isnegop2);

  isinfneg = isinf & (isnegop1 ^ isnegop2);

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(tempresult);
}


fixed fix_mul(fixed op1, fixed op2) {

  uint8_t isinfop1 = (FIX_IS_INF_NEG(op1) | FIX_IS_INF_POS(op1));
  uint8_t isinfop2 = (FIX_IS_INF_NEG(op2) | FIX_IS_INF_POS(op2));
  uint8_t isnegop1 = FIX_IS_INF_NEG(op1) | (FIX_IS_NEG(op1) & !isinfop1);
  uint8_t isnegop2 = FIX_IS_INF_NEG(op2) | (FIX_IS_NEG(op2) & !isinfop2);

  uint8_t isnan = FIX_IS_NAN(op1) | FIX_IS_NAN(op2);
  uint8_t isinf = 0;
  uint64_t tmp;

  tmp = FIX_MUL_32(op1, op2, isinf);

  isinf = (isinfop1 | isinfop2 | isinf) & (!isnan);

  uint8_t isinfpos = isinf & !(isnegop1 ^ isnegop2);
  uint8_t isinfneg = isinf & (isnegop1 ^ isnegop2);

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(tmp);
}


fixed fix_add(fixed op1, fixed op2) {

  uint8_t isnan;
  uint8_t isinfpos;
  uint8_t isinfneg;

  fixed tempresult;

  isnan = FIX_IS_NAN(op1) | FIX_IS_NAN(op2);
  isinfpos = FIX_IS_INF_POS(op1) | FIX_IS_INF_POS(op2);
  isinfneg = FIX_IS_INF_NEG(op1) | FIX_IS_INF_NEG(op2);

  tempresult = op1 + op2;

  // check if we're overflowing: adding two positive numbers that results in a
  // 'negative' number:
  //   if both inputs are positive (top bit == 0) and the result is 'negative'
  //   (top bit nonzero)
  isinfpos |= (FIX_TOP_BIT(op1) | FIX_TOP_BIT(op2)) ==
    0x0 && (FIX_TOP_BIT(tempresult) != 0x0);

  // check if there's negative infinity overflow
  isinfneg |= (FIX_TOP_BIT(op1) & FIX_TOP_BIT(op2)) ==
    FIX_TOP_BIT_MASK && (FIX_TOP_BIT(tempresult) == 0x0);

  // Force infpos to win in cases where it is unclear
  isinfneg &= !isinfpos;

  // do some horrible bit-ops to make result into what we want

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(tempresult);
}

fixed fix_floor(fixed op1) {
  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = FIX_IS_INF_NEG(op1);
  uint8_t isnan = FIX_IS_NAN(op1);

  fixed tempresult = op1 & ~((1 << (FIX_FRAC_BITS + FIX_FLAG_BITS))-1);

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(tempresult);
}

fixed fix_ceil(fixed op1) {
  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = FIX_IS_INF_NEG(op1);
  uint8_t isnan = FIX_IS_NAN(op1);
  uint8_t ispos = !FIX_IS_NEG(op1);

  uint32_t frac_mask = (1 << (FIX_FRAC_BITS + FIX_FLAG_BITS))-1;

  fixed tempresult = (op1 & ~frac_mask) +
    ((!!(op1 & frac_mask)) << (FIX_FRAC_BITS + FIX_FLAG_BITS));

  // If we used to be positive and we wrapped around, switch to INF_POS.
  isinfpos |= ((tempresult == FIX_MIN) & ispos);

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(tempresult);
}

fixed fix_exp(fixed op1) {
  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isnan = FIX_IS_NAN(op1);

  uint32_t log2 = uint32_log2(op1);
  uint32_t log2_neg = uint32_log2((~op1) + 4);

  fixed scratch =
    MASK_UNLESS(!FIX_IS_NEG(op1),
      MASK_UNLESS(log2 <= FIX_POINT_BITS, op1) |
      MASK_UNLESS(log2 > FIX_POINT_BITS, op1 >> (log2 - FIX_POINT_BITS))
    ) |
    MASK_UNLESS(FIX_IS_NEG(op1),
      MASK_UNLESS(log2_neg <= FIX_POINT_BITS, op1) |
      MASK_UNLESS(log2_neg > FIX_POINT_BITS, SIGN_EX_SHIFT_RIGHT_32(op1, (log2_neg - FIX_POINT_BITS)))
      );

  uint8_t squarings =
    MASK_UNLESS(!FIX_IS_NEG(op1),
      MASK_UNLESS(log2 <= FIX_POINT_BITS, 0) |
      MASK_UNLESS(log2 > FIX_POINT_BITS, (log2 - FIX_POINT_BITS))
    ) |
    MASK_UNLESS(FIX_IS_NEG(op1),
      MASK_UNLESS(log2_neg <= FIX_POINT_BITS, 0) |
      MASK_UNLESS(log2_neg > FIX_POINT_BITS, (log2_neg - FIX_POINT_BITS))
      );

  fixed x_i = FIXINT(1);
  fixed x_factorial = FIXINT(1);
  fixed e_x = FIXINT(1);
  fixed term = FIXINT(1);

  for(int i = 1; i < 12; i ++) {

    x_i = FIX_UNSAFE_MUL_32(x_i, scratch);
    x_factorial = FIX_UNSAFE_MUL_32(x_factorial, FIXINT(i));

    term = FIX_UNSAFE_MUL_32(term, scratch);
    term = FIX_UNSAFE_DIV_32(term, FIXINT(i));
    e_x += term;

  }

  fixed result = e_x;

  uint8_t inf;

  // X is approximately in the range [-2^16, 2^16], and we map it down to [-2,
  // 2]. We need one squaring for each halving, which means that squarings can
  // be at most log2(2^16)-2, or 14.
  //
  // But that's overzealous: e^x must fit in 2^16. If we reduced the number
  // before the approximation, then it will be at least 1, and so the
  // approximation will produce at least e^1, or ~2.718. This requires only
  // log2(ln(2**16)) successive doublings, or about 3.4. Round up to 4.
  for(int i = 0; i < 4; i++) {
    inf = 0;
    fixed r2 = FIX_MUL_32(result, result, inf);
    result = MASK_UNLESS(squarings > 0, r2) | MASK_UNLESS(squarings == 0, result);
    isinfpos |= MASK_UNLESS(squarings > 0, inf);

    squarings = MASK_UNLESS(squarings > 0, squarings-1);
  }

  // note that we want to return 0 if op1 is FIX_INF_NEG...
  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    MASK_UNLESS(!FIX_IS_INF_NEG(op1), FIX_DATA_BITS(result));
}

#define MUL_2x28(op1, op2) ((uint32_t) ((((int64_t) ((int32_t) (op1)) ) * ((int64_t) ((int32_t) (op2)) )) >> (32-4)) & 0xffffffff)

fixed fix_ln(fixed op1) {
  /* Approach taken from http://eesite.bitbucket.org/html/software/log_app/log_app.html */

  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = FIX_IS_INF_NEG(op1) | (op1 == 0);
  uint8_t isnan = FIX_IS_NAN(op1) | FIX_IS_NEG(op1);

  // compute (int) log2(op1)  (as a uint32_t, not fixed)
  uint32_t log2 = uint32_log2(op1);

  // We need to figure out how to map op1 into [-.5, .5], to use our polynomial
  // approxmation. First, we'll map op1 into [0.5, 1.5].
  //
  // We'll look at the top 2 bits of the number. If they're both 1, then we'll
  // move it to be just above 0.5. In that case, though, we need to increment
  // the log2 by 1.
  uint32_t top2mask = (3 << (log2 - 1));
  uint8_t top2set = ((op1 & top2mask) ^ top2mask) == 0;
  log2 += top2set;

  // we need to move op1 into [-0.5, 0.5] in xx.2.28
  //
  // first, let's move to [0.5, 1.5] in xx.2.28...
  uint32_t m = MASK_UNLESS(log2 <= 28, op1 << (28 - (log2))) |
    MASK_UNLESS(log2 > 28, op1 >> (log2 - 28));

  // and then shift down by '1'. (1.28 bits of zero)
  m -= (1 << 28);

  fixed ln2 = 0x000162e4; // python: "0x%08x"%(math.log(2) * 2**17)
  fixed nln2 = ln2 * (log2 - FIX_FRAC_BITS - FIX_FLAG_BITS); // correct for nonsense
    // we want this to go negative for numbers < 1.

  // now, calculate ln(1+m):
  uint32_t c411933 = 0x0697470e; // "0x%08x"%(0.411933 * 2**28)
  uint32_t c574785 = 0x093251c1; // "0x%08x"%(0.574785 * 2**28)
  uint32_t c994946 = 0x0feb4c7f; // "0x%08x"%(0.994946 * 2**28)
  uint32_t c0022683 = 0x00094a7c; // "0x%08x"%(0.0022683 * 2**28)

  // (((0.411x - 0.57)x + 0.99)x + 0.0022..

  uint32_t tempresult =
    (MUL_2x28(m,
        MUL_2x28(m,
          MUL_2x28(m,
            c411933)
          - c574785)
        + c994946)
      + c0022683);

  tempresult = SIGN_EX_SHIFT_RIGHT_32(tempresult, 28 - FIX_FRAC_BITS - FIX_FLAG_BITS);
  tempresult += nln2 - 0x128; // adjustment constant for when log should be 0

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos) |
    FIX_IF_INF_NEG(isinfneg) |
    FIX_DATA_BITS(tempresult);
}



fixed fix_log2(fixed op1) {
  /* Approach taken from http://eesite.bitbucket.org/html/software/log_app/log_app.html */

  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = FIX_IS_INF_NEG(op1) | (op1 == 0);
  uint8_t isnan = FIX_IS_NAN(op1) | FIX_IS_NEG(op1);

  // compute (int) log2(op1)  (as a uint32_t, not fixed)
  uint32_t log2 = uint32_log2(op1);

  // We need to figure out how to map op1 into [-.5, .5], to use our polynomial
  // approxmation. First, we'll map op1 into [0.5, 1.5].
  //
  // We'll look at the top 2 bits of the number. If they're both 1, then we'll
  // move it to be just above 0.5. In that case, though, we need to increment
  // the log2 by 1.
  uint32_t top2mask = (3 << (log2 - 1));
  uint8_t top2set = ((op1 & top2mask) ^ top2mask) == 0;
  log2 += top2set;

  // we need to move op1 into [-0.5, 0.5] in xx.2.28
  //
  // first, let's move to [0.5, 1.5] in xx.2.28...
  uint32_t m = MASK_UNLESS(log2 <= 28, op1 << (28 - (log2))) |
    MASK_UNLESS(log2 > 28, op1 >> (log2 - 28));

  // and then shift down by '1'. (1.28 bits of zero)
  m -= (1 << 28);

  fixed n = (log2 - FIX_FRAC_BITS - FIX_FLAG_BITS) << (FIX_FRAC_BITS + FIX_FLAG_BITS);

  // octave:31> x = -0.5:1/10000:0.5;
  // octave:32> polyfit( x, log2(x+1), 3)
  // ans =

  //   0.5777570  -0.8114606   1.4371765   0.0023697

  // now, calculate log2(1+m):
  //
  uint32_t c5777570 = 0x093e7e1f; // "0x%08x"%(0.5777570 * 2**28)
  uint32_t c8114606 = 0x0cfbbe1c; // "0x%08x"%(0.8114606 * 2**28)
  uint32_t c14371765 = 0x16feacc9; // "0x%08x"%(1.4371765 * 2**28)
  uint32_t c0023697 = 0x0009b4cf; // "0x%08x"%(0.0023697 * 2**28)

  // (((0.577x - 0.811)x + 1.43)x + 0.0023..

  uint32_t tempresult =
    (MUL_2x28(m,
        MUL_2x28(m,
          MUL_2x28(m,
            c5777570)
          - c8114606)
        + c14371765)
      + c0023697);

  tempresult = SIGN_EX_SHIFT_RIGHT_32(tempresult, 28 - FIX_FRAC_BITS - FIX_FLAG_BITS);
  tempresult += n - 0x134; // adjustment constant for when log should be 0

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos) |
    FIX_IF_INF_NEG(isinfneg) |
    FIX_DATA_BITS(tempresult);
}

fixed fix_log10(fixed op1) {
  /* Approach taken from http://eesite.bitbucket.org/html/software/log_app/log_app.html */

  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = FIX_IS_INF_NEG(op1) | (op1 == 0);
  uint8_t isnan = FIX_IS_NAN(op1) | FIX_IS_NEG(op1);

  // compute (int) log2(op1)  (as a uint32_t, not fixed)
  uint32_t log2 = uint32_log2(op1);

  uint32_t top2mask = (3 << (log2 - 1));
  uint8_t top2set = ((op1 & top2mask) ^ top2mask) == 0;
  log2 += top2set;

  // we need to move op1 into [-0.5, 0.5] in xx.2.28
  //
  // first, let's move to [0.5, 1.5] in xx.2.28...
  uint32_t m = MASK_UNLESS(log2 <= 28, op1 << (28 - (log2))) |
    MASK_UNLESS(log2 > 28, op1 >> (log2 - 28));

  // and then shift down by '1'. (1.28 bits of zero)
  m -= (1 << 28);

  fixed log10_2 = 0x00009a20; // python: "0x%08x"%(math.log(2,10) * 2**17)
  fixed nlog10_2 = log10_2 * (log2 - FIX_FRAC_BITS - FIX_FLAG_BITS); // correct for nonsense
    // we want this to go negative for numbers < 1.

  // A third-order or fourth-order approximation polynomial does very poorly on
  // log10. Use a 5th order approximation instead:

  // octave:31> x = -0.5:1/10000:0.5;
  // octave:32> polyfit( x, log10(x+1), 5)
  // ans =

  //    1.1942e-01  -1.3949e-01   1.4074e-01  -2.1438e-01   4.3441e-01  -3.4210e-05

  // now, calculate log10(1+m):
  //
  // constants:
  //
  // print "\n".join(["uint32_t k%d = 0x%08x;"%(5-i, num * 2**28) for
  // i,num in enumerate([abs(eval(x)) for x in re.split(" +", "1.1942e-01
  // -1.3949e-01   1.4074e-01  -2.1438e-01   4.3441e-01  -3.4210e-05" )])])
  //

  uint32_t k5 = 0x01e924f2;
  uint32_t k4 = 0x023b59dd;
  uint32_t k3 = 0x02407896;
  uint32_t k2 = 0x036e19b9;
  uint32_t k1 = 0x06f357e6;
  uint32_t k0 = 0x000023df;

  uint32_t tempresult =
    (MUL_2x28(m,
       MUL_2x28(m,
         MUL_2x28(m,
            MUL_2x28(m,
              MUL_2x28(m,
                k5)
              - k4)
            + k3)
          - k2)
        + k1)
       - k0);

  tempresult = SIGN_EX_SHIFT_RIGHT_32(tempresult, 28 - FIX_FRAC_BITS - FIX_FLAG_BITS);
  tempresult += nlog10_2;

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos) |
    FIX_IF_INF_NEG(isinfneg) |
    FIX_DATA_BITS(tempresult);
}


fixed fix_sqrt(fixed op1) {
  // We're going to use Newton's Method with a fixed number of iterations.
  // The polynomial to use is:
  //
  //     f(x)  = x^2 - op1
  //     f'(x) = 2x
  //
  // Each update cycle is then:
  //
  //     x' = x - f(x) / f'(x)

  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = 0;
  uint8_t isnan = FIX_IS_NAN(op1) | FIX_IS_NEG(op1);

  // We need an initial guess. Let's use log_2(op1), since that's fairly quick
  // and easy, and not horribly wrong.
  uint32_t scratch = op1;
  uint32_t log2; // compute (int) log2(op1)  (as a uint32_t, not fixed)
  uint32_t shift;

  log2 =  (scratch > 0xFFFF) << 4; scratch >>= log2;
  shift = (scratch >   0xFF) << 3; scratch >>= shift; log2 |= shift;
  shift = (scratch >    0xF) << 2; scratch >>= shift; log2 |= shift;
  shift = (scratch >    0x3) << 1; scratch >>= shift; log2 |= shift;
  log2 |= (scratch >> 1);
  //log2 is now log2(op1), considered as a uint32_t

  // Make a guess! Use log2(op1) if op1 > 2, otherwise just uhhhh mul op1 by 2.
  int64_t x = MASK_UNLESS(op1 >= (1<<(FIX_FRAC_BITS + FIX_FLAG_BITS+1)),
                           FIXINT(log2 - (FIX_FLAG_BITS + FIX_FRAC_BITS))) |
               MASK_UNLESS(op1 < (1<<(FIX_FRAC_BITS + FIX_FLAG_BITS+1)),
                           op1 << 1);


  // We're going to do all math in a 47.17 uint64_t

  uint64_t op1neg = FIX_SIGN_TO_64(fix_neg(op1));

  // we're going to use 15.17 fixed point numbers to do the calculation, and
  // then mask off our flag bits later
  for(int i = 0; i < 8; i++) {
    int64_t x2 = (x * x) >> 17;
    int64_t t = x2 + op1neg;

    x = x | (x==0); // don't divide by zero...

    // t = t / f'(x) = t / 2x
    t = ROUND_TO_EVEN((t<<22) / (x<<1), 5);

    x = x - t;
  }

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos) |
    FIX_IF_INF_NEG(isinfneg) |
    FIX_DATA_BITS(x & 0xffffffff);
}

void cordic(uint32_t* Zext, uint32_t* Cext, uint32_t* Sext) {
  /* See http://math.exeter.edu/rparris/peanut/cordic.pdf for the best
   * explanation of CORDIC I've found.
   */

  /* Use circle fractions instead of angles. Will be [0,4) in 2.28 format. */
  /* Use 2.28 notation for angles and constants. */

  /* Generate the cordic angles in terms of circle fractions:
   * ", ".join(["0x%08x"%((math.atan((1/2.)**i) / (math.pi/2)*2**28)) for i in range(0,24)])
   */
  uint32_t A[] = {
    0x08000000, 0x04b90147, 0x027ece16, 0x01444475, 0x00a2c350, 0x005175f8,
    0x0028bd87, 0x00145f15, 0x000a2f94, 0x000517cb, 0x00028be6, 0x000145f3,
    0x0000a2f9, 0x0000517c, 0x000028be, 0x0000145f, 0x00000a2f, 0x00000517,
    0x0000028b, 0x00000145, 0x000000a2, 0x00000051, 0x00000028, 0x00000014
  };

  uint32_t Z = *Zext;
  uint32_t C = *Cext;
  uint32_t S = *Sext;

  uint32_t C_ = 0;
  uint32_t S_ = 0;
  uint32_t pow2 = 0;

  /* D should be 1 if Z is positive, or -1 if Z is negative. */
  uint32_t D = SIGN_EX_SHIFT_RIGHT_32(Z, 31) | 1;

  for(int m = 0; m < 24; m++) {
    pow2 = 2 << (28 - 1 - m);

    /* generate the m+1th values of Z, C, S, and D */
    Z = Z - D * A[m];

    C_ = C - D*MUL_2x28(pow2, S);
    S_ = S + D*MUL_2x28(pow2, C);

    C = C_;
    S = S_;
    D = SIGN_EX_SHIFT_RIGHT_32(Z, 31) | 1;
  }

  *Zext = Z;
  *Cext = C;
  *Sext = S;

  return;

}

fixed fix_cordic_sin(fixed op1) {
  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = FIX_IS_INF_NEG(op1);
  uint8_t isnan = FIX_IS_NAN(op1);

  uint32_t Z = fix_circle_frac(op1);
  uint32_t C = CORDIC_P;
  uint32_t S = 0;

  // The circle fraction is in [0,4).
  // Move it to [-1, 1], where cordic will work for sin
  uint32_t top_bits_differ = ((Z >> 28) & 0x1) ^ ((Z >> 29) & 0x1);
  Z = MASK_UNLESS(top_bits_differ, (1<<29) - Z) |
      MASK_UNLESS(!top_bits_differ, SIGN_EXTEND(Z, 30));

  cordic(&Z, &C, &S);

  return FIX_IF_NAN(isnan | isinfpos | isinfneg) |
    FIX_DATA_BITS(ROUND_TO_EVEN_SIGNED(S, (28 - (FIX_POINT_BITS))));
}

fixed fix_cordic_cos(fixed op1) {
  uint8_t isinfpos = FIX_IS_INF_POS(op1);
  uint8_t isinfneg = FIX_IS_INF_NEG(op1);
  uint8_t isnan = FIX_IS_NAN(op1);

  uint32_t circle_frac = fix_circle_frac(op1);

  /* flip up into Q1 and Q2 */
  uint8_t Q3or4 = !!((2 << 28) & circle_frac);
  circle_frac = MASK_UNLESS( Q3or4, (4<<28) - circle_frac) |
                MASK_UNLESS(!Q3or4, circle_frac);

  /* Switch from cos on an angle in Q1 or Q2 to sin in Q4 or Q1.
   * This necessitates flipping the angle from [0,2] to [1, -1].
   */
  circle_frac = (1 << (28)) - circle_frac;

  uint32_t Z = circle_frac;
  uint32_t C = CORDIC_P;
  uint32_t S = 0;

  cordic(&Z, &C, &S);

  return FIX_IF_NAN(isnan | isinfpos | isinfneg) |
    FIX_DATA_BITS(ROUND_TO_EVEN_SIGNED(S, (28 - (FIX_POINT_BITS))));
}

fixed fix_cordic_tan(fixed op1) {
  // We will return NaN if you pass in infinity, but we might return infinity
  // anyway...
  uint8_t isinfpos = 0;
  uint8_t isinfneg = 0;
  uint8_t isnan = FIX_IS_NAN(op1) | FIX_IS_INF_POS(op1) | FIX_IS_INF_NEG(op1);

  /* The circle fraction is in [0,4). The cordic algorithm can handle [-1, 1],
   * and tan has rotational symmetry at z = 1.
   *
   * If we're in Q2 or 3, subtract 2 from the circle frac.
   */

  uint32_t circle_frac = fix_circle_frac(op1);

  uint32_t top_bits_differ = ((circle_frac >> 28) & 0x1) ^ ((circle_frac >> 29) & 0x1);
  uint32_t Z = MASK_UNLESS( top_bits_differ, circle_frac - (1<<29)) |
               MASK_UNLESS(!top_bits_differ, SIGN_EXTEND(circle_frac, 30));

  uint32_t C = CORDIC_P;
  uint32_t S = 0;

  cordic(&Z, &C, &S);

  uint8_t isinf;
  uint64_t result = FIX_DIV_32(S, C, isinf);

  isinfpos |= (isinf | (C==0)) && !FIX_IS_NEG(S);
  isinfneg |= (isinf | (C==0)) && FIX_IS_NEG(S);

  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(ROUND_TO_EVEN_SIGNED(result, 2) << 2);
}

uint32_t fix_circle_frac(fixed op1) {
  /* Scratchpad to compute z:
   *
   * variables are lowercase, and considered as integers.
   * real numbers are capitalized, and are the space we're trying to work in
   *
   * op1: input. fixed: 15.15.b00
   * X = op1 / 2^17                          # in radians
   * TAU = 6.28318530718...                  # 2pi
   * QTAU = TAU/4
   *
   * Z = (X / QTAU) % 4                      # the dimensionless circle fraction
   *
   * circle_frac = Z * 2^28                  # will fit in 30 bits, 2 for extra
   *
   * big_op = op1 << 32
   * BIG_OP = X * 2^32
   *
   * big_qtau = floor(QTAU * 2^(17+32-32+4)) # remove 32-4 bits so that big_op /
   *          = floor(QTAU * 2^21)           # big_tau has 28-bits of fraction and
   *                                         # 2 bits of integer
   *
   * circle_frac = big_op / big_qtau
   *   = X * 2^32 / floor(QTAU * 2^21)
   *  ~= X * 2^11 / QTAU
   *   = (X / QTAU) * 2^11
   *  ~= (op1 / QTAU / 2^17) * 2^11
   *   = (op1 / QTAU) * 2^28                # in [0,4), fills 30 bits at 2.28
   *
   */

  int64_t big_op = ((int64_t) ((int32_t) FIX_DATA_BITS(op1))) << 32;
  int32_t big_tau = 0x3243f6;  // in python: "0x%x"%(math.floor((math.pi / 2) * 2**21))
  int32_t circle_frac = (big_op / big_tau) & 0x3fffffff;
  return circle_frac;
}

fixed fix_sin(fixed op1) {
  uint8_t isinfpos;
  uint8_t isinfneg;
  uint8_t isnan;

  isinfpos = FIX_IS_INF_POS(op1);
  isinfneg = FIX_IS_INF_NEG(op1);
  isnan = FIX_IS_NAN(op1);

  /* Math:
   *
   * See http://www.coranac.com/2009/07/sines/ for a great overview.
   *
   * Fifth order taylor approximation:
   *
   *   Sin_5(x) = ax - bx^3 + cx^5
   *
   * where:
   *
   *   a = 1.569718634 (almost but not quite pi/2)
   *   b = 2a - (5/2)
   *   c = a - (3/2)
   *   Constants minimise least-squared error (according to Coranac).
   *
   * Simplify for computation:
   *
   *   Sin_5(x) = az - bz^3 + cz^5
   *            = z(a + (-bz^2 + cz^4))
   *            = z(a + z^2(cz^2 - b))
   *            = z(a - z^2(b - cz^2))
   *
   *   where z = x / (tau/4).
   *
   */

  uint32_t circle_frac = fix_circle_frac(op1);

  /* for sin, we need to map the circle frac [0,4) to [-1, 1]:
   *
   * Z' =    2 - Z       { if 1 <= z < 3
   *         Z           { otherwise
   *
   * zp =                                   # bits: xx.2.28
   *         (1<<31) - circle_frac { if 1 <= circle_frac[29:28] < 3
   *         circle_frac           { otherwise
   */
  uint32_t top_bits_differ = ((circle_frac >> 28) & 0x1) ^ ((circle_frac >> 29) & 0x1);
  uint32_t zp = MASK_UNLESS(top_bits_differ, (1<<29) - circle_frac) |
                MASK_UNLESS(!top_bits_differ, SIGN_EXTEND(circle_frac, 30));

  uint32_t zp2 = MUL_2x28(zp, zp);

  uint32_t a = 0x64764525; // a = 1.569718634; "0x%08x"%(a*2**30)"
  uint32_t b = 0x28ec8a4a; // "0x%08x"%((2*a - (5/2.)) *2**30)
  uint32_t c = 0x04764525; // "0x%08x"%((a - (3/2.)) *2**30)

  uint32_t result =
    MUL_2x28(zp,
        (a - MUL_2x28(zp2,
                          b - MUL_2x28(c, zp2))));

  // result is xx.2.28, shift over into fixed, sign extend to full result
  return FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos & (!isnan)) |
    FIX_IF_INF_NEG(isinfneg & (!isnan)) |
    FIX_DATA_BITS(SIGN_EXTEND(
          result >> (30 - FIX_FRAC_BITS - FIX_FLAG_BITS),
          (32 - (30 - FIX_FRAC_BITS - FIX_FLAG_BITS ) /* 19 */ )));

#undef MUL_2x28
}

// TODO: not constant time. will work for now.
void fix_print(char* buffer, fixed f) {
  double d;
  fixed f_ = f;

  if(FIX_IS_NAN(f)) {
    memcpy(buffer, "NaN", 4);
    return;
  }
  if(FIX_IS_INF_POS(f)) {
    memcpy(buffer, "+Inf", 5);
    return;
  }
  if(FIX_IS_INF_NEG(f)) {
    memcpy(buffer, "-Inf", 5);
    return;
  }

  uint8_t neg = !!FIX_TOP_BIT(f);

  if(neg) {
    f_ = ~f_ + 4;
  }

  d = f_ >> 17;
  d += ((f_ >> 2) & 0x7fff) / (float) (1<<15);

  if(neg) {
    d *= -1;
  }

  sprintf(buffer, "%.015f", d);
}

fixed fix_convert_double(double d) {
  uint64_t bits = *(uint64_t*) &d;
  uint32_t exponent = ((bits >> 52) & 0x7ff) - 1023;
  uint32_t sign = bits >> 63;

  /* note that this breaks with denorm numbers. However, we'll shift those all
   * away with the exponent later */
  uint64_t mantissa = (bits & ((1ull <<52)-1)) | (d != 0 ? (1ull<<52) : 0);
  uint32_t shift = 52 - (FIX_FRAC_BITS) - exponent;

  fixed result = ((ROUND_TO_EVEN(mantissa,shift)) << FIX_FLAG_BITS) & 0xffffffff;

  uint8_t isinf = (isinf(d) || ((mantissa >> shift) & ~((1ull << (FIX_FRAC_BITS + FIX_INT_BITS)) -1)) != 0);
  uint8_t isinfpos = (d > 0) & isinf;
  uint8_t isinfneg = (d < 0) & isinf;
  uint8_t isnan = isnan(d);

  fixed result_neg = fix_neg(result);

  return
    FIX_IF_NAN(isnan) |
    FIX_IF_INF_POS(isinfpos) |
    FIX_IF_INF_NEG(isinfneg) |
    MASK_UNLESS(sign == 0, FIX_DATA_BITS(result)) |
    MASK_UNLESS(sign, FIX_DATA_BITS(result_neg));
}
