#ifndef LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_XSPACE_H
#define LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_XSPACE_H
#include <molpro/linalg/itsolv/subspace/SubspaceData.h>

namespace molpro {
namespace linalg {
namespace itsolv {
namespace subspace {
namespace xspace {
//! Stores partitioning of XSpace into P, Q and R blocks with sizes and offsets for each one
struct Dimensions {
  Dimensions() = default;
  Dimensions(size_t np, size_t nq, size_t nr, size_t nc) : nP(np), nQ(nq), nR(nr), nC(nc) {}
  size_t nP = 0;
  size_t nQ = 0;
  size_t nR = 0;
  size_t nC = 0;
  size_t nX = nP + nQ + nR + nC;
  size_t oP = 0;
  size_t oQ = nP;
  size_t oR = oQ + nQ;
  size_t oC = oR + nR;
};
} // namespace xspace

//! Base class for XSpace solvers
template <class Rs, class Qs, class Ps, class Cs, typename ST>
class XSpace {
public:
  using scalar_type = ST;
  using R = typename Rs::R;
  using Q = typename Qs::Q;
  using P = typename Ps::P;
  using RS = Rs;
  using QS = Qs;
  using PS = Ps;
  using CS = Cs;
  XSpace() = default;
  virtual ~XSpace() = default;

  SubspaceData data = null_data<EqnData::H, EqnData::S>();

  //! Ensures that the subspace is well conditioned.
  virtual void check_conditioning(RS& rs, QS& qs, PS& ps, CS& cs) = 0;

  //! Solves the underlying problem in the subspace. solver can be used to pass extra parameters defining the problem
  virtual void solve(const IterativeSolver<R, Q, P>& solver) = 0;

  //! Access solution matrix. Solution vectors are stored as rows
  virtual const Matrix<scalar_type>& solutions() const = 0;

  // TODO rename this. What does this mean in DIIS or BFGS solvers? How does it relate to the errors?
  virtual const std::vector<scalar_type>& eigenvalues() const = 0;

  //! Number of vectors forming the subspace
  size_t size() { return dimensions().nX; }

  //! Build the subspace matrices H, S etc.
  virtual void build_subspace(RS& rs, QS& qs, PS& ps, CS& cs) = 0;

  virtual const xspace::Dimensions& dimensions() const = 0;
};

} // namespace subspace
} // namespace itsolv
} // namespace linalg
} // namespace molpro

#endif // LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_XSPACE_H