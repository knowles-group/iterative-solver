#ifndef LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_WRAP_H
#define LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_WRAP_H
#include <functional>
#include <limits>
#include <type_traits>
#include <vector>

namespace molpro::linalg::itsolv {

template <class A>
using VecRef = std::vector<std::reference_wrapper<A>>;

template <class A>
using CVecRef = std::vector<std::reference_wrapper<const A>>;

//! Decays CV and reference qualifiers same as std::decay, but also decays std::reference_wrapper to its underlying type
template <class T>
struct decay {
  using type = std::decay_t<T>;
};

template <class T>
struct decay<std::reference_wrapper<T>> {
  using type = std::decay_t<T>;
};

template <class T>
using decay_t = typename decay<T>::type;

//! Takes a vector of containers and returns a vector of references to each element
template <class R>
auto wrap(const std::vector<R>& vec) {
  auto w = CVecRef<decay_t<R>>{};
  std::copy(begin(vec), end(vec), std::back_inserter(w));
  return w;
}

//! Takes a vector of containers and returns a vector of references to each element
template <class R>
auto wrap(std::vector<R>& vec) {
  auto w = VecRef<decay_t<R>>{};
  std::copy(begin(vec), end(vec), std::back_inserter(w));
  return w;
}

//! Takes a vector of containers and returns a vector of references to each element
template <class R>
auto cwrap(std::vector<R>& vec) {
  auto w = CVecRef<decay_t<R>>{};
  std::copy(begin(vec), end(vec), std::back_inserter(w));
  return w;
}

//! Takes a begin and end iterators and returns a vector of references to each element
template <class R, class ForwardIt>
auto wrap(ForwardIt begin, ForwardIt end) {
  auto w = VecRef<R>{};
  std::copy(begin, end, std::back_inserter(w));
  return w;
}

//! Takes a begin and end iterators and returns a vector of references to each element
template <class R, class ForwardIt>
auto cwrap(ForwardIt begin, ForwardIt end) {
  auto w = CVecRef<R>{};
  std::copy(begin, end, std::back_inserter(w));
  return w;
}

//! Takes a map of containers and returns a vector of references to each element in the same order
template <typename I, class R>
auto wrap(const std::map<I, R>& vec) {
  auto w = CVecRef<decay_t<R>>{};
  std::transform(begin(vec), end(vec), std::back_inserter(w), [](const auto& v) { return std::cref(v.second); });
  return w;
}

//! Takes a map of containers and returns a vector of references to each element in the same order
template <typename I, class R>
auto wrap(std::map<I, R>& vec) {
  auto w = VecRef<decay_t<R>>{};
  std::transform(begin(vec), end(vec), std::back_inserter(w), [](auto& v) { return std::ref(v.second); });
  return w;
}

//! Takes a map of containers and returns a vector of references to each element in the same order
template <typename I, class R>
auto cwrap(std::map<I, R>& vec) {
  auto w = CVecRef<decay_t<R>>{};
  std::transform(begin(vec), end(vec), std::back_inserter(w), [](auto& v) { return std::cref(v.second); });
  return w;
}

/*!
 * @brief Given wrapped references in wparams and a range of original parameters [begin, end), returns
 * indices of paramters that are wrapped in this range.
 * @param wparams wrapped references to subset of params
 * @param begin start of range of original paramaters
 * @param end end of range of original paramaters
 */
template <typename R, typename ForwardIt>
std::vector<size_t> find_ref(const VecRef<R>& wparams, ForwardIt begin, ForwardIt end) {
  auto indices = std::vector<size_t>{};
  for (auto it = begin; it != end; ++it) {
    auto it_found = std::find_if(std::begin(wparams), std::end(wparams),
                                 [&it](const auto& w) { return std::addressof(w.get()) == std::addressof(*it); });
    if (it_found != std::end(wparams))
      indices.emplace_back(std::distance(begin, it));
  }
  return indices;
}
} // namespace molpro::linalg::itsolv
#endif // LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_WRAP_H
