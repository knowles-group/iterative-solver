#ifndef ITERATIVESOLVER_H
#define ITERATIVESOLVER_H
#include "molpro/ProfilerSingle.h"
#include "molpro/linalg/iterativesolver/Q.h"
#ifdef TIMING
#include <chrono>
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <Eigen/Dense>
#include <algorithm>
#include <climits>
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef LINEARALGEBRA_OFFLINE
#define LINEARALGEBRA_OFFLINE 0x01
#endif
#ifndef LINEARALGEBRA_DISTRIBUTED
#define LINEARALGEBRA_DISTRIBUTED 0x02
#endif

#undef isnan
#undef isinf
#include <memory>
#include <molpro/iostream.h>

/*!
 * @brief Contains classes that implement various iterative equation solvers.
 * They all share the feature that access to the client-provided solution and residual
 * vectors is via a potentially opaque interface to copy, scale, scalar product and
 * scalar-times-vector operations only.
 */
namespace molpro {
namespace linalg {
typedef std::map<std::string, std::string> optionMap;
template <class T>
static std::vector<T> nullStdVector;
template <class T>
static std::vector<typename T::value_type> nullVectorP;
template <class T>
static std::vector<std::vector<typename T::value_type>> nullVectorSetP;
template <class T>
static std::vector<std::reference_wrapper<T>> nullVectorRefSet;
template <class T>
static std::vector<std::vector<std::reference_wrapper<typename T::value_type>>> nullVectorRefSetP;

/*!
 * \brief A base class for iterative solvers for linear and non-linear equations, and linear eigensystems.
 *
 * The calling program should set up its own iterative loop, and in each iteration
 * - calculate
 * the action of the matrix on the current expansion vector (linear), or the actual
 * residual (non-linear)
 * - make a call to addVector() which takes the current and previous parameters and proposes
 * an improved estimate, and the best estimate of the residual vector.
 * - calculate a new solution (non-linear) or expansion vector (linear) by implementing
 * appropriate preconditioning on the residual, if addVector() has requested it.
 * -  make a call to endIteration()
 *
 * Classes that derive from this will, in the simplest case, need to provide just the solveReducedProblem() method that
 * governs how the parameter and residual vectors from successive iterations should be combined to form an optimum
 * solution with minimal residual.
 *
 * The underlying vector spaces are accessed through instances of the T class.
 * - Both scalar (T) and vector(std::vector<T>) interfaces are provided to the class functions addVector(),
 * endIteration()
 * - The class T must provide the following functions
 *   - scalar_type dot(const T& y) - scalar product of two vectors
 *   - void axpy(scalar_type a, const T& y) add a multiple a of y to *this; scalar_type is deduced from the return type
 * of dot()
 *   - scal(scalar_type a) scale *this by a. In the special case a==0, *this on entry might be uninitialised.
 *   - T(const T&, unsigned int option=0) - copy constructor containing an additional int argument that advise the
 * implementation of T on where to store its data. If zero, it should try to store in memory; if 1 or 3, it may store
 * offline, and if 2 or 3, distributed. These options are presented when copies of input solution and residual vectors
 * are taken to store the history, and if implemented, can save memory.
 *
 * @tparam T The class encapsulating solution and residual vectors
 * @tparam slowvector Used internally as a class for storing vectors on backing store
 */
template <class T, class slowvector = T>
class IterativeSolver {
public:
  // clang-format off
  IterativeSolver(std::shared_ptr<molpro::Profiler> profiler = nullptr
  ) :
      m_Pvectors(0),
      m_verbosity(0),
      m_thresh(1e-8),
      m_actions(0),
      m_maxIterations(1000),
      m_minIterations(0),
      m_linear(false),
      m_hermitian(false),
      m_roots(0),
      m_rspt(false),
      m_options(optionMap()),
      m_subspaceMatrixResRes(false),
      m_residual_eigen(false),
      m_residual_rhs(false),
      m_residuals(),
      m_solutions(),
      m_others(),
      m_rhs(),
      m_lastVectorIndex(0),
      m_updateShift(0),
      m_interpolation(),
      m_dimension(0),
      m_value_print_name("value"),
      m_iterations(0),
      m_singularity_threshold(1e-5),
      m_added_vectors(0),
      m_augmented_hessian(0),
      m_svdThreshold(1e-15),
      m_maxQ(std::max(m_roots, size_t(16))),
      m_profiler(profiler),
      m_pspace(),
      m_qspace(m_pspace, m_hermitian),
      m_threshold_residual_recalculate(1e-16),
      m_exclude_r_from_redundancy_test(false),
      m_orthogonalise_Q(true),
      m_nullify_solution_before_update(false)
{
}
  // clang-format on

  virtual ~IterativeSolver() = default;

protected:
  using value_type = typename T::value_type;                            ///< The underlying type of elements of vectors
  using vectorSet = typename std::vector<T>;                            ///< Container of vectors
  using vectorRefSet = typename std::vector<std::reference_wrapper<T>>; ///< Container of vectors
  using constVectorRefSet = typename std::vector<std::reference_wrapper<const T>>; ///< Container of vectors
  using vectorP = typename std::vector<value_type>;                                ///< P-space parameters
  using vectorRefSetP = typename std::vector<std::reference_wrapper<vectorP>>;     ///< Container of P-space parameters
  using constVectorRefSetP =
      typename std::vector<std::reference_wrapper<const vectorP>>; ///< Container of P-space parameters
  using vectorSetP = typename std::vector<vectorP>;                ///< Container of P-space parameters
public:
  using scalar_type =
      decltype(std::declval<T>().dot(std::declval<const T&>())); ///< The type of scalar products of vectors
  std::shared_ptr<molpro::Profiler> m_profiler;
  /*!
   * \brief Take, typically, a current solution and residual, and return new solution.
   * In the context of Lanczos-like linear methods, the input will be a current expansion vector and the result of
   * acting on it with the matrix, and the output will be a new expansion vector.
   * For non-linear equations, the input will be the current solution and residual, and the output the interpolated
   * solution and residual. \param parameters On input, the current solution or expansion vector. On exit, the
   * interpolated solution vector. \param action On input, the residual for parameters (non-linear), or action of matrix
   * on parameters (linear). On exit, the expected (non-linear) or actual (linear) residual of the interpolated
   * parameters. \param parametersP On exit, the interpolated solution projected onto the P space. \param other Optional
   * additional vectors that should be interpolated like the residual. corresponding element of other contains data to
   * be used. \return whether it is expected that the client should make an update, based on the returned parameters and
   * residual, before the subsequent call to endIteration()
   */
  int addVector(vectorRefSet parameters, vectorRefSet action, vectorRefSetP parametersP = nullVectorRefSetP<T>,
                vectorRefSet other = nullVectorRefSet<T>) {
    //    m_active.resize(parameters.size(), true);
    if (m_roots < 1)
      m_roots = parameters.size();                      // number of roots defaults to size of parameters
    if (m_qspace.size() == 0 and m_working_set.empty()) // initial
      for (auto i = 0; i < parameters.size(); i++)
        m_working_set.push_back(i);
    //    molpro::cout << "m_working_set size " << m_working_set.size() << std::endl;
    if (m_working_set.size() == 0)
      return 0;
    assert(parameters.size() >= m_working_set.size());
    assert(parameters.size() == action.size());
    m_iterations++;
    m_current_r.clear();
    m_current_v.clear();
    for (size_t k = 0; k < m_working_set.size(); k++) {
      if (m_residual_eigen) { // scale to roughly unit length for homogeneous equations in case the update has produced
                              // a very large vector in response to degeneracy
        auto s = parameters[k].get().dot(parameters[k]);
        if (std::abs(s - 1) > 1e-3) {
          parameters[k].get().scal(1 / std::sqrt(s));
          action[k].get().scal(1 / std::sqrt(s));
        }
      }
      m_current_r.emplace_back(parameters[k].get());
      m_current_v.emplace_back(action[k].get());
    }
    if (not m_last_d.empty()) {
      assert(m_last_d.size() == m_working_set.size());
      assert(m_last_hd.size() == m_working_set.size());
      for (size_t k = 0; k < m_working_set.size(); k++) {
        m_qspace.add(parameters[k], action[k], m_last_d[k], m_last_hd[k], m_rhs, m_subspaceMatrixResRes,
                     m_orthogonalise_Q);
      }
      m_last_d.clear();
      m_last_hd.clear();
    }
    // TODO this generates another read for the q space which could perhaps be avoided
    for (auto a = 0; a < m_qspace.size(); a++) {
      m_s_qr[a] = std::vector<scalar_type>(m_working_set.size());
      m_h_qr[a] = std::vector<scalar_type>(m_working_set.size());
      m_hh_qr[a] = std::vector<scalar_type>(m_working_set.size());
      m_h_rq[a] = std::vector<scalar_type>(m_working_set.size());
      const auto& qa = m_qspace[a];
      const auto& qha = m_qspace.action(a);
      for (size_t m = 0; m < m_working_set.size(); m++) {
        m_s_qr[a][m] = parameters[m].get().dot(m_qspace[a]);
        m_h_qr[a][m] = action[m].get().dot(m_subspaceMatrixResRes ? m_qspace.action(a) : m_qspace[a]);
        m_hh_qr[a][m] = action[m].get().dot(m_qspace.action(a));
        m_h_rq[a][m] = m_hermitian ? m_h_qr[a][m]
                                   : (m_subspaceMatrixResRes ? action : parameters)[m].get().dot(m_qspace.action(a));
        //        molpro::cout << "a=" << a << ", m=" << m << ", m_s_qr " << m_s_qr[a][m] << ", m_h_qr " << m_h_qr[a][m]
        //                     << ", m_h_rq " << m_h_rq[a][m] << std::endl;
      }
    }
    m_s_pr.clear();
    m_h_pr.clear();
    m_h_rp.clear();
    for (auto p = 0; p < m_pspace.size(); p++) {
      m_s_pr.push_back(std::vector<scalar_type>(m_working_set.size()));
      m_h_pr.push_back(std::vector<scalar_type>(m_working_set.size()));
      m_h_rp.push_back(std::vector<scalar_type>(m_working_set.size()));
      for (size_t k = 0; k < m_working_set.size(); k++) {
        m_s_pr[p][k] = parameters[k].get().dot(m_pspace[p]);
        m_h_pr[p][k] = m_h_rp[p][k] = action[k].get().dot(m_pspace[p]);
      }
    }
    m_s_rr.clear();
    m_h_rr.clear();
    m_hh_rr.clear();
    m_rhs_r.clear();
    for (auto m = 0; m < m_working_set.size(); m++) {
      m_rhs_r.push_back(std::vector<scalar_type>(m_rhs.size()));
      m_s_rr.push_back(std::vector<scalar_type>(m_working_set.size()));
      m_h_rr.push_back(std::vector<scalar_type>(m_working_set.size()));
      m_hh_rr.push_back(std::vector<scalar_type>(m_working_set.size()));
      for (size_t rhs = 0; rhs < m_rhs.size(); rhs++)
        m_rhs_r[m][rhs] = parameters[m].get().dot(m_rhs[rhs]);
      for (size_t n = 0; n < m_working_set.size(); n++) {
        m_s_rr[m][n] = parameters[n].get().dot(parameters[m].get());
        m_h_rr[m][n] = action[n].get().dot((m_subspaceMatrixResRes ? action[m] : parameters[m]).get());
        m_hh_rr[m][n] = action[n].get().dot(action[m].get());
      }
    }

#ifdef TIMING
    auto startTiming = std::chrono::steady_clock::now();
#endif
    buildSubspace();
#ifdef TIMING
    auto endTiming = std::chrono::steady_clock::now();
    std::cout << " addVector buildSubspace():  seconds="
              << std::chrono::duration_cast<std::chrono::nanoseconds>(endTiming - startTiming).count() * 1e-9
              << std::endl;
    startTiming = endTiming;
#endif
    solveReducedProblem();
#ifdef TIMING
    endTiming = std::chrono::steady_clock::now();
    std::cout << " addVector solveReducedProblem():  seconds="
              << std::chrono::duration_cast<std::chrono::nanoseconds>(endTiming - startTiming).count() * 1e-9
              << std::endl;
    startTiming = endTiming;
#endif
    //    molpro::cout << "update=" << update << std::endl;
    //    calculateResidualConvergence();
    // move any newly-converged solutions into the q space
    //      molpro::cout << "errors after calculateResidualConvergence()";
    //      for (const auto& e : m_errors)
    //        molpro::cout << " " << e;
    //      molpro::cout << std::endl;
    // evaluate all residuals
    // TODO make this more efficient by doing action just once, and not, maybe, for converged roots
    if (m_roots > parameters.size()) // TODO remove this restriction
      throw std::runtime_error("Cannot yet work with buffer smaller than number of roots");
    m_errors.resize(m_roots);
    m_working_set.clear();
    for (auto root = 0; root < m_roots; root++)
      m_working_set.push_back(root);
    if (m_linear)
      doInterpolation(parameters, action, parametersP, other, false);
    //    m_errors.assign(m_roots, 1e10); // TODO not right for retired roots
    for (auto k = 0; k < m_working_set.size(); k++)
      m_errors[m_working_set[k]] = std::sqrt(action[k].get().dot(action[k]));
    //    molpro::cout << "m_interpolation:\n" << m_interpolation << std::endl;

    doInterpolation(parameters, action, parametersP, other, true);
    m_last_d.clear();
    m_last_hd.clear();
    //    molpro::cout << "working set size "<<m_working_set.size()<<std::endl;
    //    molpro::cout << "m_thresh "<<m_thresh<<std::endl;
    for (int k = 0; k < m_working_set.size(); k++) {
      auto root = m_working_set[k];
      //      molpro::cout << "k=" << k << ", root=" << root << ", error=" << m_errors[root] << std::endl;
      if (m_linear and m_errors[root] < m_thresh and m_q_solutions.count(root) == 0) { // converged just now
        if (m_verbosity > 1)
          molpro::cout << "selecting root " << root << " for adding converged solution to Q space at position"
                       << m_qspace.size() << std::endl;
        m_qspace.add(parameters[k], action[k], m_rhs, m_subspaceMatrixResRes);
        m_q_solutions[m_working_set[k]] = m_qspace.keys().back();
      }
      if (m_linear and m_errors[root] < m_thresh) { // converged
        //        molpro::cout << "  remove this vector from the working set"<<std::endl;
        //  remove this vector from the working set
        for (auto kp = k + 1; kp < m_working_set.size(); kp++) {
          parameters[kp - 1].get() = parameters[kp].get();
          action[kp - 1].get() = action[kp].get();
          m_working_set[kp - 1] = m_working_set[kp];
        }
        m_working_set.pop_back();
        k--;
        //        molpro::cout << "k now = "<<k<<std::endl;
        //        molpro::cout << "m_working_set revised to ";for (const auto& w : m_working_set) molpro::cout <<"
        //        "<<w;molpro::cout <<std::endl;
      } else { // unconverged
               //        molpro::cout << "unconverged"<<std::endl;
        m_last_d.emplace_back(parameters[k]);
        m_last_hd.emplace_back(action[k]);
      }
    }
    //    molpro::cout << "working set size "<<m_working_set.size()<<std::endl;
    //    molpro::cout << "m_last_d size "<<m_last_d.size()<<std::endl;
    assert(m_last_d.size() == m_working_set.size());

    // re-establish the residual
    // TODO make more efficient
    doInterpolation(parameters, action, parametersP, other, false);
    if (m_nullify_solution_before_update) {
      m_last_d.clear();
      m_last_hd.clear();
      for (auto k = 0; k < m_working_set.size(); k++) {
        parameters[k].get().scal(0);
        m_last_d.emplace_back(m_current_r[k]);
        m_last_hd.emplace_back(m_current_v[k]);
      }
    }
    m_current_r.clear();
    m_current_v.clear();
    return m_working_set.size();
  }
  int addVector(std::vector<T>& parameters, std::vector<T>& action, vectorSetP& parametersP = nullVectorSetP<T>,
                std::vector<T>& other = nullStdVector<T>) {
    return addVector(vectorRefSet(parameters.begin(), parameters.end()), vectorRefSet(action.begin(), action.end()),
                     vectorRefSetP(parametersP.begin(), parametersP.end()), vectorRefSet(other.begin(), other.end()));
  }
  int addVector(T& parameters, T& action, vectorP& parametersP, T& other) {
    return addVector(vectorRefSet(1, parameters), vectorRefSet(1, action), vectorRefSetP(1, parametersP),
                     vectorRefSet(1, other));
  }
  int addVector(T& parameters, T& action, vectorP& parametersP = nullVectorP<T>) {
    // T other;
    return addVector(vectorRefSet(1, parameters), vectorRefSet(1, action), vectorRefSetP(1, parametersP)
                     //   vectorRefSet(1, other)
    );
  }

  /*!
   * \brief Take a current solution, objective function value and residual, and return new solution.
   * \param parameters On input, the current solution. On exit, the interpolated solution vector.
   * \param value The value of the objective function for parameters.
   * \param action On input, the residual for parameters. On exit, the expected (non-linear) residual of the
   * interpolated parameters. \return whether it is expected that the client should make an update, based on the
   * returned parameters and residual, before the subsequent call to endIteration()
   */
  int addValue(T& parameters, scalar_type value, T& action) {
    m_values.push_back(value);
    //    std::cout << "m_values resized to " << m_values.size() << " and filled with " << value
    //              << std::endl;
    return this->addVector(parameters, action);
  }

public:
  /*!
   * \brief Specify a P-space vector as a sparse combination of parameters. The container holds a number of segments,
   * each characterised by an offset in the full space, and a vector of coefficients starting at that offset.
   */
  using Pvector = std::map<size_t, scalar_type>;

  /*!
   * \brief Add P-space vectors to the expansion set for linear methods.
   * \param Pvectors the vectors to add
   * \param PP Matrix projected onto the existing+new, new P space. It should be provided as a
   * 1-dimensional array, with the existing+new index running fastest.
   * \param parameters On exit, the interpolated solution vector.
   * \param action  On exit, the  residual of the interpolated Q parameters.
   * The contribution from the new, and any existing, P parameters is missing, and should be added in subsequently.
   * \param parametersP On exit, the interpolated solution projected onto the P space.
   * \param other On exit, interpolation of the other vectors
   * \return The number of vectors contained in parameters, action, parametersP, other
   */
  int addP(std::vector<Pvector> Pvectors, const scalar_type* PP, vectorRefSet parameters, vectorRefSet action,
           vectorRefSetP parametersP, vectorRefSet other = nullVectorRefSet<T>) {
    m_pspace.add(Pvectors, PP, m_rhs);
    m_qspace.refreshP(action.front());
    //    auto oldss = m_subspaceMatrix.rows();
    m_active.resize(parameters.size(), true);
    //        molpro::cout << "oldss " << oldss << ", Pvectors,size() " << Pvectors.size() << std::endl;
    //    m_subspaceMatrix.conservativeResize(oldss + Pvectors.size(), oldss + Pvectors.size());
    //    m_subspaceOverlap.conservativeResize(oldss + Pvectors.size(), oldss + Pvectors.size());
    //    Eigen::Index oldNP = m_PQMatrix.rows();
    //    Eigen::Index newNP = oldNP + Pvectors.size();
    //    assert(newNP + m_QQMatrix.rows() == m_subspaceOverlap.rows());
    //    m_PQMatrix.conservativeResize(newNP, m_QQMatrix.rows());
    //    m_PQOverlap.conservativeResize(newNP, m_QQMatrix.rows());
    //    m_subspaceRHS.conservativeResize(newNP, m_rhs.size());
    //    size_t offset = 0;
    //    molpro::cout << "addP PP\n";
    //    for (auto i = 0; i < Pvectors.size(); i++)
    //      for (auto j = 0; j < newNP; j++)
    //        molpro::cout << i << " " << j << " " << PP[offset++]<<std::endl;
    //    offset = 0;
    //    for (size_t n = 0; n < Pvectors.size(); n++)
    //      m_Pvectors.push_back(Pvectors[n]);
    //    for (size_t n = 0; n < Pvectors.size(); n++) {
    //      for (Eigen::Index i = 0; i < newNP; i++) {
    //        molpro::cout << "offset " << offset << std::endl;
    //        molpro::cout << "PP " << PP[offset] << std::endl;
    //        m_subspaceMatrix(oldNP + n, i) = m_subspaceMatrix(i, oldNP + n) = PP[offset++];
    //        double overlap = 0;
    //        molpro::cout << "n=" << n << ", i=" << i << std::endl;
    //        for (const auto& p : Pvectors[n]) {
    //          molpro::cout << "addP Pvector=" << p.first << " : " << p.second << " " <<
    //          m_Pvectors[i].count(p.first) << std::endl;
    //          if (m_Pvectors[i].count(p.first))
    //            overlap += p.second * m_Pvectors[i][p.first];
    //        }
    //        molpro::cout << "overlap: " << overlap << std::endl;
    //        molpro::cout << "oldNP+n=" << oldNP + n << ", i=" << i << ", dimensions: " << m_subspaceOverlap.rows()
    //        << ", "
    //             << m_subspaceOverlap.cols() << std::endl;
    //        m_subspaceOverlap(oldNP + n, i) = m_subspaceOverlap(i, oldNP + n) = overlap;
    //        molpro::cout << "stored " << m_subspaceOverlap(oldNP + n, i) << std::endl;
    //      }
    //    }
    //    size_t l = 0;
    //    for (size_t ll = 0; ll < m_solutions.size(); ll++) {
    //      for (size_t lll = 0; lll < m_solutions[ll].size(); lll++) {
    //        if (m_vector_active[ll][lll]) {
    //          for (size_t n = 0; n < Pvectors.size(); n++) {
    //            m_PQMatrix(oldNP + n, l) = m_residuals[ll][lll].dot(Pvectors[n]);
    //            m_PQOverlap(oldNP + n, l) = m_solutions[ll][lll].dot(Pvectors[n]);
    //          }
    //          l++;
    //        }
    //      }
    //    }
    //    for (size_t n = 0; n < Pvectors.size(); n++) {
    //      for (size_t l = 0; l < m_rhs.size(); l++) {
    //        m_subspaceRHS(oldNP + n, l) = m_rhs[l].dot(Pvectors[n]);
    //      }
    //    }
    buildSubspace();
    solveReducedProblem();
    doInterpolation(parameters, action, parametersP, other);
    //    for (auto ll = 0; ll < parameters.size(); ll++)
    //      molpro::cout << " after doInterpolation, parameters=" << parameters[ll]<< std::endl;
    //      molpro::cout << " after doInterpolation, g.w=" << parameters[ll]->dot(*action[ll]) << std::endl;
    return parameters.size(); // TODO temporary
  }
  int addP(std::vector<Pvector> Pvectors, const scalar_type* PP, std::vector<T>& parameters, std::vector<T>& action,
           vectorSetP& parametersP, std::vector<T>& other = nullStdVector<T>) {
    return addP(Pvectors, PP, vectorRefSet(parameters.begin(), parameters.end()),
                vectorRefSet(action.begin(), action.end()), vectorRefSetP(parametersP.begin(), parametersP.end()),
                vectorRefSet(other.begin(), other.end()));
  }
  int addP(Pvector Pvectors, const scalar_type* PP, T& parameters, T& action, vectorP& parametersP,
           T& other = nullStdVector<T>) {
    return addP(Pvectors, PP, vectorRefSet(1, parameters), vectorRefSet(1, action), vectorRefSetP(1, parametersP),
                vectorRefSet(1, other));
  }

  /*!
   * \brief Remove completely the whole P space
   */
  void clearP() {
    throw std::logic_error("clearP no longer implemented");
    //    m_subspaceMatrix.conservativeResize(m_QQMatrix.rows(), m_QQMatrix.rows());
    //    m_subspaceOverlap.conservativeResize(m_QQMatrix.rows(), m_QQMatrix.rows());
    //    m_PQMatrix.conservativeResize(0, m_QQMatrix.rows());
    //    m_PQOverlap.conservativeResize(0, m_QQMatrix.rows());
    //    m_subspaceRHS.conservativeResize(0, m_rhs.size());
    //    m_Pvectors.clear();
  }

  /*!
   * \brief For most solvers, this function does nothing but report, but the exception is Optimize.
   * Also write progress to molpro::cout.
   * \param solution The current
   * solution, after interpolation and updating with the preconditioned residual.
   * \param residual The residual after interpolation.
   * \return Whether convergence has been reached
   */
  virtual bool endIteration(vectorRefSet solution, constVectorRefSet residual) {
    report();
    return m_errors.empty() ? 0 : *std::max_element(m_errors.cbegin(), m_errors.cend()) < m_thresh;
  }
  virtual bool endIteration(std::vector<T>& solution, const std::vector<T>& residual) {
    return endIteration(vectorRefSet(solution.begin(), solution.end()),
                        constVectorRefSet(residual.begin(), residual.end()));
  }
  virtual bool endIteration(T& solution, const T& residual) {
    return endIteration(vectorRefSet(1, solution), constVectorRefSet(1, residual));
  }

  /*!
   * @brief Whether the expansion vector for a particular root is still active, ie not yet converged, and residual has
   * meaning
   * @param root
   * @return
   */
  bool active(int root) { return m_active[root]; }
  std::vector<bool> active() { return m_active.empty() ? std::vector<bool>(1000, true) : m_active; }

  void solution(const std::vector<int>& roots, vectorRefSet parameters, vectorRefSet residual,
                vectorRefSetP parametersP = nullVectorRefSetP<T>) {
    auto working_set_save = m_working_set;
    m_working_set = roots;
    auto other = nullVectorRefSet<T>;
    m_s_rr.clear();
    buildSubspace();
    solveReducedProblem();
    doInterpolation(parameters, residual, parametersP, other);
    m_working_set = working_set_save;
  }
  void solution(const std::vector<int>& roots, std::vector<T>& parameters, std::vector<T>& residual,
                vectorSetP& parametersP = nullVectorSetP<T>) {
    return solution(roots, vectorRefSet(parameters.begin(), parameters.end()),
                    vectorRefSet(residual.begin(), residual.end()),
                    vectorRefSetP(parametersP.begin(), parametersP.end()));
  }
  void solution(int root, T& parameters, T& residual, vectorP& parametersP = nullVectorP<T>) {
    return solution(std::vector<int>(1, root), vectorRefSet(1, parameters), vectorRefSet(1, residual),
                    vectorRefSetP(1, parametersP));
  }

  /*!
   * \brief Get the solver's suggestion of which degrees of freedom would be best
   * to add to the P-space.
   * \param solution Current solution
   * \param residual Current residual
   * \param maximumNumber Suggest no more than this number
   * \param threshold Suggest only axes for which the current residual and update
   * indicate an energy improvement in the next iteration of this amount or more.
   * \return
   */
  std::vector<size_t> suggestP(constVectorRefSet solution, constVectorRefSet residual,
                               const size_t maximumNumber = 1000, const scalar_type threshold = 0) {
    std::map<size_t, scalar_type> result;
    for (size_t kkk = 0; kkk < solution.size(); kkk++) {
      //    molpro::cout << "suggestP kkk "<<kkk<<" active "<<solution.m_vector_active[kkk]<<maximumNumber<<std::endl;
      if (m_active[kkk]) {
        std::vector<size_t> indices;
        std::vector<scalar_type> values;
        std::tie(indices, values) = solution[kkk].get().select(residual[kkk], maximumNumber, threshold);
        //     molpro::cout <<"indices.size()="<<indices.size()<<std::endl;
        //     for (auto k=0; k<indices.size(); k++) molpro::cout << "select "<< indices[k] <<" :
        //     "<<values[k]<<std::endl;
        for (size_t i = 0; i < indices.size(); i++)
          if (result.count(indices[i]))
            result[indices[i]] = std::max(result[indices[i]], values[i]);
          else
            result[indices[i]] = values[i];
      }
    }
    // sort and select
    //   for (const auto& kv : result) molpro::cout << "result: " << kv.first << " : " <<kv.second<<std::endl;
    std::multimap<scalar_type, size_t, std::greater<scalar_type>> inverseResult;
    for (const auto& kv : result)
      inverseResult.insert(std::pair<scalar_type, size_t>(kv.second, kv.first));
    //   for (const auto& kv : inverseResult) molpro::cout << "inverseResult: " << kv.first << " : "
    //   <<kv.second<<std::endl;
    std::vector<size_t> indices;
    //   std::vector<T> values;
    size_t k = 0;
    for (auto p = inverseResult.cbegin(); p != inverseResult.cend() && k < maximumNumber; k++) {
      indices.push_back(p->second); // values.push_back(p->first);
      ++p;
    }
    //   for (auto k=0; k<indices.size(); k++) molpro::cout << "suggest P "<< indices[k] <<" : "<<values[k]<<std::endl;
    return indices;
  }
  std::vector<size_t> suggestP(const std::vector<T>& solution, const std::vector<T>& residual,
                               const size_t maximumNumber = 1000, const scalar_type threshold = 0) {
    return suggestP(constVectorRefSet(solution.begin(), solution.end()),
                    constVectorRefSet(residual.begin(), residual.end()), maximumNumber, threshold);
  }

  /*!
   * \brief Set convergence threshold
   */
  void setThresholds(scalar_type thresh) { m_thresh = thresh; }

  unsigned int iterations() const { return m_iterations; } //!< How many iterations have occurred

  std::vector<scalar_type> eigenvalues() const ///< The calculated eigenvalues of m_subspaceMatrix
  {
    std::vector<scalar_type> result;
    for (size_t root = 0; root < (size_t)m_roots && root < (size_t)m_subspaceEigenvalues.rows(); root++)
      result.push_back(m_subspaceEigenvalues[root].real());
    return result;
  }

  /*!
   * @brief The roots that are currently being tracked
   * @return
   */
  const std::vector<int>& working_set() const { return m_working_set; }

  std::vector<scalar_type>
  working_set_eigenvalues() const ///< The calculated eigenvalues of m_subspaceMatrix belonging to the working set
  {
    std::vector<scalar_type> result;
    for (const auto& root : m_working_set)
      result.push_back(m_subspaceEigenvalues[root].real());
    return result;
  }

  std::vector<scalar_type> errors() const { return m_errors; } //!< Error at last iteration

  size_t dimensionP() const { return (size_t)m_PQMatrix.rows(); } //!< Size of P space

protected:
  Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> m_PQMatrix, m_PQOverlap; //!< The PQ block of the matrix
  std::vector<Pvector> m_Pvectors;
  std::vector<bool> m_active; ///< whether each expansion vector is currently active
public:
  int m_verbosity; //!< How much to print. Zero means nothing; One results in a single progress-report line printed each
                   //!< iteration.
  scalar_type m_thresh; //!< If residual . residual is less than this, converged.
protected:
  unsigned int m_actions; //!< number of action vectors provided
public:
  unsigned int m_maxIterations; //!< Maximum number of iterations
  unsigned int m_minIterations; //!< Minimum number of iterations
  bool m_linear;                ///< Whether residuals are linear functions of the corresponding expansion vectors.
  bool m_hermitian; ///< Whether residuals can be assumed to be the action of an underlying self-adjoint operator.
  size_t
      m_roots; ///< How many roots to calculate / equations to solve (defaults to size of solution and residual vectors)
  bool m_rspt;

public:
  optionMap m_options; ///< A string of options to be interpreted by solveReducedProblem().
  ///< Possibilities include
  ///< - m_options["convergence"]=="energy" (the default), meaning that m_errors() returns the predicted eigenvalue
  ///< change in the next iteration, ie the scalar product of the step and the residual
  ///< - m_options["convergence"]=="step": m_errors() returns the norm of the step in the solution
  ///< - m_options["convergence"]=="residual": m_errors() returns the norm of the residual vector
protected:
  Q<T> m_qspace;
  P<value_type, scalar_type> m_pspace;
  std::vector<slowvector> m_last_d;    ///< optimum solution in last iteration
  std::vector<slowvector> m_last_hd;   ///< action vector corresponding to optimum solution in last iteration
  std::vector<slowvector> m_current_r; ///< current working space TODO can probably eliminate using m_last_d
  std::vector<slowvector> m_current_v; ///< action vector corresponding to current working space
  std::vector<std::vector<scalar_type>> m_q_scale_factors;
  std::vector<std::vector<scalar_type>> m_s_rr, m_h_rr, m_hh_rr, m_rhs_r;  ///< interactions within R space
  std::map<int, std::vector<scalar_type>> m_s_qr, m_h_qr, m_h_rq, m_hh_qr; ///< interactions between R and Q spaces
  std::vector<std::vector<scalar_type>> m_s_pr, m_h_pr, m_h_rp;            ///< interactions between R and P spaces
  mutable std::vector<int> m_working_set; ///< which roots are being tracked in the working set
  std::map<int, int> m_q_solutions;       ///< key of q space vector of a converged solution
  double
      m_threshold_residual_recalculate; ///< if the length of a residual comes in lower than this in the subspace-based
  ///< calculation, it will be recalculated with the full residual
  bool m_exclude_r_from_redundancy_test;
  bool m_orthogonalise_Q; //!< whether Q-space vectors constructed by difference should be orthogonal to the working
                          //!< vector, or the pure difference with the previous vector

public:
  /*!
   * @brief Report the number of action vectors introduced so far.
   * @return
   */
  int actions() { return m_actions; }

  virtual void report() const {
    if (m_verbosity > 0) {
      molpro::cout << "iteration " << iterations();
      if (not m_values.empty())
        molpro::cout << ", " << m_value_print_name << " = " << m_values.back();
      if (this->m_roots > 1)
        molpro::cout << ", error[" << std::max_element(m_errors.cbegin(), m_errors.cend()) - m_errors.cbegin()
                     << "] = ";
      else
        molpro::cout << ", error = ";
      molpro::cout << *std::max_element(m_errors.cbegin(), m_errors.cend()) << std::endl;
    }
  }

protected:
  virtual bool solveReducedProblem() = 0;

  int propose_singularity_deletion(size_t n, size_t ndim, const scalar_type* m,
                                   const std::vector<int>& candidates = std::vector<int>(),
                                   scalar_type threshold = 1e-10) {
    //    Eigen::EigenSolver<Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> > ss(m_subspaceMatrix);
    //    molpro::cout << "eigenvalues "<<ss.eigenvalues()<<std::endl;
    Eigen::Map<const Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic>> singularTester_(m, ndim, ndim);
    auto singularTester = singularTester_.block(0, 0, n, n);
    Eigen::JacobiSVD<Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic>> svd(singularTester,
                                                                                     Eigen::ComputeThinV);
    //    molpro::cout << "propose_singularity_deletion threshold=" << threshold << std::endl;
    //        molpro::cout << "matrix:\n" << singularTester << std::endl;
    //        molpro::cout << "singular values:\n" << svd.singularValues().transpose() << std::endl;
    //        molpro::cout << "V:\n" << svd.matrixV() << std::endl;
    //        molpro::cout << "candidates:";
    //        for (const auto& c : candidates)
    //          molpro::cout << " " << c;
    //        molpro::cout << std::endl;
    auto sv = svd.singularValues();
    std::vector<scalar_type> svv;
    for (auto k = 0; k < n; k++)
      svv.push_back(sv(k));
    auto most_singular = std::min_element(svv.begin(), svv.end()) - svv.begin();
    //        molpro::cout << "most_singular " << most_singular << std::endl;
    if (svv[most_singular] > threshold)
      return -1;
    for (const auto& k : candidates) {
      //      if (std::fabs(svd.matrixV()(k, most_singular)) > 1e-3)
      //        molpro::cout << "taking candidate " << k << ": " << svd.matrixV()(k, most_singular) << std::endl;
      if (std::fabs(svd.matrixV()(k, most_singular)) > 1e-3)
        return k;
    }
    return -1;
  }

  void buildSubspace() {
    const size_t nP = m_pspace.size();
    const size_t nQ = m_qspace.size();
    const size_t nR = m_s_rr.size();
    const size_t nX = nP + nQ + nR;
    const auto oP = 0;
    const auto oQ = oP + nP;
    const auto oR = oQ + nQ;
    //    molpro::cout << "buildSubspace nP=" << nP << ", nQ=" << nQ << ", nR=" << nR << std::endl;
    m_subspaceMatrix.conservativeResize(nX, nX);
    m_subspaceOverlap.conservativeResize(nX, nX);
    m_subspaceRHS.resize(nX, m_rhs.size());
    for (size_t a = 0; a < nQ; a++) {
      for (size_t rhs = 0; rhs < m_rhs.size(); rhs++)
        m_subspaceRHS(oQ + a, rhs) = m_qspace.rhs(a)[rhs];
      for (size_t b = 0; b < nQ; b++) {
        m_subspaceMatrix(oQ + b, oQ + a) = m_qspace.action(b, a);
        m_subspaceOverlap(oQ + b, oQ + a) = m_qspace.metric(b, a);
      }
      const auto& metric_pspace = m_qspace.metric_pspace(a);
      const auto& action_pspace = m_qspace.action_pspace(a);
      for (size_t i = 0; i < nP; i++) {
        m_subspaceMatrix(oP + i, oQ + a) = m_subspaceMatrix(oQ + a, oP + i) = action_pspace[i];
        m_subspaceOverlap(oP + i, oQ + a) = m_subspaceOverlap(oQ + a, oP + i) = metric_pspace[i];
      }
      for (size_t m = 0; m < nR; m++) {
        m_subspaceMatrix(oR + m, oQ + a) = m_h_rq[a][m];
        m_subspaceMatrix(oQ + a, oR + m) = m_h_qr[a][m];
        m_subspaceOverlap(oR + m, oQ + a) = m_s_qr[a][m];
        m_subspaceOverlap(oQ + a, oR + m) = m_s_qr[a][m];
      }
    }
    for (size_t i = 0; i < nP; i++) {
      for (size_t m = 0; m < nR; m++) {
        m_subspaceMatrix(oR + m, oP + i) = m_h_rp[i][m];
        m_subspaceMatrix(oP + i, oR + m) = m_h_pr[i][m];
        m_subspaceOverlap(oR + m, oP + i) = m_s_pr[i][m];
        m_subspaceOverlap(oP + i, oR + m) = m_s_pr[i][m];
      }
    }
    for (size_t n = 0; n < nR; n++) {
      for (size_t rhs = 0; rhs < m_rhs.size(); rhs++)
        m_subspaceRHS(oR + n, rhs) = m_rhs_r[n][rhs];
      for (size_t m = 0; m < nR; m++) {
        m_subspaceMatrix(oR + m, oR + n) = m_h_rr[m][n];
        m_subspaceOverlap(oR + m, oR + n) = m_s_rr[m][n];
      }
    }
    if (m_subspaceMatrixResRes)
      m_subspaceOverlap = m_subspaceMatrix;
    if (nQ > 0) {
      const auto& singularTester = m_residual_eigen ? m_subspaceOverlap : m_subspaceMatrix;
      std::vector<int> candidates;
      std::map<int, int> solutions_q; // map from current q space to roots
      for (const auto& x :
           m_q_solutions) // loop over all roots that are associated with a q vector. x.first=root, x.second=qindex
        for (auto k = 0; k < m_qspace.keys().size(); k++)
          if (m_qspace.keys()[k] == x.second)
            solutions_q[k] = x.first; // TODO check logic
      for (auto a = 0; a < nQ; a++)
        if (solutions_q.count(a) == 0)
          candidates.push_back(oQ + a);
      //      molpro::cout << "singularTester:\n" << singularTester << std::endl;
      //      molpro::cout << "candidates:";
      //      for (const auto& c : candidates)
      //        molpro::cout << " " << c;
      //      molpro::cout << std::endl;
      auto del =
          propose_singularity_deletion(m_exclude_r_from_redundancy_test ? nX - nR : nX, nX, &singularTester(0, 0),
                                       candidates, nQ > m_maxQ ? 1e6 : m_singularity_threshold);
      if (del >= 0) {
        if (m_verbosity > 2)
          molpro::cout << "del=" << del << "; remove Q" << del - oQ << std::endl;
        m_qspace.remove(del - oQ);
        m_errors.assign(m_roots, 1e20);
        for (auto m = 0; m < nR; m++)
          for (auto a = del - oQ; a < nQ - 1; a++) {
            m_h_rq[a][m] = m_h_rq[a + 1][m];
            m_h_qr[a][m] = m_h_qr[a + 1][m];
            m_s_qr[a][m] = m_s_qr[a + 1][m];
            m_hh_qr[a][m] = m_hh_qr[a + 1][m];
          }
        buildSubspace();
        return;
      }
    }
    if (m_verbosity > 1)
      molpro::cout << "nP=" << nP << ", nQ=" << nQ << ", nR=" << nR << std::endl;
    if (m_verbosity > 2) {
      molpro::cout << "Subspace matrix" << std::endl << this->m_subspaceMatrix << std::endl;
      molpro::cout << "Subspace overlap" << std::endl << this->m_subspaceOverlap << std::endl;
    }
  }

protected:
  void diagonalizeSubspaceMatrix() {
    auto kept = m_subspaceMatrix.rows();

    Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> H = m_subspaceMatrix.block(0, 0, kept, kept);
    Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> S = m_subspaceOverlap.block(0, 0, kept, kept);
//    molpro::cout << "diagonalizeSubspaceMatrix H:\n" << H.format(Eigen::FullPrecision) << std::endl;
//    molpro::cout << "diagonalizeSubspaceMatrix S:\n" << S.format(Eigen::FullPrecision) << std::endl;
//   Eigen::GeneralizedEigenSolver<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>> s(H, S);
#ifdef TIMING
    auto startTiming = std::chrono::steady_clock::now();
#endif
    for (auto k = 0; k < S.rows(); k++)
      if (std::abs(S(k, k) - 1) < 1e-15)
        S(k, k) = 1; // somehow avoid problems that eigen with Intel 18 get the SVD wrong if near-unit matrix
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(S, Eigen::ComputeThinU | Eigen::ComputeThinV);
    svd.setThreshold(m_svdThreshold);
    //    molpro::cout << "singular values of overlap " << svd.singularValues().transpose() << std::endl;
    //   auto Hbar = svd.solve(H);
    if (m_verbosity > 1 && svd.rank() < S.cols())
      molpro::cout << "SVD rank " << svd.rank() << " in subspace of dimension " << S.cols() << std::endl;
    if (m_verbosity > 2 && svd.rank() < S.cols())
      molpro::cout << "singular values " << svd.singularValues().transpose() << std::endl;
    auto svmh = svd.singularValues().head(svd.rank()).eval();
    for (auto k = 0; k < svd.rank(); k++)
      svmh(k) = 1 / std::sqrt(svmh(k));
#ifdef TIMING
    auto endTiming = std::chrono::steady_clock::now();
    molpro::cout << " construct svd and svmh:  seconds="
                 << std::chrono::duration_cast<std::chrono::nanoseconds>(endTiming - startTiming).count() * 1e-9
                 << std::endl;
    startTiming = std::chrono::steady_clock::now();
#endif
    auto Hbar = (svmh.asDiagonal()) * (svd.matrixU().leftCols(svd.rank()).adjoint()) * H *
                svd.matrixV().leftCols(svd.rank()) * (svmh.asDiagonal());
#ifdef TIMING
    endTiming = std::chrono::steady_clock::now();
    molpro::cout << " construct Hbar:  seconds="
                 << std::chrono::duration_cast<std::chrono::nanoseconds>(endTiming - startTiming).count() * 1e-9
                 << std::endl;
#endif
//   molpro::cout << "S\n"<<S<<std::endl;
//   molpro::cout << "S singular values"<<(Eigen::DiagonalMatrix<T, Eigen::Dynamic,
//   Eigen::Dynamic>(svd.singularValues().head(svd.rank())))<<std::endl; molpro::cout << "S inverse singular
//   values"<<Eigen::DiagonalMatrix<T, Eigen::Dynamic>(svd.singularValues().head(svd.rank())).inverse()<<std::endl;
//   molpro::cout << "S singular values"<<sv<<std::endl;
//   molpro::cout << "H\n"<<H<<std::endl;
//   molpro::cout << "Hbar\n"<<Hbar<<std::endl;
#ifdef TIMING
    molpro::cout << "symmetric Hbar? " << (Hbar - Hbar.adjoint()).norm() << std::endl;
    startTiming = std::chrono::steady_clock::now();
#endif
    Eigen::EigenSolver<Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic>> s(Hbar);
    //    molpro::cout << "s.eigenvectors()\n"<<s.eigenvectors()<<std::endl;
    m_subspaceEigenvalues = s.eigenvalues();
#ifdef TIMING
    endTiming = std::chrono::steady_clock::now();
    molpro::cout << " EigenSolver():  seconds="
                 << std::chrono::duration_cast<std::chrono::nanoseconds>(endTiming - startTiming).count() * 1e-9
                 << std::endl;
    startTiming = endTiming;
#endif
    if (s.eigenvalues().imag().norm() < 1e-10 and s.eigenvectors().imag().norm() < 1e-10) { // real eigenvectors
      m_subspaceEigenvectors = svd.matrixV().leftCols(svd.rank()) * svmh.asDiagonal() * s.eigenvectors().real();

    } else { // complex eigenvectors
#ifdef __INTEL_COMPILER
      molpro::cout << "Hbar\n" << Hbar << std::endl;
      molpro::cout << "Eigenvalues\n" << s.eigenvalues() << std::endl;
      molpro::cout << "Eigenvectors\n" << s.eigenvectors() << std::endl;
      throw std::runtime_error("Intel compiler does not support working with complex eigen3 entities properly");
#endif
      m_subspaceEigenvectors = svd.matrixV().leftCols(svd.rank()) * svmh.asDiagonal() * s.eigenvectors();
    }
    //   molpro::cout << "unsorted eigenvalues\n"<<m_subspaceEigenvalues<<std::endl;
    //   molpro::cout << "unsorted eigenvectors\n"<<m_subspaceEigenvectors<<std::endl;

    {
#ifdef TIMING
      auto startTiming = std::chrono::steady_clock::now();
#endif
      // sort
      auto eigval = m_subspaceEigenvalues;
      auto eigvec = m_subspaceEigenvectors;
      std::vector<size_t> map;
      for (Eigen::Index k = 0; k < Hbar.cols(); k++) {
        Eigen::Index ll;
        for (ll = 0; std::count(map.begin(), map.end(), ll) != 0; ll++)
          ;
        for (Eigen::Index l = 0; l < Hbar.cols(); l++) {
          if (std::count(map.begin(), map.end(), l) == 0) {
            if (eigval(l).real() < eigval(ll).real())
              ll = l;
          }
        }
        map.push_back(ll);
        m_subspaceEigenvalues[k] = eigval(ll);
        //    molpro::cout << "new sorted eigenvalue "<<k<<", "<<ll<<", "<<eigval(ll)<<std::endl;
        //    molpro::cout << eigvec.col(ll)<<std::endl;
        m_subspaceEigenvectors.col(k) = eigvec.col(ll);
      }
#ifdef TIMING
      auto endTiming = std::chrono::steady_clock::now();
      molpro::cout << " sort seconds="
                   << std::chrono::duration_cast<std::chrono::nanoseconds>(endTiming - startTiming).count() * 1e-9
                   << std::endl;
#endif
    }
//   molpro::cout << "sorted eigenvalues\n"<<m_subspaceEigenvalues<<std::endl;
//   molpro::cout << "sorted eigenvectors\n"<<m_subspaceEigenvectors<<std::endl;
//   molpro::cout << m_subspaceOverlap<<std::endl;
#ifdef TIMING
    startTiming = std::chrono::steady_clock::now();
#endif
    Eigen::MatrixXcd ovlTimesVec(m_subspaceEigenvectors.cols(), m_subspaceEigenvectors.rows()); // FIXME templating
    for (auto repeat = 0; repeat < 3; ++repeat)
      for (Eigen::Index k = 0; k < m_subspaceEigenvectors.cols(); k++) {
        if (std::abs(m_subspaceEigenvalues(k)) <
            1e-12) { // special case of zero eigenvalue -- make some real non-zero vector definitely in the null space
          m_subspaceEigenvectors.col(k).real() += double(0.3256897) * m_subspaceEigenvectors.col(k).imag();
          m_subspaceEigenvectors.col(k).imag().setZero();
        }
        if (m_hermitian)
          for (Eigen::Index l = 0; l < k; l++) {
            //        auto ovl =
            //            (m_subspaceEigenvectors.col(l).adjoint() * m_subspaceOverlap * m_subspaceEigenvectors.col(k))(
            //            0, 0); (ovlTimesVec.row(l) * m_subspaceEigenvectors.col(k))(0,0);
            //            ovlTimesVec.row(l).dot(m_subspaceEigenvectors.col(k));
            //        auto norm =
            //            (m_subspaceEigenvectors.col(l).adjoint() * m_subspaceOverlap * m_subspaceEigenvectors.col(l))(
            //                0,
            //                0);
            //      molpro::cout << "k="<<k<<", l="<<l<<", ovl="<<ovl<<" norm="<<norm<<std::endl;
            //      molpro::cout << m_subspaceEigenvectors.col(k).transpose()<<std::endl;
            //      molpro::cout << m_subspaceEigenvectors.col(l).transpose()<<std::endl;
            m_subspaceEigenvectors.col(k) -= m_subspaceEigenvectors.col(l) * // ovl;// / norm;
                                             ovlTimesVec.row(l).dot(m_subspaceEigenvectors.col(k));
            //        molpro::cout<<"immediately after projection " << k<<l<<" "<<
            //        (m_subspaceEigenvectors.col(l).adjoint() * m_subspaceOverlap * m_subspaceEigenvectors.col(k))( 0,
            //        0)<<std::endl;
          }
        //      for (Eigen::Index l = 0; l < k; l++) molpro::cout<<"after projection loop " << k<<l<<" "<<
        //      (m_subspaceEigenvectors.col(l).adjoint() * m_subspaceOverlap * m_subspaceEigenvectors.col(k))( 0,
        //      0)<<std::endl; molpro::cout <<
        //      "eigenvector"<<std::endl<<m_subspaceEigenvectors.col(k).adjoint()<<std::endl;
        auto ovl =
            //          (m_subspaceEigenvectors.col(k).adjoint() * m_subspaceOverlap *
            //          m_subspaceEigenvectors.col(k))(0,0);
            m_subspaceEigenvectors.col(k).adjoint().dot(m_subspaceOverlap * m_subspaceEigenvectors.col(k));
        m_subspaceEigenvectors.col(k) /= std::sqrt(ovl.real());
        ovlTimesVec.row(k) = m_subspaceEigenvectors.col(k).adjoint() * m_subspaceOverlap;
        //      for (Eigen::Index l = 0; l < k; l++)
        //      molpro::cout<<"after normalisation " << k<<l<<" "<< (m_subspaceEigenvectors.col(l).adjoint() *
        //      m_subspaceOverlap * m_subspaceEigenvectors.col(k))( 0, 0)<<std::endl; molpro::cout <<
        //      "eigenvector"<<std::endl<<m_subspaceEigenvectors.col(k).adjoint()<<std::endl;
        // phase
        Eigen::Index lmax = 0;
        for (Eigen::Index l = 0; l < m_subspaceEigenvectors.rows(); l++) {
          if (std::abs(m_subspaceEigenvectors(l, k)) > std::abs(m_subspaceEigenvectors(lmax, k)))
            lmax = l;
        }
        if (m_subspaceEigenvectors(lmax, k).real() < 0)
          m_subspaceEigenvectors.col(k) = -m_subspaceEigenvectors.col(k);
        //      for (Eigen::Index l = 0; l < k; l++)
        //      molpro::cout << k<<l<<" "<<
        //                       (m_subspaceEigenvectors.col(l).adjoint() * m_subspaceOverlap *
        //                       m_subspaceEigenvectors.col(k))( 0, 0)<<std::endl;
      }
#ifdef TIMING
    endTiming = std::chrono::steady_clock::now();
    molpro::cout << " repeat dimension=" << m_subspaceEigenvectors.cols() << ",  seconds="
                 << std::chrono::duration_cast<std::chrono::nanoseconds>(endTiming - startTiming).count() * 1e-9
                 << std::endl;
#endif
    //     molpro::cout << "eigenvalues"<<std::endl<<m_subspaceEigenvalues<<std::endl;
    //     molpro::cout << "eigenvectors"<<std::endl<<m_subspaceEigenvectors<<std::endl;
  }

  /*!
   * @brief form the combination of P, Q and R vectors to give the interpolated solution and corresponding residual (and
   * maybe other vectors). On entry, m_solution contains the interpolation
   *
   * @param solution On exit, the complete current solution (R, P and Q parts)
   * @param residual On exit, the R and Q contribution to the residual. The action of the matrix on the P solution is
   * missing, and has to be evaluated by the caller.
   * @param solutionP On exit, the solution projected to the P space
   * @param other On exit, interpolation of the other vectors
   * @param actionOnly If true, omit P space contribution and calculate action vector, not full residual
   */
  void doInterpolation(vectorRefSet solution, vectorRefSet residual, vectorRefSetP solutionP, vectorRefSet other,
                       bool actionOnly = false) const {
    for (auto& s : solution)
      s.get().scal(0);
    for (auto& s : residual)
      s.get().scal(0);
    for (auto& s : other)
      s.get().scal(0);
    auto nP = m_pspace.size();
    auto nR = m_current_r.size();
    //    auto nQ = m_qspace.size();
    auto nQ = this->m_interpolation.rows() - nP -
              nR; // guard against using any vectors added to the Q space since the subspace solution was evaluated
    assert(nQ <= m_qspace.size());
    auto oQ = nP;
    auto oR = oQ + nQ;
    assert(m_working_set.size() <= solution.size());
    assert(nP == 0 || solutionP.size() == residual.size());
    for (size_t kkk = 0; kkk < m_working_set.size(); kkk++) {
      auto root = m_working_set[kkk];
      //      molpro::cout << "working set k=" << kkk << " root=" << root << std::endl;
      if (nP > 0)
        solutionP[kkk].get().resize(nP);
      if (not actionOnly)
        for (size_t l = 0; l < nP; l++)
          solution[kkk].get().axpy((solutionP[kkk].get()[l] = this->m_interpolation(l, root)), m_pspace[l]);
      //      molpro::cout << "square norm of solution after P contribution " << solution[kkk]->dot(*solution[kkk]) <<
      //      std::endl;
      for (int q = 0; q < nQ; q++) {
        auto l = oQ + q;
        solution[kkk].get().axpy(this->m_interpolation(l, root), m_qspace[q]);
        residual[kkk].get().axpy(this->m_interpolation(l, root), m_qspace.action(q));
      }
      if (true) {
        for (int c = 0; c < nR; c++) {
          auto l = oR + c;
          solution[kkk].get().axpy(this->m_interpolation(l, root), m_current_r[c]);
          residual[kkk].get().axpy(this->m_interpolation(l, root), m_current_v[c]);
        }
        if (m_residual_eigen) {
          auto norm = solution[kkk].get().dot(solution[kkk].get());
          if (norm == 0)
            throw std::runtime_error("new solution has zero norm");
          solution[kkk].get().scal(1 / std::sqrt(norm));
          residual[kkk].get().scal(1 / std::sqrt(norm));
        }
        // TODO
      }
      if (not actionOnly and (m_residual_eigen || (m_residual_rhs && m_augmented_hessian > 0)))
        residual[kkk].get().axpy(-this->m_subspaceEigenvalues(root).real(), solution[kkk]);
      if (not actionOnly and m_residual_rhs)
        residual[kkk].get().axpy(-1, this->m_rhs[root]);
    }
  }

public:
  std::vector<scalar_type> m_errors; //!< Error at last iteration
  bool m_subspaceMatrixResRes; // whether m_subspaceMatrix is Residual.Residual (true) or Solution.Residual (false)
  bool m_residual_eigen;       // whether to subtract eigenvalue*solution when constructing residual
  bool m_residual_rhs;         // whether to subtract rhs when constructing residual
  // whether to use RSPT to construct solution instead of diagonalisation
  std::vector<vectorSet> m_residuals;
  std::vector<vectorSet> m_solutions;
  std::vector<vectorSet> m_others;
  std::vector<std::vector<bool>> m_vector_active;
  vectorSet m_rhs;
  size_t m_lastVectorIndex;
  std::vector<scalar_type> m_updateShift;
  Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic>
      m_interpolation; //!< The optimum combination of subspace vectors
  Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> m_subspaceMatrix;
  Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> m_subspaceOverlap;
  Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> m_subspaceRHS;
  Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic> m_subspaceGradient;
  Eigen::MatrixXcd m_subspaceSolution;     // FIXME templating
  Eigen::MatrixXcd m_subspaceEigenvectors; // FIXME templating
  Eigen::VectorXcd m_subspaceEigenvalues;  // FIXME templating
  std::vector<scalar_type> m_values;       //< function values
public:
  size_t m_dimension;
  std::string m_value_print_name; //< the title report() will give to the function value
protected:
  unsigned int m_iterations;
  scalar_type m_singularity_threshold;
  size_t m_added_vectors;          //!< number of vectors recently added to subspace
  scalar_type m_augmented_hessian; //!< The scale factor for augmented hessian solution of linear inhomogeneous systems.
                                   //!< Special values:
                                   //!< - 0: unmodified linear equations
                                   //!< - 1: standard augmented hessian

  bool m_nullify_solution_before_update;

public:
  scalar_type m_svdThreshold; ///< Threshold for singular-value truncation in linear equation solver.
  size_t m_maxQ;              //!< maximum size of Q space
protected:
};

template <class T>
typename T::scalar_type inline operator*(const typename IterativeSolver<T>::Pvector& a,
                                         const typename IterativeSolver<T>::Pvector& b) {
  typename T::scalar_type result = 0;
  for (const auto& aa : a)
    if (b.find(aa.first))
      result += aa.second * b[aa.first];
  return result;
}
} // namespace linalg
} // namespace molpro

namespace molpro {
namespace linalg {

/*! @example LinearEigensystemExample.cpp */
/*! @example LinearEigensystemExample-paged.cpp */
/*!
 * \brief A class that finds the lowest eigensolutions of a matrix using Davidson's method, i.e. preconditioned Lanczos
 *
 * Example of simplest use with a simple in-memory container for eigenvectors: @include LinearEigensystemExample.cpp
 *
 * Example using a P-space and offline distributed storage provided by the PagedVector class: @include
 * LinearEigensystemExample-paged.cpp
 *
 * \tparam scalar Type of matrix elements
 */
template <class T, class slowvector = T>
class LinearEigensystem : public IterativeSolver<T, slowvector> {
public:
  using typename IterativeSolver<T>::scalar_type;
  using typename IterativeSolver<T>::value_type;
  using IterativeSolver<T>::m_verbosity;

  /*!
   * \brief LinearEigensystem
   */
  explicit LinearEigensystem(std::shared_ptr<molpro::Profiler> profiler = nullptr) : IterativeSolver<T>(profiler) {
    this->m_residual_rhs = false;
    this->m_residual_eigen = true;
    this->m_linear = true;
  }

private:
  bool solveReducedProblem() override {
    if (this->m_rspt) {
      throw std::logic_error("RSPT not yet implemented");
    } else {
#ifdef TIMING
      auto start = std::chrono::steady_clock::now();
#endif
      this->diagonalizeSubspaceMatrix();
#ifdef TIMING
      auto end = std::chrono::steady_clock::now();
      std::cout << " diagonalizeSubspaceMatrix()"
                << ", seconds=" << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1e-9
                << std::endl;
#endif
      this->m_interpolation = this->m_subspaceEigenvectors
                                  .block(0, 0, this->m_subspaceEigenvectors.rows(),
                                         std::min(int(this->m_roots), int(this->m_subspaceEigenvectors.rows())))
                                  .real();
    }

    this->m_updateShift.resize(this->m_roots);
    for (size_t root = 0; root < (size_t)this->m_roots; root++)
      this->m_updateShift[root] = -(1 + std::numeric_limits<scalar_type>::epsilon()) *
                                  (static_cast<Eigen::Index>(root) < this->m_subspaceEigenvectors.rows()
                                       ? this->m_subspaceEigenvalues[root].real()
                                       : 0);
    return true;
  }

public:
  void report() const override {
    std::vector<scalar_type> ev = this->eigenvalues();
    if (m_verbosity > 0) {
      molpro::cout << "iteration " << this->iterations() << "[" << this->m_working_set.size() << "]";
      if (!this->m_Pvectors.empty())
        molpro::cout << ", P=" << this->m_Pvectors.size();
      if (this->m_roots > 1)
        molpro::cout << ", error["
                     << std::max_element(this->m_errors.cbegin(), this->m_errors.cend()) - this->m_errors.cbegin()
                     << "] = ";
      else
        molpro::cout << ", error = ";
      molpro::cout << *std::max_element(this->m_errors.cbegin(), this->m_errors.cend()) << ", eigenvalues: ";
      for (const auto e : ev)
        molpro::cout << " " << e;
      molpro::cout << std::endl;
    }
  }
};

/** @example LinearEquationsExample.cpp */
/*!
 * \brief A class that finds the solutions of linear equation systems using a generalisation of Davidson's method, i.e.
 * preconditioned Lanczos
 *
 * Example of simplest use: @include LinearEquationsExample.cpp
 * \tparam scalar Type of matrix elements
 *
 */
template <class T, class slowvector = T>
class LinearEquations : public IterativeSolver<T, slowvector> {
  using typename IterativeSolver<T>::scalar_type;
  using typename IterativeSolver<T>::value_type;

public:
  using vectorSet = typename std::vector<T>;                                       ///< Container of vectors
  using vectorRefSet = typename std::vector<std::reference_wrapper<T>>;            ///< Container of vectors
  using constVectorRefSet = typename std::vector<std::reference_wrapper<const T>>; ///< Container of vectors
  using IterativeSolver<T>::m_verbosity;

  /*!
   * \brief Constructor
   * \param rhs right-hand-side vectors. More can be added subsequently using addEquations(), provided iterations have
   * not yet started. \param augmented_hessian If zero, solve the inhomogeneous equations unmodified. If 1, solve
   * instead the augmented hessian problem. Other values scale the augmented hessian damping.
   */
  explicit LinearEquations(constVectorRefSet rhs, scalar_type augmented_hessian = 0) {
    this->m_linear = true;
    this->m_residual_eigen = true;
    this->m_residual_rhs = true;
    this->m_augmented_hessian = augmented_hessian;
    addEquations(rhs);
  }

  explicit LinearEquations(const vectorSet& rhs, scalar_type augmented_hessian = 0) {
    auto rhsr = constVectorRefSet(rhs.begin(), rhs.end());
    this->m_linear = true;
    this->m_residual_rhs = true;
    this->m_augmented_hessian = augmented_hessian;
    addEquations(rhsr);
  }

  explicit LinearEquations(const T& rhs, scalar_type augmented_hessian = 0) {
    auto rhsr = constVectorRefSet(1, rhs);
    this->m_linear = true;
    this->m_residual_rhs = true;
    this->m_augmented_hessian = augmented_hessian;
    addEquations(rhsr);
  }

  /*!
   * \brief add one or more equations to the set to be solved, by specifying their right-hand-side vector
   * \param rhs right-hand-side vectors to be added
   */
  void addEquations(constVectorRefSet rhs) {
    //   for (const auto &v : rhs) this->m_rhs.push_back(v);
    this->m_rhs.clear();
    this->m_rhs.reserve(rhs.size());
    for (const auto& v : rhs)
      this->m_rhs.emplace_back(v,
                               LINEARALGEBRA_DISTRIBUTED | LINEARALGEBRA_OFFLINE); // TODO template-ise these options
    //   molpro::cout << "addEquations makes m_rhs.back()="<<this->m_rhs.back()<<std::endl;
  }
  void addEquations(const std::vector<T>& rhs) { addEquations(vectorSet(rhs.begin(), rhs.end())); }
  void addEquations(const T& rhs) { addEquations(vectorSet(1, rhs)); }

protected:
  bool solveReducedProblem() override {
    const size_t nP = this->m_pspace.size();
    const size_t nQ = this->m_qspace.size();
    const size_t nR = this->m_s_rr.size();
    const Eigen::Index nX = nP + nQ + nR;
    const auto oP = 0;
    const auto oQ = oP + nP;
    const auto oR = oQ + nQ;
    //   molpro::cout << "solveReducedProblem initial subspace matrix\n"<<this->m_subspaceMatrix<<std::endl;
    //   molpro::cout << "solveReducedProblem subspaceRHS\n"<<this->m_subspaceRHS<<std::endl;
    this->m_interpolation.conservativeResize(nX, this->m_rhs.size());
    for (size_t root = 0; root < this->m_rhs.size(); root++) {
      if (this->m_augmented_hessian > 0) { // Augmented hessian
        this->m_subspaceMatrix.conservativeResize(nX + 1, nX + 1);
        this->m_subspaceOverlap.conservativeResize(nX + 1, nX + 1);
        for (Eigen::Index i = 0; i < nX; i++) {
          this->m_subspaceMatrix(i, nX) = this->m_subspaceMatrix(nX, i) =
              -this->m_augmented_hessian * this->m_subspaceRHS(i, root);
          this->m_subspaceOverlap(i, nX) = this->m_subspaceOverlap(nX, i) = 0;
        }
        this->m_subspaceMatrix(nX, nX) = 0;
        this->m_subspaceOverlap(nX, nX) = 1;
        //     molpro::cout << "solveReducedProblem augmented subspace matrix\n"<<this->m_subspaceMatrix<<std::endl;
        //     molpro::cout << "solveReducedProblem augmented subspace metric\n"<<this->m_subspaceOverlap<<std::endl;
        Eigen::GeneralizedEigenSolver<Eigen::Matrix<scalar_type, Eigen::Dynamic, Eigen::Dynamic>> s(
            this->m_subspaceMatrix, this->m_subspaceOverlap);
        auto eval = s.eigenvalues();
        auto evec = s.eigenvectors();
        Eigen::Index imax = 0;
        for (Eigen::Index i = 0; i < nX + 1; i++)
          if (eval(i).real() < eval(imax).real())
            imax = i;
        this->m_subspaceEigenvalues.conservativeResize(root + 1);
        this->m_subspaceEigenvalues(root) = eval(imax);
        //     molpro::cout << "eigenvectors\n"<<evec.real()<<std::endl;
        //     molpro::cout << "eigenvalues\n"<<eval.real()<<std::endl;
        //     molpro::cout << "imax="<<imax<<std::endl;
        //     molpro::cout <<evec.col(imax)<<std::endl;
        //     molpro::cout <<evec.col(imax).real()<<std::endl;
        //     molpro::cout <<evec.col(imax).real().head(n)<<std::endl;
        //     molpro::cout <<this->m_interpolation.col(root)<<std::endl;
        this->m_interpolation.col(root) =
            evec.col(imax).real().head(nX) / (this->m_augmented_hessian * evec.real()(nX, imax));
      } else { // straight solution of linear equations
               //        molpro::cout << "m_subspaceMatrix dimensions: " << this->m_subspaceMatrix.rows() << ", "
               //                     << this->m_subspaceMatrix.cols() << std::endl;
               //        molpro::cout << "m_subspaceRHS dimensions: " << this->m_subspaceRHS.rows() << "," <<
               //        this->m_subspaceRHS.cols()
               //                     << std::endl;
        // use QR decomposition so that also matrices that are not positive/negative semidefinite
        // can be used with IterativeSolver
        // this->m_interpolation = this->m_subspaceMatrix.ldlt().solve(this->m_subspaceRHS);
        //        molpro::cout << "m_subspaceMatrix\n" << this->m_subspaceMatrix << std::endl;
        //        molpro::cout << "m_subspaceRHS\n" << this->m_subspaceRHS << std::endl;
        this->m_interpolation = this->m_subspaceMatrix.householderQr().solve(this->m_subspaceRHS);
        //        molpro::cout << "m_interpolation\n" << this->m_interpolation << std::endl;
      }
    }
    //   molpro::cout << "m_interpolation\n"<<this->m_interpolation<<std::endl;
    this->m_subspaceMatrix.conservativeResize(nX, nX);
    this->m_subspaceOverlap.conservativeResize(nX, nX);
    //   molpro::cout << "solveReducedProblem final subspace matrix\n"<<this->m_subspaceMatrix<<std::endl;
    //   molpro::cout << "solveReducedProblem subspaceRHS\n"<<this->m_subspaceRHS<<std::endl;
    return true;
  }
};

/** @example OptimizeExample.cpp */
/*!
 * \brief A class that optimises a function using a Quasi-Newton or other method
 *
 * Example of simplest use: @include OptimizeExample.cpp
 * \tparam scalar Type of matrix elements
 *
 */
template <class T, class slowvector = T>
class Optimize : public IterativeSolver<T, slowvector> {
public:
  using typename IterativeSolver<T>::scalar_type;
  using typename IterativeSolver<T>::value_type;
  using vectorSet = typename std::vector<T>;                                       ///< Container of vectors
  using vectorRefSet = typename std::vector<std::reference_wrapper<T>>;            ///< Container of vectors
  using constVectorRefSet = typename std::vector<std::reference_wrapper<const T>>; ///< Container of vectors
  using IterativeSolver<T>::m_verbosity;
  using IterativeSolver<T>::m_values;

  /*!
   * \brief Constructor
   * \param algorithm Allowed values: "L-BFGS","null"
   * \param minimize If false, a maximum, not minimum, will be sought
   */
  explicit Optimize(const std::string& algorithm = "L-BFGS", bool minimize = true)
      : m_algorithm(algorithm), m_minimize(minimize), m_strong_Wolfe(true), m_Wolfe_1(0.0001),
        m_Wolfe_2(0.9), // recommended values Nocedal and Wright p142
        m_linesearch_tolerance(0.2), m_linesearch_grow_factor(3), m_linesearch_steplength(0) {
    this->m_linear = false;
    this->m_residual_rhs = false;
    this->m_residual_eigen = false;
    this->m_roots = 1;
    this->m_subspaceMatrixResRes = false;
    this->m_singularity_threshold = 0;
    this->m_orthogonalise_Q = false;
    this->m_exclude_r_from_redundancy_test = true;
    this->m_hermitian=false;
  }

protected:
  std::string m_algorithm; ///< which variant of Quasi-Newton or other methods
  bool m_minimize;         ///< whether to minimize or maximize
public:
  bool m_strong_Wolfe;                /// Whether to use strong or weak Wolfe conditions
  scalar_type m_Wolfe_1;              ///< Acceptance parameter for function value
  scalar_type m_Wolfe_2;              ///< Acceptance parameter for function gradient
  scalar_type m_linesearch_tolerance; ///< If the predicted line search is within tolerance of 1, don't bother taking it
  scalar_type m_linesearch_grow_factor; ///< If the predicted line search step is extrapolation, limit the step to this
                                        ///< factor times the current step
protected:
  std::vector<scalar_type> m_linesearch_steps;
  std::vector<scalar_type> m_linesearch_values;
  std::vector<scalar_type> m_linesearch_gradients; ///< the actual gradient projected onto the unit step
  //  bool m_linesearching; ///< Whether we are currently line-searching
  scalar_type m_linesearch_steplength; ///< the current line search step. Zero means continue with QN
  //  scalar_type m_linesearch_quasinewton_steplength; ///< what fraction of the Quasi-Newton step is the current line
  //  search step
  std::unique_ptr<slowvector> m_best_r, m_best_v;
  scalar_type m_best_f;

  bool interpolatedMinimum(value_type& x, scalar_type& f, value_type x0, value_type x1, scalar_type f0, scalar_type f1,
                           scalar_type g0, scalar_type g1) {
    if (std::abs(2 * f1 - g1 - 2 * f0 - g0) < 1e-10) { // cubic coefficient is zero
      auto c2 = (g1 - g0) / 2;
      if (c2 < 0)
        return false;
      x = x0 + (-0.5 * g0 / c2) * (x1 - x0);
      f = f0 + g0 * x + c2 * x * x;
      return true;
    }
    auto discriminant = (std::pow(3 * f0 - 3 * f1 + g0, 2) + (6 * f0 - 6 * f1 + g0) * g1 + std::pow(g1, 2));
    //    molpro::cout << "discriminant " << discriminant << std::endl;
    if (discriminant < 0)
      return false; // cubic has no turning points

    auto alpham = (2 * f0 - 2 * f1 + g0 + g1 == 0)
                      ? (g0 / (2 * f1 - 2 * f0 - 2 * g1))
                      : (3 * f0 - 3 * f1 + 2 * g0 + g1 - std::sqrt(discriminant)) / (3 * (2 * f0 - 2 * f1 + g0 + g1));
    auto alphap = (2 * f0 - 2 * f1 + g0 + g1 == 0)
                      ? (g0 / (2 * f1 - 2 * f0 - 2 * g1))
                      : (3 * f0 - 3 * f1 + 2 * g0 + g1 + std::sqrt(discriminant)) / (3 * (2 * f0 - 2 * f1 + g0 + g1));
    auto fm = f0 + alpham * (g0 + alpham * (-3 * f0 + 3 * f1 - 2 * g0 - g1 + alpham * (2 * f0 - 2 * f1 + g0 + g1)));
    auto fp = f0 + alphap * (g0 + alphap * (-3 * f0 + 3 * f1 - 2 * g0 - g1 + alphap * (2 * f0 - 2 * f1 + g0 + g1)));
    f = std::min(fm, fp);
    x = x0 + (fm < fp ? alpham : alphap) * (x1 - x0);
    return true;
  }

  bool solveReducedProblem() override {
    auto n = this->m_qspace.size();
    //    molpro::cout << "Optimize::solveReduced Problem n=" << n << std::endl;
    if (n > 0) {

      // first consider whether this point can be taken as the next iteration point, or whether further line-searching
      // is needed
      //      auto step = std::sqrt(this->m_subspaceOverlap(n - 1, n - 1));
      double step = 1 / this->m_qspace.scale_factor(this->m_qspace.size() - 1);
      auto f0 = m_best_f;
      auto f1 = m_values.back();
      auto g1 = step * this->m_h_qr[n - 1][0];
      //      molpro::cout << "this->m_residuals[n-1][0] " << this->m_residuals[n - 1][0] << std::endl;
      //      molpro::cout << "this->m_solutions[n-1][0] " << this->m_solutions[n - 1][0] << std::endl;
      //      molpro::cout << "this->m_residuals.back()[0] " << this->m_residuals.back()[0] << std::endl;
      //      molpro::cout << "this->m_solutions.back()[0] " << this->m_solutions.back()[0] << std::endl;
      auto g0 = step * (*m_best_v).dot(this->m_qspace[this->m_qspace.size() - 1]);
      bool Wolfe_1 = f1 <= f0 + m_Wolfe_1 * g0;
      bool Wolfe_2 = m_strong_Wolfe ? g1 >= m_Wolfe_2 * g0 : std::abs(g1) <= m_Wolfe_2 * std::abs(g0);
      if (this->m_verbosity > 1) {
        //      molpro::cout << "subspace Matrix diagonal " << this->m_subspaceMatrix(n - 1, n - 1) << std::endl;
        //      molpro::cout << "subspace Overlap diagonal " << this->m_subspaceOverlap(n - 1, n - 1) << std::endl;
        molpro::cout << "step=" << step << std::endl;
        molpro::cout << "f0=" << f0 << std::endl;
        molpro::cout << "f1=" << f1 << std::endl;
        molpro::cout << " m_Wolfe_1 =" << m_Wolfe_1 << std::endl;
        molpro::cout << " m_Wolfe_1 * g0=" << m_Wolfe_1 * g0 << std::endl;
        molpro::cout << "f0 + m_Wolfe_1 * g0=" << f0 + m_Wolfe_1 * g0 << std::endl;
        molpro::cout << "g0=" << g0 << std::endl;
        molpro::cout << "g1=" << g1 << std::endl;
        molpro::cout << "Wolfe conditions: " << Wolfe_1 << Wolfe_2 << std::endl;
      }
      if (g1 < this->m_thresh or (Wolfe_1 && Wolfe_2))
        goto accept;
      scalar_type finterp;
      //      molpro::cout << "before interpolatedMinimum" << std::endl;
      scalar_type alpha;
      auto interpolated = interpolatedMinimum(alpha, finterp, 0, 1, f0, f1, g0, g1);
      //      molpro::cout << "interpolated: " << interpolated << ", alpha " <<
      //      alpha
      //                   << ", finterp " << finterp << std::endl;
      if (interpolated and ((g0 > 0 and g1 > 0 and alpha > 0) or
                            (g0 < 0 and g1 < 0 and alpha < 1))) // not bracketed, interpolant goes the wrong way
        interpolated = false;
      if (not interpolated or alpha > m_linesearch_grow_factor) {
        if (this->m_verbosity > 1) {
          if (interpolated)
            molpro::cout << "reject interpolated minimum value " << finterp << " at alpha=" << alpha << std::endl;
          else
            molpro::cout << "cubic interpolation did not find a valid minimum" << std::endl;
          molpro::cout << "taking instead step=" << m_linesearch_grow_factor << std::endl;
        }
        alpha = m_linesearch_grow_factor; // expand the search range
      } else if (std::abs(alpha - 1) < m_linesearch_tolerance) {
        if (this->m_verbosity > 1)
          molpro::cout << "Don't bother with linesearch " << alpha << std::endl;
        goto accept; // if we are within spitting distance already, don't bother to make a line step
      } else {
        if (this->m_verbosity > 1)
          molpro::cout << "cubic linesearch interpolant has minimum " << finterp << " at " << alpha << "(absolute step "
                       << (alpha - 1) * step << ")" << std::endl;
      }
      // when we arrive here, we need to do a new line-search step
      //      molpro::cout << "we need to do a new line-search step " << alpha << std::endl;
      this->m_interpolation.conservativeResize(this->m_qspace.size() + 1, 1);
      this->m_interpolation.setZero();
      this->m_interpolation(this->m_qspace.size(), 0) = 1;
      m_linesearch_steplength = (alpha - 1) * step;
      if (f1 <= f0) {
        m_best_r.reset(new slowvector(this->m_current_r.front()));
        m_best_v.reset(new slowvector(this->m_current_v.front()));
        m_best_f = m_values.back();
        //        molpro::cout << "setting best to current, with f=" << m_best_f << std::endl;
      }
      //      molpro::cout << "m_interpolation: " << this->m_interpolation << std::endl;
      this->m_nullify_solution_before_update = false;
      return false;
    }
  accept:
//    molpro::cout << "accept reached" << std::endl;
    m_linesearch_steplength = 0;
    auto& minusAlpha = this->m_interpolation;
    //    minusAlpha.conservativeResize(n, 1);
    this->m_interpolation.conservativeResize(n + 1, 1);
    this->m_interpolation.setZero();
    this->m_interpolation(n, 0) = 1;
    this->m_nullify_solution_before_update = true;
    if (this->m_algorithm == "L-BFGS") {
      for (int a = this->m_qspace.size() - 1; a >= 0; a--) {
//        molpro::cout << "iterate q_" << a << std::endl;
        minusAlpha(a, 0) = -this->m_h_qr[a][0];
        for (auto b = a + 1; b < this->m_qspace.size(); b++)
          minusAlpha(a, 0) -= minusAlpha(b, 0) * this->m_qspace.action(a, b);
        minusAlpha(a, 0) /= this->m_qspace.action(a, a);
//        molpro::cout << "minusAlpha(a,0) " << minusAlpha(a, 0) << std::endl;
        //        this->m_interpolation(a, 0) = minusAlpha(a,0);
      }
    }
    m_best_r.reset(new slowvector(this->m_current_r.front()));
    m_best_v.reset(new slowvector(this->m_current_v.front()));
    m_best_f = m_values.back();
    return true;
  }

public:
  virtual bool endIteration(vectorRefSet solution, constVectorRefSet residual) override {
    if (this->m_q_solutions.count(0) == 0) {
      if (m_linesearch_steplength != 0) { // line search
//        molpro::cout << "*enter endIteration m_linesearch_steplength=" << m_linesearch_steplength << std::endl;
        //              molpro::cout << "solution " << solution.front().get() << std::endl;
        solution.front().get() = *m_best_r;
        solution.front().get().axpy(m_linesearch_steplength, this->m_qspace[this->m_qspace.size() - 1]);
        m_values.pop_back();
        this->m_qspace.remove(this->m_qspace.size() - 1);
      } else { // quasi-Newton
        if (m_algorithm == "L-BFGS" and this->m_interpolation.size() > 0) {
//          molpro::cout << "L-BFGS stage 2" << std::endl;
          //          molpro::cout << "before subtracting rk solution length="
          //                       << std::sqrt(solution.back().get().dot(solution.back().get())) << std::endl;
          //          solution.back().get().axpy(-1, this->m_last_d.back());
          //          molpro::cout << "after subtracting rk solution length="
          //                       << std::sqrt(solution.back().get().dot(solution.back().get())) << std::endl;
          auto& minusAlpha = this->m_interpolation;
          for (auto a = 0; a < this->m_qspace.size(); a++) {
//            molpro::cout << "iterate q_" << a << std::endl;
            auto factor =
                minusAlpha(a, 0) - this->m_qspace.action(a).dot(solution.back().get()) / this->m_qspace.action(a, a);
            solution.back().get().axpy(factor, this->m_qspace[a]);
//            molpro::cout << "Q factor " << factor << std::endl;
          }
//          molpro::cout << "after Q loop solution length=" << std::sqrt(solution.back().get().dot(solution.back().get()))
//                       << std::endl;
          solution.back().get().axpy(1, *(this->m_best_r));
//          molpro::cout << "after adding rk solution length="
//                       << std::sqrt(solution.back().get().dot(solution.back().get())) << std::endl;
        }
      }
      //    molpro::cout << "*exit endIteration m_linesearch_steplength=" << m_linesearch_steplength << std::endl;
    }
    return IterativeSolver<T>::endIteration(solution, residual);
  }

  virtual bool endIteration(std::vector<T>& solution, const std::vector<T>& residual) override {
    return endIteration(vectorRefSet(solution.begin(), solution.end()),
                        constVectorRefSet(residual.begin(), residual.end()));
  }
  virtual bool endIteration(T& solution, const T& residual) override {
    return endIteration(vectorRefSet(1, solution), constVectorRefSet(1, residual));
  }

public:
  virtual void report() const override {
    if (m_verbosity > 0) {
      molpro::cout << "iteration " << this->iterations();
      if (m_linesearch_steplength != 0)
        molpro::cout << ", line search step = " << m_linesearch_steplength;
      if (not m_values.empty())
        molpro::cout << ", " << this->m_value_print_name << " = " << m_values.back();
      molpro::cout << ", error = " << this->m_errors.front() << std::endl;
    }
  }
};

/** @example DIISexample.cpp */
/*!
 * \brief A class that encapsulates accelerated convergence of non-linear equations
 * through the DIIS or related methods.
 *
 * Example of simplest use: @include DIISexample.cpp
 *
 */
template <class T, class slowvector = T>
class DIIS : public IterativeSolver<T, slowvector> {
  using IterativeSolver<T>::m_residuals;
  using IterativeSolver<T>::m_solutions;
  using IterativeSolver<T>::m_others;

public:
  using typename IterativeSolver<T>::scalar_type;
  using typename IterativeSolver<T>::value_type;
  using IterativeSolver<T>::m_verbosity;
  enum DIISmode_type {
    disabled ///< No extrapolation is performed
    ,
    DIISmode ///< Direct Inversion in the Iterative Subspace
    ,
    KAINmode ///< Krylov Accelerated Inexact Newton
  };

  /*!
   * \brief DIIS
   */
  DIIS() {
    this->m_residual_rhs = false;
    this->m_residual_eigen = false;
    this->m_roots = 1;
    setMode(DIISmode);
    this->m_exclude_r_from_redundancy_test = true;
    this->m_singularity_threshold =
        this->m_svdThreshold; // It does not matter if the submatrix goes a bit singular in DIIS
    this->m_orthogonalise_Q = false;
  }

  /*!
   * \brief Set options for DIIS.
   * \param mode Whether to perform DIIS, KAIN, or nothing.
   */
  virtual void setMode(enum DIISmode_type mode = DIISmode) {
    m_DIISmode = mode;
    this->m_subspaceMatrixResRes = mode != KAINmode;
    //     this->m_preconditionResiduals = mode==KAINmode; // FIXME

    if (m_verbosity > 1)
      molpro::cout << "m_DIISmode set to " << m_DIISmode << std::endl;
  }

protected:
  bool solveReducedProblem() override {
    //	  molpro::cout << "Enter DIIS::solveReducedProblem"<<std::endl;
    //	  molpro::cout << "residual : "<<residual<<std::endl;
    //	  molpro::cout << "solution : "<<solution<<std::endl;
    this->m_updateShift.clear();
    this->m_updateShift.push_back(-(1 + std::numeric_limits<double>::epsilon()) * this->m_subspaceMatrix(0, 0));

    if (this->m_roots > 1)
      throw std::logic_error("DIIS does not handle multiple solutions");

    //  if (m_subspaceMatrix.rows() < 9) {
    //      molpro::cout << "m_subspaceMatrix on entry to
    //      DIIS::solveReducedProblem"<<std::endl<<m_subspaceMatrix<<std::endl;
    //  }
    size_t nDim = this->m_subspaceMatrix.rows() - 1;
    this->m_interpolation.resize(nDim + 1, 1);
    if (nDim > 0) {
      Eigen::VectorXd Rhs(nDim), Coeffs(nDim);
      Eigen::MatrixXd B(nDim, nDim);

      B.block(0, 0, nDim, nDim) = this->m_subspaceMatrix.block(0, 0, nDim, nDim);
      Rhs = -this->m_subspaceMatrix.block(0, nDim, nDim, 1);

      molpro::cout << "B:" << std::endl << B << std::endl;
      molpro::cout << "Rhs:" << std::endl << Rhs << std::endl;

      // invert the system, determine extrapolation coefficients.
      Eigen::JacobiSVD<Eigen::MatrixXd> svd(B, Eigen::ComputeThinU | Eigen::ComputeThinV);
      svd.setThreshold(this->m_svdThreshold);
      //    molpro::cout << "svdThreshold "<<this->m_svdThreshold<<std::endl;
      //    molpro::cout << "U\n"<<svd.matrixU()<<std::endl;
      //    molpro::cout << "V\n"<<svd.matrixV()<<std::endl;
      //    molpro::cout << "singularValues\n"<<svd.singularValues()<<std::endl;
      Coeffs = svd.solve(Rhs).head(nDim);
      if (m_verbosity > 1)
        molpro::cout << "Combination of iteration vectors: " << Coeffs.transpose() << std::endl;
      for (size_t k = 0; k < (size_t)Coeffs.rows(); k++)
        if (std::isnan(Coeffs(k))) {
          molpro::cout << "B:" << std::endl << B << std::endl;
          molpro::cout << "Rhs:" << std::endl << Rhs << std::endl;
          molpro::cout << "Combination of iteration vectors: " << Coeffs.transpose() << std::endl;
          throw std::overflow_error("NaN detected in DIIS submatrix solution");
        }
      this->m_interpolation.block(0, 0, nDim, 1) = Coeffs;
    }
    molpro::cout << "m_interpolation rows=" << this->m_interpolation.rows() << ", cols=" << this->m_interpolation.cols()
                 << std::endl;
    this->m_interpolation(nDim, 0) = 1;
    return true;
  }

public:
  static void randomTest(size_t sample, size_t n = 100, double alpha = 0.1, double gamma = 0.0,
                         DIISmode_type mode = DIISmode);

private:
  typedef unsigned int uint;
  enum DIISmode_type m_DIISmode;

  // the following variables are kept for informative/displaying purposes
  scalar_type
      // dot(R,R) of last residual vector fed into this state.
      m_LastResidualNormSq,
      // coefficient the actual new vector got in the last DIIS step
      m_LastAmplitudeCoeff;
};

// extern template
// class LinearEigensystem<double>;
// extern template
// class LinearEquations<double>;
// extern template
// class DIIS<double>;

} // namespace linalg
} // namespace molpro

// C interface
extern "C" void IterativeSolverLinearEigensystemInitialize(size_t nQ, size_t nroot, double thresh,
                                                           unsigned int maxIterations, int verbosity, const char* fname,
                                                           int64_t fcomm, int lmppx);

extern "C" void IterativeSolverLinearEquationsInitialize(size_t n, size_t nroot, const double* rhs, double aughes,
                                                         double thresh, unsigned int maxIterations, int verbosity);

extern "C" void IterativeSolverDIISInitialize(size_t n, double thresh, unsigned int maxIterations, int verbosity);

extern "C" void IterativeSolverOptimizeInitialize(size_t n, double thresh, unsigned int maxIterations, int verbosity,
                                                  char* algorithm, int minimize);

extern "C" void IterativeSolverFinalize();

extern "C" int IterativeSolverAddVector(double* parameters, double* action, double* parametersP, int sync, int lmppx);

extern "C" int IterativeSolverAddValue(double* parameters, double value, double* action, int sync, int lmppx);

extern "C" int IterativeSolverEndIteration(double* c, double* g, double* error, int lmppx);

extern "C" void IterativeSolverAddP(size_t nP, const size_t* offsets, const size_t* indices, const double* coefficients,
                                    const double* pp, double* parameters, double* action, double* parametersP,
                                    int lmppx);

extern "C" void IterativeSolverEigenvalues(double* eigenvalues);

extern "C" void IterativeSolverOption(const char* key, const char* val);

extern "C" size_t IterativeSolverSuggestP(const double* solution, const double* residual, size_t maximumNumber,
                                          double threshold, size_t* indices, int lmppx);

#endif // ITERATIVESOLVER_H
