#ifndef LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_UTIL_H
#define LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_UTIL_H
#include <limits>
#include <molpro/linalg/itsolv/ArrayHandlers.h>
#include <molpro/linalg/itsolv/subspace/Matrix.h>
#include <molpro/linalg/itsolv/wrap.h>

namespace molpro::linalg::itsolv::subspace::util {
namespace detail {
template <class R, class Q, class Z, class W, bool = std::is_same<R, Z>::value, bool = std::is_same<W, Q>::value>
struct Overlap {
  static Matrix<double> _(const CVecRef<R>& left, const CVecRef<Q>& right, array::ArrayHandler<Z, W>& handler) {
    auto m = Matrix<double>({left.size(), right.size()});
    for (size_t i = 0; i < m.rows(); ++i)
      for (size_t j = 0; j < m.cols(); ++j)
        m(i, j) = handler.dot(left[i], right[j]);
    return m;
  }
};

template <class R, class Q, class Z, class W>
struct Overlap<R, Q, Z, W, false, false> {
  static Matrix<double> _(const CVecRef<R>& left, const CVecRef<Q>& right, array::ArrayHandler<Z, W>& handler) {
    auto m = Matrix<double>({left.size(), right.size()});
    for (size_t i = 0; i < m.rows(); ++i)
      for (size_t j = 0; j < m.cols(); ++j)
        m(i, j) = handler.dot(right[j], left[i]);
    return m;
  }
};

} // namespace detail

//! Calculates overlap matrix between left and right vectors
template <class R, class Q, class Z, class W>
Matrix<double> overlap(const CVecRef<R>& left, const CVecRef<Q>& right, array::ArrayHandler<Z, W>& handler) {
  return detail::Overlap<R, Q, Z, W>::_(left, right, handler);
}

//! Calculates overlap matrix for a parameter set. Matrix is symmetric by construction.
template <class R>
Matrix<double> overlap(const CVecRef<R>& params, array::ArrayHandler<R, R>& handler) {
  auto m = Matrix<double>({params.size(), params.size()});
  for (size_t i = 0; i < m.rows(); ++i)
    for (size_t j = 0; j <= i; ++j)
      m(i, j) = m(j, i) = handler.dot(params[i], params[j]);
  return m;
}

template <typename T>
void matrix_symmetrize(Matrix<T>& mat) {
  assert(mat.rows() == mat.cols() && "must be a square matrix");
  for (size_t i = 0; i < mat.rows(); ++i)
    for (size_t j = 0; j < i; ++j)
      mat(i, j) = mat(j, i) = 0.5 * (mat(i, j) + mat(j, i));
}

//! Return maximum element in a matrix along specified rows and columns
template <typename T>
typename Matrix<T>::coord_type max_element_index(const std::list<size_t>& rows, const std::list<size_t>& cols,
                                                 const Matrix<T>& mat) {
  auto max_el = std::numeric_limits<T>::lowest();
  auto ind = typename Matrix<T>::coord_type{0, 0};
  for (auto i : rows) {
    for (auto j : cols) {
      if (mat(i, j) > max_el) {
        max_el = mat(i, j);
        ind = {i, j};
      }
    }
  }
  return ind;
}

/*!
 * @brief Returns order of rows in a matrix slice that brings it closest to identity.
 *
 * @code{.cpp}
 * auto order = eye_order(mat);
 * for( size_t i = 0; i < n_rows; ++i){
 *   mat_new.row(i) = mat.row(order[i]);
 * }
 *
 * @endcode
 * @tparam Slice Slice type
 * @param mat  square matrix
 */
template <typename Slice>
std::vector<size_t> eye_order(const Slice& mat) {
  auto dim = mat.dimensions();
  auto rows = std::list<size_t>{};
  auto cols = std::list<size_t>{};
  for (size_t i = 0; i < dim.first; ++i) {
    rows.emplace_back(i);
    cols.emplace_back(i);
  }
  auto order = std::vector<size_t>(dim.first);
  size_t i, j;
  while (!rows.empty() && !cols.empty()) {
    std::tie(i, j) = max_element_index(rows, cols, mat);
    order.at(j) = i;
    auto it_row = std::find(begin(rows), end(rows), i);
    auto it_col = std::find(begin(cols), end(cols), j);
    rows.erase(it_row);
    cols.erase(it_col);
  }
  return order;
}

} // namespace molpro::linalg::itsolv::subspace::util

#endif // LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_UTIL_H
