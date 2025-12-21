/*
KISS FFT - https://github.com/mborgerding/kissfft
Simplified BSD-style license: Copyright (c) 2003-2010 Mark Borgerding.
This is a minimal float-only version adapted for C++ builds.
*/

#include "kiss_fft.h"

#include <cmath>
#include <new>

#ifndef KISS_FFT_MALLOC
#define KISS_FFT_MALLOC std::malloc
#endif

#ifndef KISS_FFT_FREE
#define KISS_FFT_FREE std::free
#endif

#ifndef KISS_FFT_COS
#define KISS_FFT_COS std::cos
#endif

#ifndef KISS_FFT_SIN
#define KISS_FFT_SIN std::sin
#endif

#ifndef KISS_FFT_PI
#define KISS_FFT_PI 3.14159265358979323846f
#endif

typedef struct kiss_fft_state {
    int nfft;
    int inverse;
    kiss_fft_cpx twiddles[1];
} kiss_fft_state;

static void kf_bfly2(kiss_fft_cpx * Fout, const size_t fstride, const kiss_fft_cfg st, int m) {
    kiss_fft_cpx * Fout2 = Fout + m;
    const kiss_fft_cpx * tw1 = st->twiddles;
    kiss_fft_cpx t;
    for (int i = 0; i < m; ++i) {
        t.r = Fout2[i].r * tw1[i * fstride].r - Fout2[i].i * tw1[i * fstride].i;
        t.i = Fout2[i].r * tw1[i * fstride].i + Fout2[i].i * tw1[i * fstride].r;
        Fout2[i].r = Fout[i].r - t.r;
        Fout2[i].i = Fout[i].i - t.i;
        Fout[i].r += t.r;
        Fout[i].i += t.i;
    }
}

static void kf_bfly4(kiss_fft_cpx * Fout, const size_t fstride, const kiss_fft_cfg st, int m) {
    const kiss_fft_cpx * tw1 = st->twiddles;
    const kiss_fft_cpx * tw2 = st->twiddles + fstride;
    const kiss_fft_cpx * tw3 = st->twiddles + 2 * fstride;
    kiss_fft_cpx scratch[6];
    for (int i = 0; i < m; ++i) {
        kiss_fft_cpx * Fout0 = Fout + i;
        kiss_fft_cpx * Fout1 = Fout0 + m;
        kiss_fft_cpx * Fout2 = Fout0 + 2 * m;
        kiss_fft_cpx * Fout3 = Fout0 + 3 * m;

        scratch[0].r = Fout1->r * tw1[i * fstride].r - Fout1->i * tw1[i * fstride].i;
        scratch[0].i = Fout1->r * tw1[i * fstride].i + Fout1->i * tw1[i * fstride].r;
        scratch[1].r = Fout2->r * tw2[i * fstride].r - Fout2->i * tw2[i * fstride].i;
        scratch[1].i = Fout2->r * tw2[i * fstride].i + Fout2->i * tw2[i * fstride].r;
        scratch[2].r = Fout3->r * tw3[i * fstride].r - Fout3->i * tw3[i * fstride].i;
        scratch[2].i = Fout3->r * tw3[i * fstride].i + Fout3->i * tw3[i * fstride].r;

        scratch[5].r = Fout0->r - scratch[1].r;
        scratch[5].i = Fout0->i - scratch[1].i;
        Fout0->r += scratch[1].r;
        Fout0->i += scratch[1].i;
        scratch[3].r = scratch[0].r + scratch[2].r;
        scratch[3].i = scratch[0].i + scratch[2].i;
        scratch[4].r = scratch[0].r - scratch[2].r;
        scratch[4].i = scratch[0].i - scratch[2].i;
        Fout2->r = Fout0->r - scratch[3].r;
        Fout2->i = Fout0->i - scratch[3].i;
        Fout0->r += scratch[3].r;
        Fout0->i += scratch[3].i;
        Fout1->r = scratch[5].r + scratch[4].i;
        Fout1->i = scratch[5].i - scratch[4].r;
        Fout3->r = scratch[5].r - scratch[4].i;
        Fout3->i = scratch[5].i + scratch[4].r;
    }
}

static void kf_bfly3(kiss_fft_cpx * Fout, const size_t fstride, const kiss_fft_cfg st, int m) {
    size_t k = m;
    size_t m2 = 2 * m;
    const kiss_fft_cpx * tw1 = st->twiddles;
    const kiss_fft_cpx * tw2 = st->twiddles + fstride;
    kiss_fft_cpx scratch[5];
    for (size_t i = 0; i < k; ++i) {
        kiss_fft_cpx * Fout0 = Fout + i;
        kiss_fft_cpx * Fout1 = Fout0 + k;
        kiss_fft_cpx * Fout2 = Fout0 + m2;

        scratch[1].r = Fout1->r * tw1[i * fstride].r - Fout1->i * tw1[i * fstride].i;
        scratch[1].i = Fout1->r * tw1[i * fstride].i + Fout1->i * tw1[i * fstride].r;
        scratch[2].r = Fout2->r * tw2[i * fstride].r - Fout2->i * tw2[i * fstride].i;
        scratch[2].i = Fout2->r * tw2[i * fstride].i + Fout2->i * tw2[i * fstride].r;

        scratch[3].r = scratch[1].r + scratch[2].r;
        scratch[3].i = scratch[1].i + scratch[2].i;
        scratch[0].r = scratch[1].r - scratch[2].r;
        scratch[0].i = scratch[1].i - scratch[2].i;

        Fout1->r = Fout0->r - 0.5f * scratch[3].r;
        Fout1->i = Fout0->i - 0.5f * scratch[3].i;
        Fout0->r += scratch[3].r;
        Fout0->i += scratch[3].i;

        scratch[0].r *= -0.86602540378443864676f;
        scratch[0].i *= -0.86602540378443864676f;

        Fout2->r = Fout1->r + scratch[0].i;
        Fout2->i = Fout1->i - scratch[0].r;
        Fout1->r -= scratch[0].i;
        Fout1->i += scratch[0].r;
    }
}

static void kf_bfly5(kiss_fft_cpx * Fout, const size_t fstride, const kiss_fft_cfg st, int m) {
    const kiss_fft_cpx * twiddles = st->twiddles;
    kiss_fft_cpx scratch[13];
    const float ya = 0.5590169943749474241f;
    const float yb = 0.95105651629515357212f;

    for (int u = 0; u < m; ++u) {
        kiss_fft_cpx * Fout0 = Fout + u;
        kiss_fft_cpx * Fout1 = Fout0 + m;
        kiss_fft_cpx * Fout2 = Fout0 + 2 * m;
        kiss_fft_cpx * Fout3 = Fout0 + 3 * m;
        kiss_fft_cpx * Fout4 = Fout0 + 4 * m;

        scratch[0] = *Fout0;

        scratch[1].r = Fout1->r * twiddles[u * fstride].r - Fout1->i * twiddles[u * fstride].i;
        scratch[1].i = Fout1->r * twiddles[u * fstride].i + Fout1->i * twiddles[u * fstride].r;
        scratch[2].r = Fout2->r * twiddles[2 * u * fstride].r - Fout2->i * twiddles[2 * u * fstride].i;
        scratch[2].i = Fout2->r * twiddles[2 * u * fstride].i + Fout2->i * twiddles[2 * u * fstride].r;
        scratch[3].r = Fout3->r * twiddles[3 * u * fstride].r - Fout3->i * twiddles[3 * u * fstride].i;
        scratch[3].i = Fout3->r * twiddles[3 * u * fstride].i + Fout3->i * twiddles[3 * u * fstride].r;
        scratch[4].r = Fout4->r * twiddles[4 * u * fstride].r - Fout4->i * twiddles[4 * u * fstride].i;
        scratch[4].i = Fout4->r * twiddles[4 * u * fstride].i + Fout4->i * twiddles[4 * u * fstride].r;

        scratch[7].r = scratch[1].r + scratch[4].r;
        scratch[7].i = scratch[1].i + scratch[4].i;
        scratch[8].r = scratch[1].r - scratch[4].r;
        scratch[8].i = scratch[1].i - scratch[4].i;
        scratch[9].r = scratch[2].r + scratch[3].r;
        scratch[9].i = scratch[2].i + scratch[3].i;
        scratch[10].r = scratch[2].r - scratch[3].r;
        scratch[10].i = scratch[2].i - scratch[3].i;

        Fout0->r += scratch[7].r + scratch[9].r;
        Fout0->i += scratch[7].i + scratch[9].i;

        scratch[5].r = scratch[0].r + ya * scratch[7].r + ya * scratch[9].r;
        scratch[5].i = scratch[0].i + ya * scratch[7].i + ya * scratch[9].i;

        scratch[6].r = yb * (scratch[8].r + scratch[10].r);
        scratch[6].i = yb * (scratch[8].i + scratch[10].i);

        scratch[11].r = scratch[5].r - scratch[6].i;
        scratch[11].i = scratch[5].i + scratch[6].r;
        Fout1->r = scratch[1].r + scratch[11].r - scratch[9].r;
        Fout1->i = scratch[1].i + scratch[11].i - scratch[9].i;

        scratch[12].r = scratch[5].r + scratch[6].i;
        scratch[12].i = scratch[5].i - scratch[6].r;
        Fout4->r = scratch[4].r + scratch[12].r - scratch[7].r;
        Fout4->i = scratch[4].i + scratch[12].i - scratch[7].i;

        scratch[5].r = scratch[0].r + ya * scratch[7].r + ya * scratch[9].r;
        scratch[5].i = scratch[0].i + ya * scratch[7].i + ya * scratch[9].i;
        scratch[6].r = yb * (scratch[8].r - scratch[10].r);
        scratch[6].i = yb * (scratch[8].i - scratch[10].i);

        scratch[11].r = scratch[5].r - scratch[6].i;
        scratch[11].i = scratch[5].i + scratch[6].r;
        Fout2->r = scratch[2].r + scratch[11].r - scratch[7].r;
        Fout2->i = scratch[2].i + scratch[11].i - scratch[7].i;

        scratch[12].r = scratch[5].r + scratch[6].i;
        scratch[12].i = scratch[5].i - scratch[6].r;
        Fout3->r = scratch[3].r + scratch[12].r - scratch[9].r;
        Fout3->i = scratch[3].i + scratch[12].i - scratch[9].i;
    }
}

static void kf_work(kiss_fft_cpx * Fout, const kiss_fft_cpx * f, const size_t fstride, int in_stride, int * factors, const kiss_fft_cfg st) {
    kiss_fft_cpx * Fout_beg = Fout;
    const int p = factors[0];
    const int m = factors[1];
    Fout = Fout_beg;

    if (m == 1) {
        for (int i = 0; i < p; ++i) {
            Fout[i] = f[i * fstride * in_stride];
        }
    } else {
        for (int i = 0; i < p; ++i) {
            kf_work(Fout, f, fstride * p, in_stride, factors + 2, st);
            Fout += m;
            f += fstride * in_stride;
        }
    }

    Fout = Fout_beg;
    switch (p) {
        case 2: kf_bfly2(Fout, fstride, st, m); break;
        case 3: kf_bfly3(Fout, fstride, st, m); break;
        case 4: kf_bfly4(Fout, fstride, st, m); break;
        case 5: kf_bfly5(Fout, fstride, st, m); break;
        default:
            for (int u = 0; u < p; ++u) {
                const kiss_fft_cpx * twiddles = st->twiddles;
                kiss_fft_cpx t;
                for (int q1 = 0; q1 < m; ++q1) {
                    int idx = u * m + q1;
                    t.r = Fout[q1].r;
                    t.i = Fout[q1].i;
                    for (int k = 1; k < p; ++k) {
                        const kiss_fft_cpx * tw = twiddles + k * fstride * idx;
                        float tr = Fout[q1 + k * m].r;
                        float ti = Fout[q1 + k * m].i;
                        float wr = tw->r;
                        float wi = tw->i;
                        t.r += tr * wr - ti * wi;
                        t.i += tr * wi + ti * wr;
                    }
                    Fout[q1 + u * m] = t;
                }
            }
            break;
    }
}

static void kf_factor(int n, int * facbuf) {
    int p = 4;
    int i = 0;
    while (n > 1 && p <= n) {
        while (n % p) {
            switch (p) {
                case 4: p = 2; break;
                case 2: p = 3; break;
                default: p += 2; break;
            }
            if (p * p > n) p = n;
        }
        n /= p;
        facbuf[i++] = p;
        facbuf[i++] = n;
    }
}

kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void * mem, size_t * lenmem) {
    kiss_fft_cfg st = nullptr;
    size_t memneeded = sizeof(kiss_fft_state) + sizeof(kiss_fft_cpx) * (size_t)(nfft - 1);
    if (lenmem == nullptr || mem == nullptr) {
        st = static_cast<kiss_fft_cfg>(KISS_FFT_MALLOC(memneeded));
    } else {
        if (*lenmem >= memneeded) {
            st = static_cast<kiss_fft_cfg>(mem);
        }
        *lenmem = memneeded;
    }
    if (!st) return nullptr;

    st->nfft = nfft;
    st->inverse = inverse_fft;

    for (int i = 0; i < nfft; ++i) {
        float phase = -2 * KISS_FFT_PI * i / nfft;
        if (st->inverse) phase = -phase;
        st->twiddles[i].r = KISS_FFT_COS(phase);
        st->twiddles[i].i = KISS_FFT_SIN(phase);
    }
    return st;
}

void kiss_fft_stride(kiss_fft_cfg cfg, const kiss_fft_cpx * fin, kiss_fft_cpx * fout, int fin_stride) {
    if (!cfg) return;
    int factors[64];
    kf_factor(cfg->nfft, factors);
    kf_work(fout, fin, 1, fin_stride, factors, cfg);
    if (cfg->inverse) {
        float scale = 1.0f / cfg->nfft;
        for (int i = 0; i < cfg->nfft; ++i) {
            fout[i].r *= scale;
            fout[i].i *= scale;
        }
    }
}

void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx * fin, kiss_fft_cpx * fout) {
    kiss_fft_stride(cfg, fin, fout, 1);
}

void kiss_fft_free(void * mem) {
    if (mem) {
        KISS_FFT_FREE(mem);
    }
}
