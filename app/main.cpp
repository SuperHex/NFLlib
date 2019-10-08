#include <nfl.hpp>
#include <iostream>
#include <tuple>
#include <array>
#include <gmpxx.h>

// cipher text ring, have two moduli, i.e. 15361, 13313
using T = uint64_t;

template <typename _T>
struct Para;

template <>
struct Para<uint16_t> {
    constexpr static auto p = 1;
    constexpr static auto q = 2;
    constexpr static auto degree = 16;
};

template <>
struct Para<uint32_t> {
    constexpr static auto p = 10;
    constexpr static auto q = 11;
    constexpr static auto degree = 128;
};

template <>
struct Para<uint64_t> {
    constexpr static auto p = 8;
    constexpr static auto q = 14;
    constexpr static auto degree = 32;
};

template <typename _T>
struct pair {
    _T x;
    _T y;
};

struct C {
    using poly = nfl::poly<T, Para<T>::degree, Para<T>::q>;
    using poly_ptr = nfl::poly_p<T, Para<T>::degree, Para<T>::q>;
};

struct P {
    using poly = nfl::poly<T, Para<T>::degree, Para<T>::p>;
    using poly_ptr = nfl::poly_p<T, Para<T>::degree, Para<T>::p>;
};


static nfl::FastGaussianNoise<uint16_t, T, 2> fg(8, 80, Para<T>::degree);
static nfl::gaussian<uint16_t, T, 2> chi(&fg);

// plain text ring, only have one modulus

using Cipher = std::pair<C::poly, C::poly>;
using Keys = std::tuple<C::poly, C::poly, C::poly>;

using Chi = nfl::gaussian<uint16_t, T, 2>;

template <class T>
std::pair<T, T> operator+(const std::pair<T, T>& lhs, const std::pair<T, T>& rhs) {
    return {lhs.first + rhs.first, lhs.second + rhs.second};
}

template <class T>
std::pair<T, T> operator*(const std::pair<T, T>& lhs, const T& rhs) {
    return {lhs.first * rhs, lhs.second * rhs};
}

void roundNearest(mpz_t out, mpz_t in, mpz_t div) {
    mpz_t q, r;
    mpz_init(q);
    mpz_init(r);
    mpz_set_ui(q, 0);
    mpz_set_ui(r, 0);

    mpz_tdiv_qr(q, r, in, div);
    mpz_mul_ui(r, r, 2);
    if (mpz_cmp(r, div) >= 0) {
        mpz_add_ui(q, q, 1);
    }
    mpz_mul(out, q, div);
}

void calcQOverP(mpz_t qp, const C::poly& t) {
    mpz_init(qp);
    mpz_set_ui(qp, 1);
    for (auto i = Para<T>::p; i < Para<T>::q; i++) {
        mpz_mul_ui(qp, qp, t.get_modulus(i));
    }
}

Keys genKeys() {

    // first uniformly choose a
    C::poly a(nfl::uniform{});

    // gaussian dist
    C::poly s(chi);
    C::poly e(chi);

    // keep everything in FFT domain
    a.ntt_pow_phi();
    s.ntt_pow_phi();
    e.ntt_pow_phi();

    C::poly b = s * a + e;
    // return (public Cipher, private Cipher)
    return {a, b, s};
}

std::vector<C::poly> genKeys(size_t n) {
    std::vector<C::poly> vec(n + 2);
    C::poly a;
    a.ntt_pow_phi();
    for (auto i = 0; i < n; i++) {
        auto u = C::poly(nfl::uniform{});
        u.ntt_pow_phi();
        a = a + u;
    }

    C::poly b;
    b.ntt_pow_phi();
    for (auto i = 0; i < n; i++) {
        C::poly si(chi);
        C::poly ei(chi);
        si.ntt_pow_phi();
        ei.ntt_pow_phi();
        vec[i + 2] = si;

        C::poly bi = si * a + ei;
        b = b + bi;
    }

    vec[0] = a;
    vec[1] = b;
    return vec;
}

// assume message is NOT in FFT domain
Cipher encrypt(Cipher k, P::poly message) {
    C::poly u(chi), v(chi), w(chi);
    //C::poly u{}, v{}, w{};  // no noise now

    // keep everything in FFT domain
    u.ntt_pow_phi();
    v.ntt_pow_phi();
    w.ntt_pow_phi();

    // transform Q/P to polynomial
    mpz_t q_over_p;
    calcQOverP(q_over_p, std::get<0>(k));
    auto qdivp = C::poly(q_over_p);
    qdivp.ntt_pow_phi();
    // transform message from mod P to mod Q
    C::poly message_q;
    message_q.mpz2poly(message.poly2mpz());
    message_q.ntt_pow_phi();

    C::poly fst = C::poly_ptr(u) * std::get<0>(k) + C::poly_ptr(v);
    C::poly sec = C::poly_ptr(u) * std::get<1>(k) + C::poly_ptr(w) + qdivp * message_q;

    return {fst, sec};
}

C::poly decrypt(Cipher cipher, C::poly s) {
    C::poly dec = std::get<1>(cipher) - std::get<0>(cipher) * s;
    return dec;
}

std::array<mpz_class, Para<T>::degree> genZ(const mpz_class& tau, const mpz_class& N, size_t lambda) {
    mpz_class bound = tau * N * (mpz_class(1) << lambda);
    mpz_class bound2 = bound * 2;
    std::array<mpz_class, Para<T>::degree> arr;
    gmp_randstate_t state;
    gmp_randinit_mt(state);
    mpz_class tmp(0);
    for (auto it = arr.begin(); it != arr.end(); it++) {
        mpz_urandomm(tmp.get_mpz_t(), state, bound2.get_mpz_t());
        tmp = tmp - bound;
        *it = tmp;
    }
    return arr;
}

mpz_class genZ1(const mpz_class& tau, const mpz_class& N, size_t lambda) {
    mpz_class bound = tau * N * (mpz_class(1) << lambda);
    mpz_class bound2 = bound * 2;
    gmp_randstate_t state;
    gmp_randinit_mt(state);
    mpz_class tmp(0);
    mpz_urandomm(tmp.get_mpz_t(), state, bound2.get_mpz_t());
    tmp = tmp - bound;
    return tmp;
}


int main() {

    constexpr auto N = 2;
    mpz_class tau = mpz_class(nfl::params<T>::P[Para<T>::p]) * mpz_class(nfl::params<T>::P[Para<T>::p + 1]);  // 120 bits
    mpz_class security = mpz_class(1) << 80;

    // protocol for 2 parties
    auto keys = genKeys(2);
    auto a = keys[0];
    auto b = keys[1];
    auto s0 = keys[2];
    auto s1 = keys[3];

    P::poly x0{1,2,3}, x1{0,2,3,4};  // {1, 4, 6, 4}
    C::poly y0{1}, y1{2};  // {3}

    x0.invntt_pow_invphi(); x1.invntt_pow_invphi();
    y0.ntt_pow_phi(); y1.ntt_pow_phi();

    auto ex0 = encrypt({a, b}, x0);
    auto ex1 = encrypt({a, b}, x1);

    auto ex = ex0 + ex1;
    auto eex = ex * y0;
    auto eeex = ex * y1;

/*     P::poly zn0(genZ1(tau, N, 80));
    zn0.ntt_pow_phi();
    P::poly zn1(genZ1(tau, N, 80));
    zn1.ntt_pow_phi();
    P::poly poly_tau(tau);
    poly_tau.ntt_pow_phi();
    zn0 = zn0 * poly_tau;
    zn1 = zn1 * poly_tau;

    auto ezn0 = encrypt({a, b}, zn0);
    auto ezn1 = encrypt({a, b}, zn1);

    eex = eex + ezn0;
    eeex = eeex + ezn1; */

    auto c = eex.first + eeex.first;

    auto d0 = decrypt({c, eex.second}, s0);
    auto d1 = decrypt({c, eeex.second}, s1);

    C::poly d = d0 + d1;

    // get coefficient
    d.invntt_pow_invphi();
    std::array<mpz_t, Para<T>::degree> tt = d.poly2mpz();  // convert poly to an array

    // get Q/P
    mpz_t mm;
    calcQOverP(mm, d);

    for (auto it = tt.begin(); it != tt.end(); it++) {
        roundNearest(*it, *it, mm);
    }

    P::poly dd;
    dd.mpz2poly(tt);

    // finally evaluate the polynomial
    dd.ntt_pow_phi();
    tt = dd.poly2mpz();
    
    // and the result is incorrect for now
    for (auto it = tt.begin(); it != tt.begin() + 8; it++) {
        mpz_tdiv_q(*it, *it, mm);
        //mpz_tdiv_r(*it, *it, tau.get_mpz_t());
        std::cout << *it << std::endl;
    }
    return 1;
}

/* int main() {
    auto [a, b, s] = genKeys();

    P::poly test{1,2,3};
    P::poly foo {2, 3, 4};
    test.invntt_pow_invphi();
    foo.invntt_pow_invphi();

    C::poly konst{5};
    konst.ntt_pow_phi();

    auto e = encrypt({a, b}, test);
    auto e1 = encrypt({a, b}, foo);
    auto e2 = e + e1;
    auto e3 = e2 * konst;
    auto e4 = e3 + e3;
    auto d = decrypt(e4, s);

    d.invntt_pow_invphi();
    std::array<mpz_t, Para<T>::degree> tt = d.poly2mpz();  // convert poly to an array

    // get Q/P
    mpz_t mm;
    calcQOverP(mm, d);

    for (auto it = tt.begin(); it != tt.end(); it++) {
        roundNearest(*it, *it, mm);
    }

    P::poly dd;
    dd.mpz2poly(tt);

    // finally evaluate the polynomial
    dd.ntt_pow_phi();
    tt = dd.poly2mpz();
    
    // and the result is incorrect for now
    for (auto it = tt.begin(); it != tt.begin() + 8; it++) {
        mpz_tdiv_q(*it, *it, mm);
        std::cout << *it << std::endl;
    }
} */