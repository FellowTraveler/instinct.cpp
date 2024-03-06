//
// Created by RobinQu on 2024/3/3.
//

#ifndef ASSERTIONS_HPP
#define ASSERTIONS_HPP

#include "CoreGlobals.hpp"


namespace INSTINCT_CORE_NS {
    template<typename R>
    concept sized_range = std::ranges::input_range<R> && std::ranges::sized_range<R>;

    template <typename R1, typename R2>
    requires sized_range<R1> && sized_range<R2>
    static bool check_equality(R1&& a, R2&& b) {
        if (std::ranges::size(a) != std::ranges::size(b)) {
            return false;
        }
        auto itr_a = a.begin();
        auto itr_b = b.begin();
        while(itr_a != a.end() && itr_b != b.end()) {
            if(*itr_a != *itr_b) {
                return false;
            }
            ++itr_a;
            ++itr_b;
        }
        return true;
    }

    template <typename R>
    requires sized_range<R>
    static bool check_non_empty_range(R&& r) {
        return std::ranges::size(r) > 0;
    }

    template <typename R>
    requires sized_range<R>
    static void assert_non_empty_range(R&& r, const std::string& message = "range object should not be empty") {
        if (check_non_empty_range(std::forward<R>(r))) {
            throw InstinctException(message);
        }
    }

    template <typename R1, typename R2>
    requires sized_range<R1> && sized_range<R2>
    static bool check_equal_size(R1&& r1, R2&& r2) {
        return std::ranges::size(std::forward<R1>(r1)) == std::ranges::size(std::forward<R2>(r2));
    }

    template <typename R1, typename R2>
    requires sized_range<R1> && sized_range<R2>
    static void assert_equal_size(R1&& r1, R2&& r2, const std::string& message = "two range objects should have equal size") {
        if (!check_equal_size(std::forward<R1>(r1), std::forward<R2>(r2))) {
            throw InstinctException(message);
        }
    }


}

#endif //ASSERTIONS_HPP
