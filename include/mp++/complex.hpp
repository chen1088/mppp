// Copyright 2016-2020 Francesco Biscani (bluescarni@gmail.com)
//
// This file is part of the mp++ library.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef MPPP_COMPLEX_HPP
#define MPPP_COMPLEX_HPP

#include <mp++/config.hpp>

#if defined(MPPP_WITH_MPC)

#include <cassert>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#if defined(MPPP_HAVE_STRING_VIEW)
#include <string_view>
#endif

#include <mp++/concepts.hpp>
#include <mp++/detail/fwd_decl.hpp>
#include <mp++/detail/mpc.hpp>
#include <mp++/detail/mpfr.hpp>
#include <mp++/detail/type_traits.hpp>
#include <mp++/detail/utils.hpp>
#include <mp++/detail/visibility.hpp>
#include <mp++/integer.hpp>
#include <mp++/rational.hpp>
#include <mp++/real.hpp>
#include <mp++/type_name.hpp>

#if defined(MPPP_WITH_QUADMATH)

#include <mp++/complex128.hpp>
#include <mp++/real128.hpp>

#endif

namespace mppp
{

// Detect real-valued interoperable types
// for complex.
template <typename T>
using is_rv_complex_interoperable
    = detail::disjunction<is_cpp_arithmetic<detail::uncvref_t<T>>, detail::is_integer<detail::uncvref_t<T>>,
                          detail::is_rational<detail::uncvref_t<T>>, std::is_same<detail::uncvref_t<T>, real>
#if defined(MPPP_WITH_QUADMATH)
                          ,
                          std::is_same<detail::uncvref_t<T>, real128>
#endif
                          >;

namespace detail
{

// Detect complex-valued interoperable types.
// For internal use only.
template <typename T>
using is_cv_complex_interoperable = detail::disjunction<is_cpp_complex<detail::uncvref_t<T>>
#if defined(MPPP_WITH_QUADMATH)
                                                        ,
                                                        std::is_same<detail::uncvref_t<T>, complex128>
#endif
                                                        >;

} // namespace detail

// Detect interoperable types
// for complex.
template <typename T>
using is_complex_interoperable
    = detail::disjunction<is_rv_complex_interoperable<T>, detail::is_cv_complex_interoperable<T>>;

template <typename T>
using is_complex_convertible = detail::conjunction<is_complex_interoperable<T>, std::is_same<T, detail::uncvref_t<T>>>;

#if defined(MPPP_HAVE_CONCEPTS)

template <typename T>
MPPP_CONCEPT_DECL rv_complex_interoperable = is_rv_complex_interoperable<T>::value;

template <typename T>
MPPP_CONCEPT_DECL complex_interoperable = is_complex_interoperable<T>::value;

template <typename T>
MPPP_CONCEPT_DECL complex_convertible = is_complex_convertible<T>::value;

#endif

template <typename T>
using is_cvr_complex = std::is_same<detail::uncvref_t<T>, complex>;

#if defined(MPPP_HAVE_CONCEPTS)

template <typename T>
MPPP_CONCEPT_DECL cvr_complex = is_cvr_complex<T>::value;

#endif

template <typename... Args>
using cvr_complex_enabler = detail::enable_if_t<detail::conjunction<is_cvr_complex<Args>...>::value, int>;

// Fwd declare swap.
void swap(complex &, complex &) noexcept;

namespace detail
{

// Fwd declare for friendship.
template <bool, typename F, typename Arg0, typename... Args>
complex &mpc_nary_op_impl(::mpfr_prec_t, const F &, complex &, Arg0 &&, Args &&...);

template <bool, typename F, typename Arg0, typename... Args>
complex mpc_nary_op_return_impl(::mpfr_prec_t, const F &, Arg0 &&, Args &&...);

// Precision deducer for real.
inline ::mpfr_prec_t real_deduce_precision(const real &r)
{
    return r.get_prec();
}

} // namespace detail

// Strongly typed enum alias for mpfr_prec_t.
enum class complex_prec_t : ::mpfr_prec_t {};

// For the future:
// - in some sub implementations, we are using the pattern of implementing b-a
//   as -(a-b). Probably eliminating the negation is more efficient,
//   but on the other hand it requires more complex implementations.
//   Need to verify if this is worth the hassle.

// Multiprecision complex class.
class MPPP_DLL_PUBLIC complex
{
    // Make friends, for accessing the non-checking prec setting funcs.
    template <bool, typename F, typename Arg0, typename... Args>
    friend complex &detail::mpc_nary_op_impl(::mpfr_prec_t, const F &, complex &, Arg0 &&, Args &&...);
    template <bool, typename F, typename Arg0, typename... Args>
    friend complex detail::mpc_nary_op_return_impl(::mpfr_prec_t, const F &, Arg0 &&, Args &&...);

    // Utility function to check the precision upon init.
    static ::mpfr_prec_t check_init_prec(::mpfr_prec_t p)
    {
        if (mppp_unlikely(!detail::real_prec_check(p))) {
            throw std::invalid_argument("Cannot init a complex with a precision of " + detail::to_string(p)
                                        + ": the maximum allowed precision is " + detail::to_string(real_prec_max())
                                        + ", the minimum allowed precision is " + detail::to_string(real_prec_min()));
        }
        return p;
    }

public:
    // Default ctor.
    complex();

private:
    // A tag to call private ctors.
    struct ptag {
    };
    // Private ctor that sets to NaN with a certain precision,
    // without checking the input precision value.
    explicit complex(const ptag &, ::mpfr_prec_t, bool);

public:
    // Copy ctor.
    complex(const complex &);
    // Move constructor.
    complex(complex &&other) noexcept
    {
        // Shallow copy other.
        m_mpc = other.m_mpc;
        // Mark the other as moved-from.
        other.m_mpc.re->_mpfr_d = nullptr;
    }

    // Copy constructor with custom precision.
    explicit complex(const complex &, complex_prec_t);
    // Move constructor with custom precision.
    explicit complex(complex &&, complex_prec_t);

private:
    // A tag for private generic ctors.
    struct gtag {
    };
    // From real-valued interoperable types + optional precision.
    template <typename T, typename... Args>
    explicit complex(gtag, std::true_type, T &&x, const Args &... args)
    {
        // Init the real part from x + optional explicit precision.
        real re{std::forward<T>(x), static_cast<::mpfr_prec_t>(args)...};
        // The imaginary part is inited to +0, using either the explicit
        // precision (if provided) or the precision of the real part
        // otherwise.
        auto im = sizeof...(Args) ? real{real_kind::zero, 1, static_cast<::mpfr_prec_t>(args)...}
                                  : real{real_kind::zero, 1, re.get_prec()};

        // Shallow-copy into this.
        m_mpc.re[0] = *re.get_mpfr_t();
        m_mpc.im[0] = *im.get_mpfr_t();

        // Deactivate the temporaries.
        re._get_mpfr_t()->_mpfr_d = nullptr;
        im._get_mpfr_t()->_mpfr_d = nullptr;
    }
    // From complex-valued interoperable types + optional precision.
    // NOTE: this will delegate to the ctors from real + imaginary parts.
    // NOTE: no need for std::forward, as this constructor will involve
    // only std::complex or complex128.
    template <typename T, typename... Args>
    explicit complex(gtag, std::false_type, const T &c, const Args &... args) : complex(c.real(), c.imag(), args...)
    {
    }

public:
    // Ctor from interoperable types.
#if defined(MPPP_HAVE_CONCEPTS)
    template <complex_interoperable T>
#else
    template <typename T, detail::enable_if_t<is_complex_interoperable<T>::value, int> = 0>
#endif
    explicit complex(T &&x) : complex(gtag{}, is_rv_complex_interoperable<T>{}, std::forward<T>(x))
    {
    }
    // Ctor from interoperable types + precision.
#if defined(MPPP_HAVE_CONCEPTS)
    template <complex_interoperable T>
#else
    template <typename T, detail::enable_if_t<is_complex_interoperable<T>::value, int> = 0>
#endif
    explicit complex(T &&x, complex_prec_t p) : complex(gtag{}, is_rv_complex_interoperable<T>{}, std::forward<T>(x), p)
    {
    }

private:
    // Implementation of the ctor from real and imaginary
    // parts + explicitly specified precision.
    template <typename T, typename U>
    void real_imag_ctor_impl(T &&re, U &&im, ::mpfr_prec_t p)
    {
        // Init real-imaginary parts with the input prec.
        real rp{std::forward<T>(re), p}, ip{std::forward<U>(im), p};

        // Shallow-copy into this.
        m_mpc.re[0] = *rp.get_mpfr_t();
        m_mpc.im[0] = *ip.get_mpfr_t();

        // Deactivate the temporaries.
        rp._get_mpfr_t()->_mpfr_d = nullptr;
        ip._get_mpfr_t()->_mpfr_d = nullptr;
    }

public:
    // Binary ctor from real-valued interoperable types.
#if defined(MPPP_HAVE_CONCEPTS)
    template <rv_complex_interoperable T, rv_complex_interoperable U>
#else
    template <typename T, typename U,
              detail::enable_if_t<
                  detail::conjunction<is_rv_complex_interoperable<T>, is_rv_complex_interoperable<U>>::value, int> = 0>
#endif
    explicit complex(T &&re, U &&im)
    {
        // NOTE: the precision will be the largest between the
        // automatically-deduced ones for re and im.
        real_imag_ctor_impl(std::forward<T>(re), std::forward<U>(im),
                            detail::c_max(detail::real_deduce_precision(re), detail::real_deduce_precision(im)));
    }
    // Binary ctor from real-valued interoperable types + prec.
#if defined(MPPP_HAVE_CONCEPTS)
    template <rv_complex_interoperable T, rv_complex_interoperable U>
#else
    template <typename T, typename U,
              detail::enable_if_t<
                  detail::conjunction<is_rv_complex_interoperable<T>, is_rv_complex_interoperable<U>>::value, int> = 0>
#endif
    explicit complex(T &&re, U &&im, complex_prec_t p)
    {
        real_imag_ctor_impl(std::forward<T>(re), std::forward<U>(im), static_cast<::mpfr_prec_t>(p));
    }

private:
    // A tag for private string ctors.
    struct stag {
    };
    MPPP_DLL_LOCAL void construct_from_c_string(const char *, int, ::mpfr_prec_t);
    explicit complex(const stag &, const char *, int, ::mpfr_prec_t);
    explicit complex(const stag &, const std::string &, int, ::mpfr_prec_t);
#if defined(MPPP_HAVE_STRING_VIEW)
    explicit complex(const stag &, const std::string_view &, int, ::mpfr_prec_t);
#endif

public:
    // Constructor from string, base and precision.
#if defined(MPPP_HAVE_CONCEPTS)
    template <string_type T>
#else
    template <typename T, detail::enable_if_t<is_string_type<T>::value, int> = 0>
#endif
    explicit complex(const T &s, int base, complex_prec_t p) : complex(stag{}, s, base, static_cast<::mpfr_prec_t>(p))
    {
    }
    // Constructor from string and precision.
#if defined(MPPP_HAVE_CONCEPTS)
    template <string_type T>
#else
    template <typename T, detail::enable_if_t<is_string_type<T>::value, int> = 0>
#endif
    explicit complex(const T &s, complex_prec_t p) : complex(s, 10, p)
    {
    }
    // Constructor from range of characters, base and precision.
    explicit complex(const char *, const char *, int, complex_prec_t);
    // Constructor from range of characters and precision.
    explicit complex(const char *, const char *, complex_prec_t);

    // Constructor from mpc_t.
    explicit complex(const ::mpc_t);
#if !defined(_MSC_VER) || defined(__clang__)
    // Move constructor from mpc_t.
    explicit complex(::mpc_t &&c) : m_mpc(*c) {}
#endif

    ~complex();

    // Copy assignment operator.
    complex &operator=(const complex &);
    // Move assignment operator.
    complex &operator=(complex &&other) noexcept
    {
        // NOTE: for generic code, std::swap() is not a particularly good way of implementing
        // the move assignment:
        //
        // https://stackoverflow.com/questions/6687388/why-do-some-people-use-swap-for-move-assignments
        //
        // Here however it is fine, as we know there are no side effects we need to maintain.
        //
        // NOTE: we use a raw std::swap() here (instead of mpc_swap()) because we don't know in principle
        // if mpc_swap() relies on the operands not to be in a moved-from state (although it's unlikely).
        std::swap(m_mpc, other.m_mpc);
        return *this;
    }

private:
    // Assignment from real-valued interoperable types.
    template <typename T>
    void dispatch_generic_assignment(T &&x, std::true_type)
    {
        re_ref re{*this};
        im_ref im{*this};

        // Assign the real part.
        *re = std::forward<T>(x);
        // Set the imaginary part to zero with
        // the same precision as re.
        im->set_prec(re->get_prec());
        im->set_zero();
    }
    // Assignment from complex-valued interoperable types.
    template <typename T>
    void dispatch_generic_assignment(const T &c, std::false_type)
    {
        // Determine the max prec between the real
        // and imaginary parts.
        // NOTE: this is not really necessary, as for std::complex
        // and complex128 (the only two other complex types) the
        // prec deduction does not depend on the value. However,
        // this is what we *would* do if we had a complex type
        // in which the two parts can have different precisions.
        const auto p = detail::c_max(detail::real_deduce_precision(c.real()), detail::real_deduce_precision(c.imag()));

        re_ref re{*this};
        im_ref im{*this};

        // Destructively set the precision to p
        // for both re and im.
        re->set_prec(p);
        im->set_prec(p);

        // Assign the values from c with the setter
        // (and not with the assignment operator, which
        // may alter the precision of re and im).
        re->set(c.real());
        im->set(c.imag());
    }

public:
#if defined(MPPP_HAVE_CONCEPTS)
    template <complex_interoperable T>
#else
    template <typename T, detail::enable_if_t<is_complex_interoperable<T>::value, int> = 0>
#endif
    complex &operator=(T &&x)
    {
        dispatch_generic_assignment(std::forward<T>(x), is_rv_complex_interoperable<T>{});
        return *this;
    }

    // Copy assignment from mpc_t.
    complex &operator=(const ::mpc_t);

#if !defined(_MSC_VER) || defined(__clang__)
    // Move assignment from mpc_t.
    complex &operator=(::mpc_t &&);
#endif

    // Check validity.
    bool is_valid() const noexcept
    {
        return mpc_realref(&m_mpc)->_mpfr_d != nullptr;
    }

    // Set to another complex.
    complex &set(const complex &);

private:
    // Implementation of the generic setter.
    template <typename T>
    void set_impl(const T &x, std::true_type)
    {
        re_ref re{*this};

        re->set(x);
        ::mpfr_set_zero(mpc_imagref(&m_mpc), 1);
    }
    template <typename T>
    void set_impl(const T &c, std::false_type)
    {
        re_ref re{*this};
        im_ref im{*this};

        re->set(c.real());
        im->set(c.imag());
    }

public:
    // Generic setter.
#if defined(MPPP_HAVE_CONCEPTS)
    template <complex_interoperable T>
#else
    template <typename T, detail::enable_if_t<is_complex_interoperable<T>::value, int> = 0>
#endif
    complex &set(const T &other)
    {
        set_impl(other, is_rv_complex_interoperable<T>{});
        return *this;
    }

private:
    // Implementation of string setters.
    MPPP_DLL_LOCAL void string_assignment_impl(const char *, int);
    complex &set_impl(const char *, int);
    complex &set_impl(const std::string &, int);
#if defined(MPPP_HAVE_STRING_VIEW)
    complex &set_impl(const std::string_view &, int);
#endif

public:
    // Setter to string.
#if defined(MPPP_HAVE_CONCEPTS)
    template <string_type T>
#else
    template <typename T, detail::enable_if_t<is_string_type<T>::value, int> = 0>
#endif
    complex &set(const T &s, int base = 10)
    {
        return set_impl(s, base);
    }
    // Set to character range.
    complex &set(const char *begin, const char *end, int base = 10);

    // Set to an mpc_t.
    complex &set(const ::mpc_t);

    class re_ref
    {
    public:
        explicit re_ref(complex &c) : m_c(c), m_value(real::shallow_copy_t{}, mpc_realref(&c.m_mpc)) {}

        re_ref(const re_ref &) = delete;
        re_ref(re_ref &&) = delete;
        re_ref &operator=(const re_ref &) = delete;
        re_ref &operator=(re_ref &&) = delete;

        ~re_ref()
        {
            mpc_realref(&m_c.m_mpc)[0] = *m_value.get_mpfr_t();
            m_value._get_mpfr_t()->_mpfr_d = nullptr;
        }

        real &operator*()
        {
            return m_value;
        }

        real *operator->()
        {
            return &m_value;
        }

    private:
        complex &m_c;
        real m_value;
    };

    class re_cref
    {
    public:
        explicit re_cref(const complex &c) : m_value(real::shallow_copy_t{}, mpc_realref(&c.m_mpc)) {}

        re_cref(const re_cref &) = delete;
        re_cref(re_cref &&) = delete;
        re_cref &operator=(const re_cref &) = delete;
        re_cref &operator=(re_cref &&) = delete;

        ~re_cref()
        {
            m_value._get_mpfr_t()->_mpfr_d = nullptr;
        }

        const real &operator*() const
        {
            return m_value;
        }

        const real *operator->() const
        {
            return &m_value;
        }

    private:
        real m_value;
    };

    class im_ref
    {
    public:
        explicit im_ref(complex &c) : m_c(c), m_value(real::shallow_copy_t{}, mpc_imagref(&c.m_mpc)) {}

        im_ref(const im_ref &) = delete;
        im_ref(im_ref &&) = delete;
        im_ref &operator=(const im_ref &) = delete;
        im_ref &operator=(im_ref &&) = delete;

        ~im_ref()
        {
            mpc_imagref(&m_c.m_mpc)[0] = *m_value.get_mpfr_t();
            m_value._get_mpfr_t()->_mpfr_d = nullptr;
        }

        real &operator*()
        {
            return m_value;
        }

        real *operator->()
        {
            return &m_value;
        }

    private:
        complex &m_c;
        real m_value;
    };

    class im_cref
    {
    public:
        explicit im_cref(const complex &c) : m_value(real::shallow_copy_t{}, mpc_imagref(&c.m_mpc)) {}

        im_cref(const im_cref &) = delete;
        im_cref(im_cref &&) = delete;
        im_cref &operator=(const im_cref &) = delete;
        im_cref &operator=(im_cref &&) = delete;

        ~im_cref()
        {
            m_value._get_mpfr_t()->_mpfr_d = nullptr;
        }

        const real &operator*() const
        {
            return m_value;
        }

        const real *operator->() const
        {
            return &m_value;
        }

    private:
        real m_value;
    };

#if MPPP_CPLUSPLUS >= 201703L
    // Helpers to construct re/im refs.
    // They require C++17.
    auto real_cref() const
    {
        return re_cref{*this};
    }
    auto imag_cref() const
    {
        return im_cref{*this};
    }
    auto real_ref()
    {
        return re_ref{*this};
    }
    auto imag_ref()
    {
        return im_ref{*this};
    }
#endif

    // Precision getter.
    ::mpfr_prec_t get_prec() const
    {
        assert(mpfr_get_prec(mpc_realref(&m_mpc)) == mpfr_get_prec(mpc_imagref(&m_mpc)));

        return mpfr_get_prec(mpc_realref(&m_mpc));
    }

private:
    // Utility function to check precision in set_prec().
    static ::mpfr_prec_t check_set_prec(::mpfr_prec_t p)
    {
        if (mppp_unlikely(!detail::real_prec_check(p))) {
            throw std::invalid_argument("Cannot set the precision of a complex to the value " + detail::to_string(p)
                                        + ": the maximum allowed precision is " + detail::to_string(real_prec_max())
                                        + ", the minimum allowed precision is " + detail::to_string(real_prec_min()));
        }
        return p;
    }
    // mpc_set_prec() wrapper, with or without prec checking.
    template <bool Check>
    void set_prec_impl(::mpfr_prec_t p)
    {
        ::mpc_set_prec(&m_mpc, Check ? check_set_prec(p) : p);
    }
    // Wrapper for setting the precision while preserving
    // the original value, with or without precision checking.
    template <bool Check>
    void prec_round_impl(::mpfr_prec_t p_)
    {
        const auto p = Check ? check_set_prec(p_) : p_;
        ::mpfr_prec_round(mpc_realref(&m_mpc), p, MPFR_RNDN);
        ::mpfr_prec_round(mpc_imagref(&m_mpc), p, MPFR_RNDN);
    }

public:
    // Precision setters.
    complex &set_prec(::mpfr_prec_t);
    complex &prec_round(::mpfr_prec_t);

    // mpc_t getters.
    const mpc_struct_t *get_mpc_t() const
    {
        return &m_mpc;
    }
    mpc_struct_t *_get_mpc_t()
    {
        return &m_mpc;
    }

    // Detect special values.
    bool zero_p() const
    {
        return mpfr_zero_p(mpc_realref(&m_mpc)) != 0 && mpfr_zero_p(mpc_imagref(&m_mpc)) != 0;
    }
    bool is_one() const;

private:
    // Implementation of the conversion operator.
    template <typename T>
    T dispatch_conversion(std::true_type) const
    {
        if (std::is_same<T, bool>::value) {
            return static_cast<T>(!zero_p());
        } else {
            if (mppp_unlikely(!mpfr_zero_p(mpc_imagref(&m_mpc)))) {
                throw std::domain_error("Cannot convert the complex value " + to_string() + " to the real-valued type '"
                                        + type_name<T>() + "': the imaginary part is not zero");
            }

            re_cref re{*this};

            return static_cast<T>(*re);
        }
    }
    template <typename T>
    T dispatch_conversion(std::false_type) const
    {
        using value_type = typename T::value_type;

        re_cref re{*this};
        im_cref im{*this};

        return T{static_cast<value_type>(*re), static_cast<value_type>(*im)};
    }

public:
#if defined(MPPP_HAVE_CONCEPTS)
    template <complex_convertible T>
#else
    template <typename T, detail::enable_if_t<is_complex_convertible<T>::value, int> = 0>
#endif
    explicit operator T() const
    {
        return dispatch_conversion<T>(is_rv_complex_interoperable<T>{});
    }

private:
    // Implementation of get().
    // Real-valued types, except mppp::real.
    template <typename T>
    bool dispatch_get(T &rop, std::integral_constant<int, 1>) const
    {
        if (std::is_same<T, bool>::value) {
            rop = static_cast<T>(!zero_p());
            return true;
        } else {
            if (!mpfr_zero_p(mpc_imagref(&m_mpc))) {
                return false;
            }

            re_cref re{*this};

            return re->get(rop);
        }
    }
    // Special case if T is mppp::real.
    template <typename T>
    bool dispatch_get(T &rop, std::integral_constant<int, 3>) const
    {
        if (!mpfr_zero_p(mpc_imagref(&m_mpc))) {
            return false;
        }

        re_cref re{*this};

        rop = *re;

        return true;
    }
    // Complex-valued types.
    template <typename T>
    bool dispatch_get(T &rop, std::integral_constant<int, 0>) const
    {
        using value_type = typename T::value_type;

        re_cref re{*this};
        im_cref im{*this};

        // NOTE: currently here T is always either
        // std::complex or complex128, and thus the conversion
        // of the real/imag parts of this to the value type
        // of T can never fail. We will have to change this
        // in the future if we introduce other complex types
        // with different properties.
        rop = T{static_cast<value_type>(*re), static_cast<value_type>(*im)};

        return true;
    }

public:
#if defined(MPPP_HAVE_CONCEPTS)
    template <complex_convertible T>
#else
    template <typename T, detail::enable_if_t<is_complex_convertible<T>::value, int> = 0>
#endif
    bool get(T &rop) const
    {
        return dispatch_get(
            rop,
            std::integral_constant<int, is_rv_complex_interoperable<T>::value + std::is_same<T, real>::value * 2>{});
    }

    std::string to_string(int base = 10) const;

private:
    template <typename T>
    MPPP_DLL_LOCAL complex &self_mpc_unary(T &&);

public:
    // In-place arithmetic functions.
    complex &neg();
    complex &conj();
    complex &abs();
    complex &norm();
    complex &arg();
    complex &proj();
    complex &sqr();
    complex &mul_i(int sgn = 0);

private:
    mpc_struct_t m_mpc;
};

template <typename T, typename U>
using are_complex_op_types
    = detail::disjunction<detail::conjunction<is_cvr_complex<T>, is_cvr_complex<U>>,
                          detail::conjunction<is_cvr_complex<T>, is_complex_interoperable<U>>,
                          detail::conjunction<is_cvr_complex<U>, is_complex_interoperable<T>>,
                          detail::conjunction<is_cvr_real<T>, is_cpp_complex<detail::uncvref_t<U>>>,
                          detail::conjunction<is_cvr_real<U>, is_cpp_complex<detail::uncvref_t<T>>>
#if defined(MPPP_WITH_QUADMATH)
                          ,
                          detail::conjunction<is_cvr_real<T>, std::is_same<detail::uncvref_t<U>, complex128>>,
                          detail::conjunction<is_cvr_real<U>, std::is_same<detail::uncvref_t<T>, complex128>>
#endif
                          >;

template <typename T, typename U>
using are_complex_in_place_op_types
    = detail::conjunction<detail::negation<std::is_const<detail::unref_t<T>>>, are_complex_op_types<T, U>>;

#if defined(MPPP_HAVE_CONCEPTS)

template <typename T, typename U>
MPPP_CONCEPT_DECL complex_op_types = are_complex_op_types<T, U>::value;

template <typename T, typename U>
MPPP_CONCEPT_DECL complex_in_place_op_types = are_complex_in_place_op_types<T, U>::value;

#endif

// Precision handling.
inline ::mpfr_prec_t get_prec(const complex &c)
{
    return c.get_prec();
}

inline void set_prec(complex &c, ::mpfr_prec_t p)
{
    c.set_prec(p);
}

inline void prec_round(complex &c, ::mpfr_prec_t p)
{
    c.prec_round(p);
}

namespace detail
{

template <typename... Args>
using complex_set_t = decltype(std::declval<complex &>().set(std::declval<const Args &>()...));

}

#if defined(MPPP_HAVE_CONCEPTS)

template <typename... Args>
MPPP_CONCEPT_DECL complex_set_args = detail::is_detected<detail::complex_set_t, Args...>::value;

#endif

// Generic setter.
#if defined(MPPP_HAVE_CONCEPTS)
template <complex_set_args... Args>
#else
template <typename... Args, detail::enable_if_t<detail::is_detected<detail::complex_set_t, Args...>::value, int> = 0>
#endif
inline complex &set(complex &c, const Args &... args)
{
    return c.set(args...);
}

// Swap.
inline void swap(complex &a, complex &b) noexcept
{
    ::mpc_swap(a._get_mpc_t(), b._get_mpc_t());
}

// Generic conversion function.
#if defined(MPPP_HAVE_CONCEPTS)
template <complex_convertible T>
#else
template <typename T, detail::enable_if_t<is_complex_convertible<T>::value, int> = 0>
#endif
inline bool get(T &rop, const complex &c)
{
    return c.get(rop);
}

namespace detail
{

// NOTE: there's lots of similarity with the implementation
// of the same functionality for real. Perhaps in the future
// we can avoid the repetition.

// A small helper to init the pairs in the functions below. We need this because
// we cannot take the address of a const complex as a complex *.
template <typename Arg, enable_if_t<!is_ncrvr<Arg &&>::value, int> = 0>
inline std::pair<complex *, ::mpfr_prec_t> mpc_nary_op_init_pair(::mpfr_prec_t min_prec, Arg &&arg)
{
    // arg is not a non-const rvalue ref, we cannot steal from it. Init with nullptr.
    return std::make_pair(static_cast<complex *>(nullptr), c_max(arg.get_prec(), min_prec));
}

template <typename Arg, enable_if_t<is_ncrvr<Arg &&>::value, int> = 0>
inline std::pair<complex *, ::mpfr_prec_t> mpc_nary_op_init_pair(::mpfr_prec_t min_prec, Arg &&arg)
{
    // arg is a non-const rvalue ref, and a candidate for stealing resources.
    return std::make_pair(&arg, c_max(arg.get_prec(), min_prec));
}

// A recursive function to determine, in an MPC function call,
// the largest argument we can steal resources from, and the max precision among
// all the arguments.
inline void mpc_nary_op_check_steal(std::pair<complex *, ::mpfr_prec_t> &) {}

// NOTE: we need 2 overloads for this, as we cannot extract a non-const pointer from
// arg0 if arg0 is a const ref.
template <typename Arg0, typename... Args, enable_if_t<!is_ncrvr<Arg0 &&>::value, int> = 0>
void mpc_nary_op_check_steal(std::pair<complex *, ::mpfr_prec_t> &, Arg0 &&, Args &&...);

template <typename Arg0, typename... Args, enable_if_t<is_ncrvr<Arg0 &&>::value, int> = 0>
void mpc_nary_op_check_steal(std::pair<complex *, ::mpfr_prec_t> &, Arg0 &&, Args &&...);

template <typename Arg0, typename... Args, enable_if_t<!is_ncrvr<Arg0 &&>::value, int>>
inline void mpc_nary_op_check_steal(std::pair<complex *, ::mpfr_prec_t> &p, Arg0 &&arg0, Args &&... args)
{
    // arg0 is not a non-const rvalue ref, we won't be able to steal from it regardless. Just
    // update the max prec.
    p.second = c_max(arg0.get_prec(), p.second);
    mpc_nary_op_check_steal(p, std::forward<Args>(args)...);
}

template <typename Arg0, typename... Args, enable_if_t<is_ncrvr<Arg0 &&>::value, int>>
inline void mpc_nary_op_check_steal(std::pair<complex *, ::mpfr_prec_t> &p, Arg0 &&arg0, Args &&... args)
{
    const auto prec0 = arg0.get_prec();
    if (!p.first || prec0 > p.first->get_prec()) {
        // The current argument arg0 is a non-const rvalue reference, and either it's
        // the first argument we encounter we can steal from, or it has a precision
        // larger than the current candidate for stealing resources from. This means that
        // arg0 is the new candidate.
        p.first = &arg0;
    }
    // Update the max precision among the arguments, if necessary.
    p.second = c_max(prec0, p.second);
    mpc_nary_op_check_steal(p, std::forward<Args>(args)...);
}

// A small wrapper to call an MPC function f with arguments args. If the first param is true_type,
// the rounding mode MPC_RNDNN will be appended at the end of the function arguments list.
template <typename F, typename... Args>
inline void mpc_nary_func_wrapper(const std::true_type &, const F &f, Args &&... args)
{
    f(std::forward<Args>(args)..., MPC_RNDNN);
}

template <typename F, typename... Args>
inline void mpc_nary_func_wrapper(const std::false_type &, const F &f, Args &&... args)
{
    f(std::forward<Args>(args)...);
}

// The goal of this helper is to invoke the MPC-like function object f with signature
//
// void f(mpc_t out, const mpc_t x0, const mpc_t x1, ...)
//
// on the mpc_t instances contained in the input complex objects,
//
// f(rop._get_mpc_t(), arg0.get_mpc_t(), arg1.get_mpc_t(), ...)
//
// The helper will ensure that, before the invocation, the precision
// of rop is set to max(min_prec, arg0.get_prec(), arg1.get_prec(), ...).
//
// One of the input arguments may be used as return value in the invocation
// instead of rop if it provides enough precision and it is passed as a non-const
// rvalue reference. In such a case, the selected input argument will be swapped
// into rop after the invocation and before the function returns.
//
// The Rnd flag controls whether to add the rounding mode (MPC_RNDNN) at the end
// of the MPC-like function object arguments list or not.
//
// This function requires that the MPC-like function object being called supports
// overlapping arguments (both input and output).
template <bool Rnd, typename F, typename Arg0, typename... Args>
inline complex &mpc_nary_op_impl(::mpfr_prec_t min_prec, const F &f, complex &rop, Arg0 &&arg0, Args &&... args)
{
    // Make sure min_prec is valid.
    // NOTE: min_prec == 0 is ok, it just means
    // p below will be inited with arg0's precision
    // rather than min_prec.
    assert(min_prec == 0 || real_prec_check(min_prec));

    // This pair will contain:
    //
    // - a pointer to the largest-precision arg from which we can steal resources (may be nullptr),
    // - the largest precision among all args and min_prec (i.e., the target precision
    //   for rop).
    //
    // It is inited with arg0's precision (but no less than min_prec), and a pointer to arg0, if arg0 is a nonconst
    // rvalue ref (a nullptr otherwise).
    auto p = mpc_nary_op_init_pair(min_prec, std::forward<Arg0>(arg0));
    // Finish setting up p by examining the remaining arguments.
    mpc_nary_op_check_steal(p, std::forward<Args>(args)...);

    // Cache for convenience.
    const auto r_prec = rop.get_prec();

    if (p.second == r_prec) {
        // The target precision and the precision of the return value
        // match. No need to steal, just execute the function.
        mpc_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, rop._get_mpc_t(), arg0.get_mpc_t(),
                              args.get_mpc_t()...);
    } else {
        if (r_prec > p.second) {
            // The precision of the return value is larger than the target precision.
            // We can reset its precision destructively
            // because we know it does not overlap with any operand.
            rop.set_prec_impl<false>(p.second);
            mpc_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, rop._get_mpc_t(), arg0.get_mpc_t(),
                                  args.get_mpc_t()...);
        } else if (p.first && p.first->get_prec() == p.second) {
            // The precision of the return value is smaller than the target precision,
            // and we have a candidate for stealing with enough precision: we will use it as return
            // value and then swap out the result to rop.
            mpc_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, p.first->_get_mpc_t(), arg0.get_mpc_t(),
                                  args.get_mpc_t()...);
            swap(*p.first, rop);
        } else {
            // The precision of the return value is smaller than the target precision,
            // and either:
            //
            // - we cannot steal from any argument, or
            // - we can steal from an argument but the selected argument
            //   does not have enough precision.
            //
            // In these cases, we will just set the precision of rop and call the function.
            //
            // NOTE: we need to set the precision without destroying the rop, as rop might
            // overlap with one of the arguments. Since this will be an increase in precision,
            // it should not entail a rounding operation.
            //
            // NOTE: we assume all the precs in the operands and min_prec are valid, so
            // we will not need to check them.
            rop.prec_round_impl<false>(p.second);
            mpc_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, rop._get_mpc_t(), arg0.get_mpc_t(),
                                  args.get_mpc_t()...);
        }
    }

    return rop;
}

// The goal of this helper is to invoke the MPC-like function object f with signature
//
// void f(mpc_t out, const mpc_t x0, const mpc_t x1, ...)
//
// on the mpc_t instances contained in the input complex objects,
//
// f(rop._get_mpc_t(), arg0.get_mpc_t(), arg1.get_mpc_t(), ...)
//
// and then return rop.
//
// The rop object will either be created within the helper with a precision
// set to max(min_prec, arg0.get_prec(), arg1.get_prec(), ...),
// or it will be one of the input arguments if it provides enough precision and
// it is passed as a non-const rvalue reference.
//
// The Rnd flag controls whether to add the rounding mode (MPC_RNDNN) at the end
// of the MPC-like function object arguments list or not.
//
// This function requires that the MPC-like function object being called supports
// overlapping arguments (both input and output).
template <bool Rnd, typename F, typename Arg0, typename... Args>
inline complex mpc_nary_op_return_impl(::mpfr_prec_t min_prec, const F &f, Arg0 &&arg0, Args &&... args)
{
    // Make sure min_prec is valid.
    // NOTE: min_prec == 0 is ok, it just means
    // p below will be inited with arg0's precision
    // rather than min_prec.
    assert(min_prec == 0 || real_prec_check(min_prec));

    // This pair will contain:
    //
    // - a pointer to the largest-precision arg from which we can steal resources (may be nullptr),
    // - the largest precision among all args and min_prec (i.e., the target precision
    //   for the return value).
    //
    // It is inited with arg0's precision (but no less than min_prec), and a pointer to arg0, if arg0 is a nonconst
    // rvalue ref (a nullptr otherwise).
    auto p = mpc_nary_op_init_pair(min_prec, std::forward<Arg0>(arg0));
    // Finish setting up p by examining the remaining arguments.
    mpc_nary_op_check_steal(p, std::forward<Args>(args)...);

    if (p.first && p.first->get_prec() == p.second) {
        // We can steal from one or more args, and the precision of
        // the largest-precision arg we can steal from matches
        // the target precision. Use it.
        mpc_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, p.first->_get_mpc_t(), arg0.get_mpc_t(),
                              args.get_mpc_t()...);
        return std::move(*p.first);
    } else {
        // Either we cannot steal from any arg, or the candidate does not have
        // enough precision. Init a new value and use it instead.
        complex retval{complex::ptag{}, p.second, true};
        mpc_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, retval._get_mpc_t(), arg0.get_mpc_t(),
                              args.get_mpc_t()...);
        return retval;
    }
}

} // namespace detail

// These are helper macros to reduce typing when dealing with the common case
// of exposing MPC-like functions with a single argument (both variants with retval
// and with return). "name" will be the name of the mppp function, "fname" is
// the name of the MPC-like function and "rnd" is a boolean flag that signals whether
// fname requires a rounding mode argument or not.
// The fname function must accept only mpc_t arguments in input (plus the rounding mode if
// rnd is true).

// These are the headers of the overloads that will be produced. They are different depending
// on whether concepts are available or not.
#if defined(MPPP_HAVE_CONCEPTS)
#define MPPP_COMPLEX_MPC_UNARY_HEADER template <cvr_complex T>
#else
#define MPPP_COMPLEX_MPC_UNARY_HEADER template <typename T, detail::enable_if_t<is_cvr_complex<T>::value, int> = 0>
#endif

#define MPPP_COMPLEX_MPC_UNARY_IMPL(name, fname, rnd)                                                                  \
    MPPP_COMPLEX_MPC_UNARY_HEADER inline complex &name(complex &rop, T &&op)                                           \
    {                                                                                                                  \
        return detail::mpc_nary_op_impl<rnd>(0, fname, rop, std::forward<T>(op));                                      \
    }                                                                                                                  \
    MPPP_COMPLEX_MPC_UNARY_HEADER inline complex name(T &&r)                                                           \
    {                                                                                                                  \
        return detail::mpc_nary_op_return_impl<rnd>(0, fname, std::forward<T>(r));                                     \
    }

// Basic arithmetics.

// Ternary addition.
#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T, cvr_complex U>
#else
template <typename T, typename U, cvr_complex_enabler<T, U> = 0>
#endif
inline complex &add(complex &rop, T &&a, U &&b)
{
    return detail::mpc_nary_op_impl<true>(0, ::mpc_add, rop, std::forward<T>(a), std::forward<U>(b));
}

// Ternary subtraction.
#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T, cvr_complex U>
#else
template <typename T, typename U, cvr_complex_enabler<T, U> = 0>
#endif
inline complex &sub(complex &rop, T &&a, U &&b)
{
    return detail::mpc_nary_op_impl<true>(0, ::mpc_sub, rop, std::forward<T>(a), std::forward<U>(b));
}

// Ternary multiplication.
#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T, cvr_complex U>
#else
template <typename T, typename U, cvr_complex_enabler<T, U> = 0>
#endif
inline complex &mul(complex &rop, T &&a, U &&b)
{
    return detail::mpc_nary_op_impl<true>(0, ::mpc_mul, rop, std::forward<T>(a), std::forward<U>(b));
}

// Ternary division.
#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T, cvr_complex U>
#else
template <typename T, typename U, cvr_complex_enabler<T, U> = 0>
#endif
inline complex &div(complex &rop, T &&a, U &&b)
{
    return detail::mpc_nary_op_impl<true>(0, ::mpc_div, rop, std::forward<T>(a), std::forward<U>(b));
}

MPPP_COMPLEX_MPC_UNARY_IMPL(neg, ::mpc_neg, true)
MPPP_COMPLEX_MPC_UNARY_IMPL(conj, ::mpc_conj, true)
MPPP_COMPLEX_MPC_UNARY_IMPL(proj, ::mpc_proj, true)
MPPP_COMPLEX_MPC_UNARY_IMPL(sqr, ::mpc_sqr, true)

// Multiplication by +-i.
#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T>
#else
template <typename T, cvr_complex_enabler<T> = 0>
#endif
inline complex &mul_i(complex &rop, T &&c, int sgn = 0)
{
    auto wrapper = [sgn](::mpc_t r, const ::mpc_t o) { ::mpc_mul_i(r, o, sgn, MPC_RNDNN); };

    return detail::mpc_nary_op_impl<false>(0, wrapper, rop, std::forward<T>(c));
}

#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T>
#else
template <typename T, cvr_complex_enabler<T> = 0>
#endif
inline complex mul_i(T &&c, int sgn = 0)
{
    auto wrapper = [sgn](::mpc_t rop, const ::mpc_t o) { ::mpc_mul_i(rop, o, sgn, MPC_RNDNN); };

    return detail::mpc_nary_op_return_impl<false>(0, wrapper, std::forward<T>(c));
}

// NOTE: these functions return a real, thus we need
// custom implementations.
MPPP_DLL_PUBLIC real &abs(real &, const complex &);
MPPP_DLL_PUBLIC real abs(const complex &);

MPPP_DLL_PUBLIC real &norm(real &, const complex &);
MPPP_DLL_PUBLIC real norm(const complex &);

MPPP_DLL_PUBLIC real &arg(real &, const complex &);
MPPP_DLL_PUBLIC real arg(const complex &);

#undef MPPP_COMPLEX_MPC_UNARY_HEADER
#undef MPPP_COMPLEX_MPC_UNARY_IMPL

#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T>
#else
template <typename T, cvr_complex_enabler<T> = 0>
#endif
inline complex operator+(T &&c)
{
    return std::forward<T>(c);
}

// Prefix increment.
MPPP_DLL_PUBLIC complex &operator++(complex &);

// Suffix increment.
MPPP_DLL_PUBLIC complex operator++(complex &, int);

namespace detail
{

// complex-complex.
template <typename T, typename U, enable_if_t<conjunction<is_cvr_complex<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_add(T &&a, U &&b)
{
    return mpc_nary_op_return_impl<true>(0, ::mpc_add, std::forward<T>(a), std::forward<U>(b));
}

// complex-real.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_add(T &&a, const real &x)
{
    // NOTE: this is the usual pattern in which we transform a non-MPC-like binary operation
    // into an unary MPC-like operation and account for the non-mpc_t argument's precision
    // via the min_prec parameter of mpc_nary_op_return_impl().
    auto wrapper = [&x](::mpc_t c, const ::mpc_t o) { ::mpc_add_fr(c, o, x.get_mpfr_t(), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(x.get_prec(), wrapper, std::forward<T>(a));
}

// real-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_add(const real &x, T &&a)
{
    return dispatch_complex_binary_add(std::forward<T>(a), x);
}

// complex-(anything real-valued other than unsigned integral or real).
// NOTE: MPC has primitives only for ui and mpfr_t addition. Thus, for
// other real-valued types, we implement on top of the real API.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>,
                                  negation<is_cpp_unsigned_integral<U>>>::value,
                      int> = 0>
inline complex dispatch_complex_binary_add(T &&a, const U &x)
{
    // NOTE: another way of implementing this is via the conversion
    // of x to a temporary real whose precision is max(a_prec, x_prec),
    // and then delegating to mpc_add_fr(). The potential advantage of this
    // implementation is that it re-uses the specialised adding primitives
    // of real, which *may* be more efficient than doing real vs real
    // operations after a conversion (but I haven't measured that).

    // Init the return value with the necessary precision.
    // If a already has enough precision, forward it
    // so that we may steal its resources. Otherwise,
    // copy-construct with extended precision.
    // NOTE: this also mirrors the usual semantics of
    // mpc_nary_op_return_impl(): try stealing if a has
    // enough precision, otherwise init new value.
    const auto a_prec = a.get_prec();
    const auto x_prec = real_deduce_precision(x);
    auto ret = (a_prec >= x_prec) ? complex{std::forward<T>(a)} : complex{a, complex_prec_t(x_prec)};

    {
        // NOTE: scope the lifetime of re, so that
        // we are sure that ret is updated before
        // the return statement.
        complex::re_ref re{ret};

        // Add x to the real part of ret.
        *re += x;
    }

    return ret;
}

// (anything real-valued other than unsigned integral or real)-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>,
                                  negation<is_cpp_unsigned_integral<U>>>::value,
                      int> = 0>
inline complex dispatch_complex_binary_add(const U &x, T &&a)
{
    return dispatch_complex_binary_add(std::forward<T>(a), x);
}

// complex-unsigned integral.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cpp_unsigned_integral<U>>::value, int> = 0>
inline complex dispatch_complex_binary_add(T &&a, const U &n)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_add_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

        return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
    } else {
        return dispatch_complex_binary_add(std::forward<T>(a), integer<2>{n});
    }
}

// complex-bool.
// NOTE: make this explicit (rather than letting bool fold into
// the unsigned integrals overload) in order to avoid MSVC warnings.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_add(T &&a, const bool &n)
{
    auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_add_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
}

// unsigned integral-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cpp_unsigned_integral<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_add(const T &n, U &&a)
{
    return dispatch_complex_binary_add(std::forward<U>(a), n);
}

// complex-complex valued interoperable types.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_add(T &&a, const U &c)
{
    // NOTE: same precision-handling scheme as in the
    // complex-real valued overload.
    // NOTE: here we are taking advantage of the fact that
    // U is either std::complex<T> or complex128, for which
    // the precision deduction rules are the same as for T
    // and real128 (i.e., compile-time constant independent
    // of the actual value). If in the future we will have other
    // complex types (e.g., Gaussian rationals) we will have
    // to update this.
    const auto a_prec = a.get_prec();
    const auto c_prec = real_deduce_precision(c.real());
    auto ret = (a_prec >= c_prec) ? complex{std::forward<T>(a)} : complex{a, complex_prec_t(c_prec)};

    {
        complex::re_ref re{ret};
        complex::im_ref im{ret};

        *re += c.real();
        *im += c.imag();
    }

    return ret;
}

// complex valued interoperable types-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_add(const U &c, T &&a)
{
    return dispatch_complex_binary_add(std::forward<T>(a), c);
}

// real-(std::complex or complex128).
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_real<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_add(T &&x, const U &c)
{
    // NOTE: the binary ctor from real+imag parts will
    // select the higher precision.
    return complex{std::forward<T>(x) + c.real(), c.imag()};
}

// (std::complex or complex128)-real.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_real<U>, is_cv_complex_interoperable<T>>::value, int> = 0>
inline complex dispatch_complex_binary_add(const T &c, U &&x)
{
    return dispatch_complex_binary_add(std::forward<U>(x), c);
}

} // namespace detail

// Binary addition.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_op_types<T, U>::value, int> = 0>
#endif
    inline complex operator+(T &&a, U &&b)
{
    return detail::dispatch_complex_binary_add(std::forward<T>(a), std::forward<U>(b));
}

namespace detail
{

// complex-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline void dispatch_complex_in_place_add(complex &a, T &&b)
{
    add(a, a, std::forward<T>(b));
}

// complex-real.
MPPP_DLL_PUBLIC void dispatch_complex_in_place_add(complex &, const real &);

// complex-(anything real-valued other than unsigned integral or real).
template <
    typename T,
    enable_if_t<conjunction<is_rv_complex_interoperable<T>, negation<is_cpp_unsigned_integral<T>>>::value, int> = 0>
inline void dispatch_complex_in_place_add(complex &a, const T &x)
{
    const auto orig_p = a.get_prec();

    complex::re_ref re{a};

    *re += x;

    const auto new_p = re->get_prec();

    // NOTE: the addition of x might have increased
    // the precision of re. If that's the case,
    // increase the precision of the imaginary part as well.
    if (new_p != orig_p) {
        assert(new_p > orig_p);
        complex::im_ref im{a};
        im->prec_round(new_p);
    }
}

// complex-unsigned integral.
template <typename T, enable_if_t<conjunction<is_cpp_unsigned_integral<T>>::value, int> = 0>
inline void dispatch_complex_in_place_add(complex &a, const T &n)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_add_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

        mpc_nary_op_impl<false>(real_deduce_precision(n), wrapper, a, a);
    } else {
        dispatch_complex_in_place_add(a, integer<2>{n});
    }
}

// complex-bool.
// NOTE: make this explicit (rather than letting bool fold into
// the unsigned integrals overload) in order to avoid MSVC warnings.
MPPP_DLL_PUBLIC void dispatch_complex_in_place_add(complex &, bool);

// complex-complex valued.
template <typename T, enable_if_t<is_cv_complex_interoperable<T>::value, int> = 0>
inline void dispatch_complex_in_place_add(complex &a, const T &c)
{
    // NOTE: here we are taking advantage of the fact that
    // T is either std::complex<U> or complex128, for which
    // the precision deduction rules are the same as for U
    // and real128 (i.e., compile-time constant independent
    // of the actual value). If in the future we will have other
    // complex types (e.g., Gaussian rationals) we will have
    // to update this.
    complex::re_ref re{a};
    complex::im_ref im{a};

    *re += c.real();
    *im += c.imag();
}

// complex interoperable-complex, or real-complex valued.
template <typename T, typename U,
          enable_if_t<disjunction<conjunction<is_complex_interoperable<T>, is_cvr_complex<U>>,
                                  conjunction<std::is_same<real, T>, is_complex_interoperable<U>>,
                                  conjunction<is_cvr_real<U>, is_complex_interoperable<T>>>::value,
                      int> = 0>
inline void dispatch_complex_in_place_add(T &x, U &&a)
{
    x = static_cast<T>(x + std::forward<U>(a));
}

} // namespace detail

// In-place addition.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_in_place_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_in_place_op_types<T, U>::value, int> = 0>
#endif
    inline T &operator+=(T &a, U &&b)
{
    detail::dispatch_complex_in_place_add(a, std::forward<U>(b));
    return a;
}

#if defined(MPPP_HAVE_CONCEPTS)
template <cvr_complex T>
#else
template <typename T, cvr_complex_enabler<T> = 0>
#endif
inline complex operator-(T &&c)
{
    complex ret{std::forward<T>(c)};
    ret.neg();
    return ret;
}

// Prefix decrement.
MPPP_DLL_PUBLIC complex &operator--(complex &);

// Suffix decrement.
MPPP_DLL_PUBLIC complex operator--(complex &, int);

namespace detail
{

// complex-complex.
template <typename T, typename U, enable_if_t<conjunction<is_cvr_complex<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_sub(T &&a, U &&b)
{
    return mpc_nary_op_return_impl<true>(0, ::mpc_sub, std::forward<T>(a), std::forward<U>(b));
}

// complex-real.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_sub(T &&a, const real &x)
{
    auto wrapper = [&x](::mpc_t c, const ::mpc_t o) { ::mpc_sub_fr(c, o, x.get_mpfr_t(), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(x.get_prec(), wrapper, std::forward<T>(a));
}

// real-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_sub(const real &x, T &&a)
{
    auto wrapper = [&x](::mpc_t c, const ::mpc_t o) { ::mpc_fr_sub(c, x.get_mpfr_t(), o, MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(x.get_prec(), wrapper, std::forward<T>(a));
}

// complex-(anything real-valued other than unsigned integral or real).
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>,
                                  negation<is_cpp_unsigned_integral<U>>>::value,
                      int> = 0>
inline complex dispatch_complex_binary_sub(T &&a, const U &x)
{
    const auto a_prec = a.get_prec();
    const auto x_prec = real_deduce_precision(x);
    auto ret = (a_prec >= x_prec) ? complex{std::forward<T>(a)} : complex{a, complex_prec_t(x_prec)};

    {
        // NOTE: scope the lifetime of re, so that
        // we are sure that ret is updated before
        // the return statement.
        complex::re_ref re{ret};

        // Subtract x from the real part of ret.
        *re -= x;
    }

    return ret;
}

// (anything real-valued other than unsigned integral or real)-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>,
                                  negation<is_cpp_unsigned_integral<U>>>::value,
                      int> = 0>
inline complex dispatch_complex_binary_sub(const U &x, T &&a)
{
    return -dispatch_complex_binary_sub(std::forward<T>(a), x);
}

// complex-unsigned integral.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cpp_unsigned_integral<U>>::value, int> = 0>
inline complex dispatch_complex_binary_sub(T &&a, const U &n)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_sub_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

        return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
    } else {
        return dispatch_complex_binary_sub(std::forward<T>(a), integer<2>{n});
    }
}

// complex-bool.
// NOTE: make this explicit (rather than letting bool fold into
// the unsigned integrals overload) in order to avoid MSVC warnings.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_sub(T &&a, const bool &n)
{
    auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_sub_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
}

// unsigned integral-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cpp_unsigned_integral<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_sub(const T &n, U &&a)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_ui_sub(c, static_cast<unsigned long>(n), o, MPC_RNDNN); };

        return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<U>(a));
    } else {
        return dispatch_complex_binary_sub(integer<2>{n}, std::forward<U>(a));
    }
}

// bool-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_sub(const bool &n, T &&a)
{
    auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_ui_sub(c, static_cast<unsigned long>(n), o, MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
}

// complex-complex valued interoperable types.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_sub(T &&a, const U &c)
{
    const auto a_prec = a.get_prec();
    const auto c_prec = real_deduce_precision(c.real());
    auto ret = (a_prec >= c_prec) ? complex{std::forward<T>(a)} : complex{a, complex_prec_t(c_prec)};

    {
        complex::re_ref re{ret};
        complex::im_ref im{ret};

        *re -= c.real();
        *im -= c.imag();
    }

    return ret;
}

// complex valued interoperable types-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_sub(const U &c, T &&a)
{
    return -dispatch_complex_binary_sub(std::forward<T>(a), c);
}

// real-(std::complex or complex128).
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_real<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_sub(T &&x, const U &c)
{
    return complex{std::forward<T>(x) - c.real(), -c.imag()};
}

// (std::complex or complex128)-real.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_real<U>, is_cv_complex_interoperable<T>>::value, int> = 0>
inline complex dispatch_complex_binary_sub(const T &c, U &&x)
{
    return -dispatch_complex_binary_sub(std::forward<U>(x), c);
}

} // namespace detail

// Binary subtraction.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_op_types<T, U>::value, int> = 0>
#endif
    inline complex operator-(T &&a, U &&b)
{
    return detail::dispatch_complex_binary_sub(std::forward<T>(a), std::forward<U>(b));
}

namespace detail
{

// complex-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline void dispatch_complex_in_place_sub(complex &a, T &&b)
{
    sub(a, a, std::forward<T>(b));
}

// complex-real.
MPPP_DLL_PUBLIC void dispatch_complex_in_place_sub(complex &, const real &);

// complex-(anything real-valued other than unsigned integral or real).
template <
    typename T,
    enable_if_t<conjunction<is_rv_complex_interoperable<T>, negation<is_cpp_unsigned_integral<T>>>::value, int> = 0>
inline void dispatch_complex_in_place_sub(complex &a, const T &x)
{
    const auto orig_p = a.get_prec();

    complex::re_ref re{a};

    *re -= x;

    const auto new_p = re->get_prec();

    if (new_p != orig_p) {
        assert(new_p > orig_p);
        complex::im_ref im{a};
        im->prec_round(new_p);
    }
}

// complex-unsigned integral.
template <typename T, enable_if_t<conjunction<is_cpp_unsigned_integral<T>>::value, int> = 0>
inline void dispatch_complex_in_place_sub(complex &a, const T &n)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_sub_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

        mpc_nary_op_impl<false>(real_deduce_precision(n), wrapper, a, a);
    } else {
        dispatch_complex_in_place_sub(a, integer<2>{n});
    }
}

// complex-bool.
// NOTE: make this explicit (rather than letting bool fold into
// the unsigned integrals overload) in order to avoid MSVC warnings.
MPPP_DLL_PUBLIC void dispatch_complex_in_place_sub(complex &, bool);

// complex-complex valued.
template <typename T, enable_if_t<is_cv_complex_interoperable<T>::value, int> = 0>
inline void dispatch_complex_in_place_sub(complex &a, const T &c)
{
    complex::re_ref re{a};
    complex::im_ref im{a};

    *re -= c.real();
    *im -= c.imag();
}

// complex interoperable-complex, or real-complex valued.
template <typename T, typename U,
          enable_if_t<disjunction<conjunction<is_complex_interoperable<T>, is_cvr_complex<U>>,
                                  conjunction<std::is_same<real, T>, is_complex_interoperable<U>>,
                                  conjunction<is_cvr_real<U>, is_complex_interoperable<T>>>::value,
                      int> = 0>
inline void dispatch_complex_in_place_sub(T &x, U &&a)
{
    x = static_cast<T>(x - std::forward<U>(a));
}

} // namespace detail

// In-place subtraction.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_in_place_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_in_place_op_types<T, U>::value, int> = 0>
#endif
    inline T &operator-=(T &a, U &&b)
{
    detail::dispatch_complex_in_place_sub(a, std::forward<U>(b));
    return a;
}

namespace detail
{

// complex-complex.
template <typename T, typename U, enable_if_t<conjunction<is_cvr_complex<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(T &&a, U &&b)
{
    return mpc_nary_op_return_impl<true>(0, ::mpc_mul, std::forward<T>(a), std::forward<U>(b));
}

// complex-real.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_mul(T &&a, const real &x)
{
    auto wrapper = [&x](::mpc_t c, const ::mpc_t o) { ::mpc_mul_fr(c, o, x.get_mpfr_t(), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(x.get_prec(), wrapper, std::forward<T>(a));
}

// real-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_mul(const real &x, T &&a)
{
    return dispatch_complex_binary_mul(std::forward<T>(a), x);
}

// complex-(anything real-valued other than integral or real).
template <
    typename T, typename U,
    enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>, negation<is_cpp_integral<U>>>::value,
                int> = 0>
inline complex dispatch_complex_binary_mul(T &&a, const U &x)
{
    const auto a_prec = a.get_prec();
    const auto x_prec = real_deduce_precision(x);
    auto ret = (a_prec >= x_prec) ? complex{std::forward<T>(a)} : complex{a, complex_prec_t(x_prec)};

    {
        // NOTE: scope the lifetime of re/im, so that
        // we are sure that ret is updated before
        // the return statement.
        complex::re_ref re{ret};
        complex::im_ref im{ret};

        // Multiply the real/imag parts of ret by x.
        *re *= x;
        *im *= x;
    }

    return ret;
}

// (anything real-valued other than integral or real)-complex.
template <
    typename T, typename U,
    enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>, negation<is_cpp_integral<U>>>::value,
                int> = 0>
inline complex dispatch_complex_binary_mul(const U &x, T &&a)
{
    return dispatch_complex_binary_mul(std::forward<T>(a), x);
}

// complex-unsigned integral.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cpp_unsigned_integral<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(T &&a, const U &n)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_mul_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

        return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
    } else {
        return dispatch_complex_binary_mul(std::forward<T>(a), integer<2>{n});
    }
}

// complex-bool.
// NOTE: make this explicit (rather than letting bool fold into
// the unsigned integrals overload) in order to avoid MSVC warnings.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_mul(T &&a, const bool &n)
{
    auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_mul_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
}

// unsigned integral-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cpp_unsigned_integral<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(const T &n, U &&a)
{
    return dispatch_complex_binary_mul(std::forward<U>(a), n);
}

// complex-signed integral.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cpp_signed_integral<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(T &&a, const U &n)
{
    if (n <= nl_max<long>() && n >= nl_min<long>()) {
        auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_mul_si(c, o, static_cast<long>(n), MPC_RNDNN); };

        return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
    } else {
        return dispatch_complex_binary_mul(std::forward<T>(a), integer<2>{n});
    }
}

// signed integral-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cpp_signed_integral<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(const T &n, U &&a)
{
    return dispatch_complex_binary_mul(std::forward<U>(a), n);
}

// complex-complex valued interoperable types.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(T &&a, const U &c)
{
    // NOTE: here we are taking advantage of the fact that
    // U is either std::complex or complex128, for which
    // the precision deduction rules are the same as for
    // the underlying real value type (i.e., compile-time constant independent
    // of the actual value). If in the future we will have other
    // complex types (e.g., Gaussian rationals) we will have
    // to update this.
    MPPP_MAYBE_TLS complex tmp;
    tmp.set_prec(c_max(a.get_prec(), real_deduce_precision(c.real())));
    tmp.set(c);

    return std::forward<T>(a) * tmp;
}

// complex valued interoperable types-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(const U &c, T &&a)
{
    return dispatch_complex_binary_mul(std::forward<T>(a), c);
}

// real-(std::complex or complex128).
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_real<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(T &&x, const U &c)
{
    // NOTE: don't forward x twice.
    auto re = x * c.real();
    auto im = std::forward<T>(x) * c.imag();

    return complex{std::move(re), std::move(im)};
}

// (std::complex or complex128)-real.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_real<U>, is_cv_complex_interoperable<T>>::value, int> = 0>
inline complex dispatch_complex_binary_mul(const T &c, U &&x)
{
    return dispatch_complex_binary_mul(std::forward<U>(x), c);
}

} // namespace detail

// Binary multiplication.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_op_types<T, U>::value, int> = 0>
#endif
    inline complex operator*(T &&a, U &&b)
{
    return detail::dispatch_complex_binary_mul(std::forward<T>(a), std::forward<U>(b));
}

namespace detail
{

// complex-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline void dispatch_complex_in_place_mul(complex &a, T &&b)
{
    mul(a, a, std::forward<T>(b));
}

// complex-real.
MPPP_DLL_PUBLIC void dispatch_complex_in_place_mul(complex &, const real &);

// complex-(anything real-valued other than integral or real).
template <typename T,
          enable_if_t<conjunction<is_rv_complex_interoperable<T>, negation<is_cpp_integral<T>>>::value, int> = 0>
inline void dispatch_complex_in_place_mul(complex &a, const T &x)
{
    complex::re_ref re{a};
    complex::im_ref im{a};

    // NOTE: the multiplications here may change the
    // precisions of re/im. Because the original
    // precisions were identical and because re/im are
    // multiplied by the same value, they
    // will have the same new precision.
    *re *= x;
    *im *= x;

    assert(re->get_prec() == im->get_prec());
}

// complex-unsigned integral.
template <typename T, enable_if_t<conjunction<is_cpp_unsigned_integral<T>>::value, int> = 0>
inline void dispatch_complex_in_place_mul(complex &a, const T &n)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_mul_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

        mpc_nary_op_impl<false>(real_deduce_precision(n), wrapper, a, a);
    } else {
        dispatch_complex_in_place_mul(a, integer<2>{n});
    }
}

// complex-bool.
// NOTE: make this explicit (rather than letting bool fold into
// the unsigned integrals overload) in order to avoid MSVC warnings.
MPPP_DLL_PUBLIC void dispatch_complex_in_place_mul(complex &, bool);

// complex-signed integral.
template <typename T, enable_if_t<conjunction<is_cpp_signed_integral<T>>::value, int> = 0>
inline void dispatch_complex_in_place_mul(complex &a, const T &n)
{
    if (n <= nl_max<long>() && n >= nl_min<long>()) {
        auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_mul_si(c, o, static_cast<long>(n), MPC_RNDNN); };

        mpc_nary_op_impl<false>(real_deduce_precision(n), wrapper, a, a);
    } else {
        dispatch_complex_in_place_mul(a, integer<2>{n});
    }
}

// complex-complex valued.
template <typename T, enable_if_t<is_cv_complex_interoperable<T>::value, int> = 0>
inline void dispatch_complex_in_place_mul(complex &a, const T &c)
{
    // NOTE: here we are taking advantage of the fact that
    // T is either std::complex or complex128, for which
    // the precision deduction rules are the same as for
    // the underlying real value type (i.e., compile-time constant independent
    // of the actual value). If in the future we will have other
    // complex types (e.g., Gaussian rationals) we will have
    // to update this.
    MPPP_MAYBE_TLS complex tmp;
    tmp.set_prec(c_max(a.get_prec(), real_deduce_precision(c.real())));
    tmp.set(c);

    dispatch_complex_in_place_mul(a, tmp);
}

// complex interoperable-complex, or real-complex valued.
template <typename T, typename U,
          enable_if_t<disjunction<conjunction<is_complex_interoperable<T>, is_cvr_complex<U>>,
                                  conjunction<std::is_same<real, T>, is_complex_interoperable<U>>,
                                  conjunction<is_cvr_real<U>, is_complex_interoperable<T>>>::value,
                      int> = 0>
inline void dispatch_complex_in_place_mul(T &x, U &&a)
{
    x = static_cast<T>(x * std::forward<U>(a));
}

} // namespace detail

// In-place multiplication.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_in_place_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_in_place_op_types<T, U>::value, int> = 0>
#endif
    inline T &operator*=(T &a, U &&b)
{
    detail::dispatch_complex_in_place_mul(a, std::forward<U>(b));
    return a;
}

namespace detail
{

// complex-complex.
template <typename T, typename U, enable_if_t<conjunction<is_cvr_complex<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_div(T &&a, U &&b)
{
    return mpc_nary_op_return_impl<true>(0, ::mpc_div, std::forward<T>(a), std::forward<U>(b));
}

// complex-real.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_div(T &&a, const real &x)
{
    auto wrapper = [&x](::mpc_t c, const ::mpc_t o) { ::mpc_div_fr(c, o, x.get_mpfr_t(), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(x.get_prec(), wrapper, std::forward<T>(a));
}

// real-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_div(const real &x, T &&a)
{
    auto wrapper = [&x](::mpc_t c, const ::mpc_t o) { ::mpc_fr_div(c, x.get_mpfr_t(), o, MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(x.get_prec(), wrapper, std::forward<T>(a));
}

// complex-(anything real-valued other than unsigned integral or real).
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>,
                                  negation<is_cpp_unsigned_integral<U>>>::value,
                      int> = 0>
inline complex dispatch_complex_binary_div(T &&a, const U &x)
{
    const auto a_prec = a.get_prec();
    const auto x_prec = real_deduce_precision(x);
    auto ret = (a_prec >= x_prec) ? complex{std::forward<T>(a)} : complex{a, complex_prec_t(x_prec)};

    {
        // NOTE: scope the lifetime of re/im, so that
        // we are sure that ret is updated before
        // the return statement.
        complex::re_ref re{ret};
        complex::im_ref im{ret};

        // Divide the real/imag parts of ret by x.
        *re /= x;
        *im /= x;
    }

    return ret;
}

// (anything real-valued other than unsigned integral or real)-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_rv_complex_interoperable<U>,
                                  negation<is_cpp_unsigned_integral<U>>>::value,
                      int> = 0>
inline complex dispatch_complex_binary_div(const U &x, T &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp.set_prec(c_max(a.get_prec(), real_deduce_precision(x)));
    tmp.set(x);

    return dispatch_complex_binary_div(tmp, std::forward<T>(a));
}

// complex-unsigned integral.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cpp_unsigned_integral<U>>::value, int> = 0>
inline complex dispatch_complex_binary_div(T &&a, const U &n)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_div_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

        return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
    } else {
        return dispatch_complex_binary_div(std::forward<T>(a), integer<2>{n});
    }
}

// complex-bool.
// NOTE: make this explicit (rather than letting bool fold into
// the unsigned integrals overload) in order to avoid MSVC warnings.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_div(T &&a, const bool &n)
{
    auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_div_ui(c, o, static_cast<unsigned long>(n), MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
}

// unsigned integral-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cpp_unsigned_integral<T>, is_cvr_complex<U>>::value, int> = 0>
inline complex dispatch_complex_binary_div(const T &n, U &&a)
{
    if (n <= nl_max<unsigned long>()) {
        auto wrapper
            = [n](::mpc_t c, const ::mpc_t o) { ::mpc_ui_div(c, static_cast<unsigned long>(n), o, MPC_RNDNN); };

        return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<U>(a));
    } else {
        return dispatch_complex_binary_div(integer<2>{n}, std::forward<U>(a));
    }
}

// bool-complex.
template <typename T, enable_if_t<is_cvr_complex<T>::value, int> = 0>
inline complex dispatch_complex_binary_div(const bool &n, T &&a)
{
    auto wrapper = [n](::mpc_t c, const ::mpc_t o) { ::mpc_ui_div(c, static_cast<unsigned long>(n), o, MPC_RNDNN); };

    return mpc_nary_op_return_impl<false>(real_deduce_precision(n), wrapper, std::forward<T>(a));
}

// complex-complex valued interoperable types.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_div(T &&a, const U &c)
{
    // NOTE: here we are taking advantage of the fact that
    // U is either std::complex or complex128, for which
    // the precision deduction rules are the same as for
    // the underlying real value type (i.e., compile-time constant independent
    // of the actual value). If in the future we will have other
    // complex types (e.g., Gaussian rationals) we will have
    // to update this.
    MPPP_MAYBE_TLS complex tmp;
    tmp.set_prec(c_max(a.get_prec(), real_deduce_precision(c.real())));
    tmp.set(c);

    return dispatch_complex_binary_div(std::forward<T>(a), tmp);
}

// complex valued interoperable types-complex.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_complex<T>, is_cv_complex_interoperable<U>>::value, int> = 0>
inline complex dispatch_complex_binary_div(const U &c, T &&a)
{
    // NOTE: here we are taking advantage of the fact that
    // U is either std::complex or complex128, for which
    // the precision deduction rules are the same as for
    // the underlying real value type (i.e., compile-time constant independent
    // of the actual value). If in the future we will have other
    // complex types (e.g., Gaussian rationals) we will have
    // to update this.
    MPPP_MAYBE_TLS complex tmp;
    tmp.set_prec(c_max(a.get_prec(), real_deduce_precision(c.real())));
    tmp.set(c);

    return dispatch_complex_binary_div(tmp, std::forward<T>(a));
}

// real-(std::complex or complex128).
template <typename T, enable_if_t<is_cv_complex_interoperable<T>::value, int> = 0>
inline complex dispatch_complex_binary_div(const real &x, const T &c)
{
    // NOTE: here we are taking advantage of the fact that
    // U is either std::complex or complex128, for which
    // the precision deduction rules are the same as for
    // the underlying real value type (i.e., compile-time constant independent
    // of the actual value). If in the future we will have other
    // complex types (e.g., Gaussian rationals) we will have
    // to update this.
    MPPP_MAYBE_TLS complex tmp1, tmp2;

    const auto p = c_max(x.get_prec(), real_deduce_precision(c.real()));

    tmp1.set_prec(p);
    tmp1.set(x);

    tmp2.set_prec(p);
    tmp2.set(c);

    return dispatch_complex_binary_div(tmp1, tmp2);
}

// (std::complex or complex128)-real.
template <typename T, typename U,
          enable_if_t<conjunction<is_cvr_real<U>, is_cv_complex_interoperable<T>>::value, int> = 0>
inline complex dispatch_complex_binary_div(const T &c, U &&x)
{
    // NOTE: don't forward x twice.
    auto re = c.real() / x;
    auto im = c.imag() / std::forward<U>(x);

    return complex{std::move(re), std::move(im)};
}

} // namespace detail

// Binary division.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_op_types<T, U>::value, int> = 0>
#endif
    inline complex operator/(T &&a, U &&b)
{
    return detail::dispatch_complex_binary_div(std::forward<T>(a), std::forward<U>(b));
}

// Stream operator.
MPPP_DLL_PUBLIC std::ostream &operator<<(std::ostream &, const complex &);

namespace detail
{

MPPP_DLL_PUBLIC bool dispatch_complex_equality(const complex &, const complex &);

template <typename T, enable_if_t<is_rv_complex_interoperable<T>::value, int> = 0>
inline bool dispatch_complex_equality(const complex &c, const T &x)
{
    complex::re_cref rex{c};

    return mpfr_zero_p(mpc_imagref(c.get_mpc_t())) != 0 && *rex == x;
}

template <typename T, enable_if_t<!is_rv_complex_interoperable<T>::value, int> = 0>
inline bool dispatch_complex_equality(const complex &c1, const T &c2)
{
    complex::re_cref rex{c1};
    complex::im_cref iex{c1};

    return *rex == c2.real() && *iex == c2.imag();
}

template <typename T>
inline bool dispatch_complex_equality(const T &x, const complex &c)
{
    return dispatch_complex_equality(c, x);
}

} // namespace detail

// Equality.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_op_types<T, U>::value, int> = 0>
#endif
    inline bool operator==(const T &x, const U &y)
{
    return detail::dispatch_complex_equality(x, y);
}

// Inequality.
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires complex_op_types<T, U>
#else
template <typename T, typename U, detail::enable_if_t<are_complex_op_types<T, U>::value, int> = 0>
#endif
    inline bool operator!=(const T &x, const U &y)
{
    return !(x == y);
}

} // namespace mppp

#else

#error The complex.hpp header was included but mp++ was not configured with the MPPP_WITH_MPC option.

#endif

#endif
