#ifndef NFL_GMP_HPP
#define NFL_GMP_HPP

#include <vector>
#include "nfl/poly.hpp"

namespace nfl {

template <class T, size_t Degree, size_t NbModuli>
typename poly<T, Degree, NbModuli>::GMP poly<T, Degree, NbModuli>::gmp;

/*
 * Constructors
 */
template <class T, size_t Degree, size_t NbModuli>
poly<T, Degree, NbModuli>::poly(mpz_t const& v) {
  set_mpz(v);
}

template <class T, size_t Degree, size_t NbModuli>
poly<T, Degree, NbModuli>::poly(mpz_class const& v) {
  set_mpz({v});
}

template <class T, size_t Degree, size_t NbModuli>
poly<T, Degree, NbModuli>::poly(std::initializer_list<mpz_class> const& values) {
  set_mpz(values);
}

template <class T, size_t Degree, size_t NbModuli>
poly<T, Degree, NbModuli>::poly(std::array<mpz_t, Degree> const& values) {
  set_mpz(values);
}

template <class T, size_t Degree, size_t NbModuli>
poly<T, Degree, NbModuli>::poly(std::array<mpz_class, Degree> const& values) {
  set_mpz(values);
}

/*
 * set_mpz's functions
 */

template <class T, size_t Degree, size_t NbModuli>
void poly<T, Degree, NbModuli>::set_mpz(mpz_t const& v) {
  set_mpz({mpz_class(v)});
}

template <class T, size_t Degree, size_t NbModuli>
void poly<T, Degree, NbModuli>::set_mpz(mpz_class const& v) {
  set_mpz({v});
}

template <class T, size_t Degree, size_t NbModuli>
void poly<T, Degree, NbModuli>::set_mpz(std::array<mpz_t, Degree> const &values) {
  set_mpz(values.begin(), values.end());
}

template <class T, size_t Degree, size_t NbModuli>
void poly<T, Degree, NbModuli>::set_mpz(std::array<mpz_class, Degree> const& values) {
  set_mpz(values.begin(), values.end());
}

template <class T, size_t Degree, size_t NbModuli>
void poly<T, Degree, NbModuli>::set_mpz(
    std::initializer_list<mpz_class> const& values) {
  set_mpz(values.begin(), values.end());
}

template <class T, size_t Degree, size_t NbModuli>
template <class It>
void poly<T, Degree, NbModuli>::set_mpz(It first, It last) {
  // CRITICAL: the object must be 32-bytes aligned to avoid vectorization issues
  assert((unsigned long)(this->_data) % 32 == 0);

  // The initializer needs to have either less values than the polynomial degree
  // (and the remaining coefficients are set to 0), or be fully defined (i.e.
  // the degree*nmoduli coefficients needs to be provided)
  size_t size = std::distance(first, last);
  if (size > degree && size != degree * nmoduli) {
    throw std::runtime_error(
        "gmp: CRITICAL, initializer of size above degree but not equal "
        "to nmoduli*degree");
  }

  auto* iter = begin();
  auto viter = first;

  for (size_t cm = 0; cm < nmoduli; cm++) {
    auto const p = get_modulus(cm);

    if (size != degree * nmoduli) viter = first;

    // set the coefficients
    size_t i = 0;
    for (; i < degree && viter < last; ++i, ++viter, ++iter) {
      *iter = mpz_fdiv_ui(viter->get_mpz_t(), p);
    }

    // pad with zeroes if needed
    for (; i < degree; ++i, ++iter) {
      *iter = 0;
    }
  }
}

/*
 * Nested GMP class
 */
template <class T, size_t Degree, size_t NbModuli>
poly<T, Degree, NbModuli>::GMP::GMP() {
  // Compute product of moduli
  mpz_init_set_ui(moduli_product, 1);
  for (size_t i = 0; i < nmoduli; i++) {
    mpz_mul_ui(moduli_product, moduli_product, params<T>::P[i]);
  }

  bits_in_moduli_product = mpz_sizeinbase(moduli_product, 2);

  // Compute Shoup value for optimized reduction modulo "moduli_product"
  auto const shift_modulus_shoup = bits_in_moduli_product +
                                   sizeof(T) * CHAR_BIT +
                                   static_log2<nmoduli>::value + 1;
  mpz_t one;
  mpz_init_set_ui(one, 1);
  mpz_init(modulus_shoup);
  mpz_mul_2exp(modulus_shoup, one, shift_modulus_shoup);
  mpz_tdiv_q(modulus_shoup, modulus_shoup, moduli_product);

  // Compute the lifting coefficients
  mpz_t quotient;
  mpz_init(quotient);
  
  for (size_t i = 0; i < nmoduli; i++) {
    // Current modulus
    mpz_t current_modulus;
    mpz_init_set_ui(current_modulus, params<T>::P[i]);

    // compute the product of primes except the current one
    mpz_divexact(quotient, moduli_product, current_modulus);

    // Compute the inverse of the product
    mpz_init(lifting_integers[i]);
    mpz_invert(lifting_integers[i], quotient, current_modulus);

    // Multiply by the quotient
    mpz_mul(lifting_integers[i], lifting_integers[i], quotient);
    mpz_clear(current_modulus);
  }

  // Clear
  mpz_clear(one);
  mpz_clear(quotient);
}

template <class T, size_t Degree, size_t NbModuli>
poly<T, Degree, NbModuli>::GMP::~GMP() {
  for (size_t i = 0; i < nmoduli; i++) {
    mpz_clear(lifting_integers[i]);
  }
  mpz_clear(modulus_shoup);
  mpz_clear(moduli_product);
}

/**
 * Functions to convert a poly into an array of mpz_t, and back
 */

template <class T, size_t Degree, size_t NbModuli>
std::array<mpz_t, Degree> poly<T, Degree, NbModuli>::GMP::poly2mpz(poly const& op) {
  // Assign and init
  std::array<mpz_t, Degree> poly_mpz;
  for (size_t i = 0; i < degree; i++) {
    mpz_init2(poly_mpz[i], bits_in_moduli_product);
  }

  // Fill
  poly2mpz(poly_mpz, op);
  return poly_mpz;
}

template <class T, size_t Degree, size_t NbModuli>
void poly<T, Degree, NbModuli>::GMP::poly2mpz(std::array<mpz_t, Degree>& rop, poly const& op) {

  auto const shift_modulus_shoup = bits_in_moduli_product +
                                   sizeof(T) * CHAR_BIT +
                                   static_log2<nmoduli>::value + 1;

  mpz_t tmp;
  mpz_init2(tmp, shift_modulus_shoup + static_log2<nmoduli>::value);

  // Loop on each coefficient
  for (size_t i = 0; i < degree; i++) {
    mpz_set_ui(rop[i], 0);
    for (size_t cm = 0; cm < nmoduli; cm++) {
      if (op(cm, i) != 0) {
        mpz_mul_ui(tmp, lifting_integers[cm], op(cm, i));
      }
      mpz_add(rop[i], rop[i], tmp);
    }
    // Modular reduction using Shoup
    mpz_mul(tmp, rop[i], modulus_shoup);
    mpz_tdiv_q_2exp(tmp, tmp, shift_modulus_shoup);
    mpz_mul(tmp, tmp, moduli_product);
    mpz_sub(rop[i], rop[i], tmp);
    if (mpz_cmp(rop[i], moduli_product) >= 0) {
      mpz_sub(rop[i], rop[i], moduli_product);
    }
  }

  // Clean
  mpz_clear(tmp);
}

template <class T, size_t Degree, size_t NbModuli>
void poly<T, Degree, NbModuli>::GMP::mpz2poly(poly<T, Degree, NbModuli>& rop,
                                              std::array<mpz_t, Degree> const& poly_mpz) {
  for (size_t cm = 0; cm < nmoduli; cm++) {
    for (size_t i = 0; i < degree; i++) {
      rop(cm, i) = mpz_fdiv_ui(poly_mpz[i], get_modulus(cm));
    }
  }
}
}

#endif