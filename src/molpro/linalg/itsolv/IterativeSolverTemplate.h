#ifndef LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_ITERATIVESOLVERTEMPLATE_H
#define LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_ITERATIVESOLVERTEMPLATE_H
#include <molpro/linalg/itsolv/IterativeSolver.h>
#include <molpro/linalg/itsolv/subspace/Matrix.h>
#include <molpro/linalg/itsolv/subspace/util.h>

namespace molpro {
namespace linalg {
namespace itsolv {
namespace detail {

template <class R, class Q, class P>
void construct_solution(const std::vector<int>& working_set, std::vector<std::reference_wrapper<R>>& params,
                        const std::vector<std::reference_wrapper<R>>& dummy,
                        const std::vector<std::reference_wrapper<Q>>& qparams,
                        const std::vector<std::reference_wrapper<P>>& pparams, size_t oR, size_t oQ, size_t oP,
                        const subspace::Matrix<double>& solutions, ArrayHandlers<R, Q, P>& handlers) {
  assert(dummy.size() >= working_set.size());
  for (size_t i = 0; i < working_set.size(); ++i) {
    handlers.rr().copy(dummy.at(i), params.at(i));
  }
  for (size_t i = 0; i < working_set.size(); ++i) {
    handlers.rr().fill(0, params[i]);
    auto solution = solutions.col(working_set[i]);
    for (size_t j = 0; j < pparams.size(); ++j) {
      handlers.rp().axpy(solution(oP + j, 0), pparams.at(j), params.at(i));
    }
    for (size_t j = 0; j < qparams.size(); ++j) {
      handlers.rq().axpy(solution(oQ + j, 0), qparams.at(j), params.at(i));
    }
    for (size_t j = 0; j < dummy.size(); ++j) {
      handlers.rr().axpy(solution(oR + j, 0), dummy.at(j), params.at(i));
    }
  }
}

template <class R, typename T>
void construct_residual(const std::vector<int>& working_set, const std::vector<std::reference_wrapper<R>>& solutions,
                        const std::vector<std::reference_wrapper<R>>& actions,
                        std::vector<std::reference_wrapper<R>>& residuals, const std::vector<T>& eigvals,
                        array::ArrayHandler<R, R>& handler) {
  assert(residuals.size() >= working_set.size());
  for (size_t i = 0; i < working_set.size(); ++i) {
    handler.rr().copy(residuals.at(i), actions.at(i));
  }
  for (size_t i = 0; i < working_set.size(); ++i) {
    handler.axpy(-eigvals.at(working_set[i]), solutions.at(i), residuals.at(i));
  }
}

template <class R>
auto update_errors(const std::vector<int>& working_set, const std::vector<std::reference_wrapper<R>>& residual,
                   array::ArrayHandler<R, R>& handler) {
  auto errors = std::vector<double>(working_set.size());
  for (size_t i = 0; i < working_set.size(); ++i)
    errors[i] = handler->rr().dot(residual[i], residual[i]);
}

} // namespace detail

/*!
 * @brief Implements common functionality of iterative solvers
 *
 * This is the trunk. It has a template of steps that all iterative solvers follow. Variations in implementation are
 * accepted as policies for managing the subspaces.
 *
 */
template <class Solver, class XS>
class IterativeSolverTemplate : public Solver {
public:
  using typename Solver::scalar_type;
  using RS = typename XS::RS;
  using QS = typename XS::QS;
  using PS = typename XS::PS;
  using R = typename Solver::R;
  using Q = typename Solver::Q;
  using P = typename Solver::P;

  void add_vector(std::vector<R>& parameters, std::vector<R>& action, std::vector<P>& parametersP) override {
    using subspace::util::wrap;
    m_rspace.update(parameters, action, *static_cast<Solver*>(this));
    m_working_set.clear();
    std::copy(begin(m_rspace.working_set), end(m_rspace.working_set()), std::back_inserter(m_working_set));
    if (m_nroots == 0)
      m_nroots = m_working_set.size();
    m_qspace.update(m_rspace, *static_cast<Solver*>(this));
    m_xspace.build_subspace(m_rspace, m_qspace, m_pspace);
    m_xspace.check_conditioning(m_rspace, m_qspace, m_pspace);
    m_xspace.solve(*static_cast<Solver*>(this));
    auto& dummy = m_rspace.dummy(m_working_set.size());
    detail::construct_solution(m_working_set, m_rspace.params(), wrap(dummy), m_qspace.params(), m_pspace.params(),
                               m_xspace.dimensions().oR, m_xspace.dimensions().oQ, m_xspace.dimensions().oP,
                               m_xspace.solutions(), *m_handlers);
    detail::construct_solution(m_working_set, m_rspace.actions(), wrap(dummy), m_qspace.actions(), m_pspace.actions(),
                               m_xspace.dimensions().oR, m_xspace.dimensions().oQ, m_xspace.dimensions().oP,
                               m_xspace.solutions(), *m_handlers);
    construct_residual(m_working_set, m_rspace.params(), m_rspace.actions(), wrap(dummy), m_xspace.eigenvalues(),
                       m_handlers->rr());
    m_errors = detail::update_errors(m_working_set, wrap(dummy), m_handlers->rr());
    update_working_set();
    for (size_t i = 0; i < m_working_set.size(); ++i)
      m_handlers->rr().copy(m_rspace.actions().at(i), dummy.at(i));
    construct_residual(parameters, action);
  };

  void solution(const std::vector<int>& roots, std::vector<R>& parameters, std::vector<R>& residual) override {
    auto working_set_save = m_working_set;
    m_working_set = roots;
    m_xspace.build_subspace(m_rspace, m_qspace, m_pspace);
    m_xspace.solve(*static_cast<Solver*>(this));
    construct_solution(parameters);
    construct_residual(residual);
    m_working_set = working_set_save;
  };

  void solution(const std::vector<int>& roots, std::vector<R>& parameters, std::vector<R>& residual,
                std::vector<P>& parametersP) override {
    solution(roots, parameters, residual);
  }

  std::vector<size_t> suggest_p(const std::vector<R>& solution, const std::vector<R>& residual, size_t maximumNumber,
                                double threshold) override {
    return {};
  }

  const std::vector<int>& working_set() const override { return m_working_set; }
  size_t n_roots() const override { return 0; }
  const std::vector<scalar_type>& errors() const override { return m_errors; }
  const Statistics& statistics() const override { return *m_stats; }

protected:
  //! Updates working sets and adds any converged solution to the q space
  void update_working_set() {}

  std::shared_ptr<ArrayHandlers<R, Q, P>> m_handlers;
  RS m_rspace;
  QS m_qspace;
  PS m_pspace;
  XS m_xspace;
  std::vector<double> m_errors;
  std::vector<int> m_working_set;
  size_t m_nroots{0};
  std::shared_ptr<Statistics> m_stats;
};

} // namespace itsolv
} // namespace linalg
} // namespace molpro

#endif // LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_ITERATIVESOLVERTEMPLATE_H
