#ifndef LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_XSPACE_H
#define LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_XSPACE_H
#include <cassert>
#include <molpro/linalg/itsolv/helper.h>
#include <molpro/linalg/itsolv/subspace/DSpace.h>
#include <molpro/linalg/itsolv/subspace/Dimensions.h>
#include <molpro/linalg/itsolv/subspace/PSpace.h>
#include <molpro/linalg/itsolv/subspace/QSpace.h>
#include <molpro/linalg/itsolv/subspace/XSpaceI.h>

namespace molpro::linalg::itsolv::subspace {
namespace xspace {
//! New sections of equation data
struct NewData {
  NewData(size_t nQnew, size_t nX) {
    for (auto d : {EqnData::H, EqnData::S}) {
      qq[d].resize({nQnew, nQnew});
      qx[d].resize({nQnew, nX});
      xq[d].resize({nX, nQnew});
    }
  }

  SubspaceData qq = null_data<EqnData::H, EqnData::S>(); //!< data block between new paramters
  SubspaceData qx = null_data<EqnData::H, EqnData::S>(); //!< data block between new parameters and current X space
  SubspaceData xq = null_data<EqnData::H, EqnData::S>(); //!< data block between current X space and new parameters
};

//! Returns new sections of equation data
template <class R, class Q, class P>
auto update_qspace_data(const CVecRef<R>& params, const CVecRef<R>& actions, const CVecRef<P>& pparams,
                        const CVecRef<Q>& qparams, const CVecRef<Q>& qactions, const CVecRef<Q>& dparams,
                        const CVecRef<Q>& dactions, const Dimensions& dims, ArrayHandlers<R, Q, P>& handlers,
                        Logger& logger) {
  auto nQnew = params.size();
  auto data = NewData(nQnew, dims.nX);
  auto& qq = data.qq;
  auto& qx = data.qx;
  auto& xq = data.xq;
  qq[EqnData::S] = util::overlap(params, handlers.rr());
  qx[EqnData::S].slice({0, dims.oP}, {nQnew, dims.oP + dims.nP}) = util::overlap(params, pparams, handlers.rp());
  qx[EqnData::S].slice({0, dims.oQ}, {nQnew, dims.oQ + dims.nQ}) = util::overlap(params, qparams, handlers.rq());
  qx[EqnData::S].slice({0, dims.oD}, {nQnew, dims.oD + dims.nD}) = util::overlap(params, dparams, handlers.rq());
  qq[EqnData::H] = util::overlap(params, actions, handlers.rr());
  qx[EqnData::H].slice({0, dims.oQ}, {nQnew, dims.oQ + dims.nQ}) = util::overlap(params, qactions, handlers.rq());
  qx[EqnData::H].slice({0, dims.oD}, {nQnew, dims.oD + dims.nD}) = util::overlap(params, dactions, handlers.rq());
  xq[EqnData::H].slice({dims.oP, 0}, {dims.oP + dims.nP, nQnew}) = util::overlap(pparams, actions, handlers.rp());
  xq[EqnData::H].slice({dims.oQ, 0}, {dims.oQ + dims.nQ, nQnew}) = util::overlap(qparams, actions, handlers.qr());
  xq[EqnData::H].slice({dims.oD, 0}, {dims.oD + dims.nD, nQnew}) = util::overlap(dparams, actions, handlers.qr());
  transpose_copy(xq[EqnData::S].slice({dims.oP, 0}, {dims.oP + dims.nP, nQnew}),
                 qx[EqnData::S].slice({0, dims.oP}, {nQnew, dims.oP + dims.nP}));
  transpose_copy(xq[EqnData::S].slice({dims.oQ, 0}, {dims.oQ + dims.nQ, nQnew}),
                 qx[EqnData::S].slice({0, dims.oQ}, {nQnew, dims.oQ + dims.nQ}));
  transpose_copy(xq[EqnData::S].slice({dims.oD, 0}, {dims.oD + dims.nD, nQnew}),
                 qx[EqnData::S].slice({0, dims.oD}, {nQnew, dims.oD + dims.nD}));
  // FIXME only works for Hermitian Hamiltonian
  transpose_copy(qx[EqnData::H].slice({0, dims.oP}, {nQnew, dims.oP + dims.nP}),
                 xq[EqnData::H].slice({dims.oP, 0}, {dims.oP + dims.nP, nQnew}));
  if (logger.data_dump) {
    logger.msg("xspace::update_qspace_data() nQnew = " + std::to_string(nQnew), Logger::Info);
    logger.msg("Sqq = " + as_string(qq[EqnData::S]), Logger::Info);
    logger.msg("Hqq = " + as_string(qq[EqnData::H]), Logger::Info);
    logger.msg("Sqx = " + as_string(qx[EqnData::S]), Logger::Info);
    logger.msg("Hqx = " + as_string(qx[EqnData::H]), Logger::Info);
    logger.msg("Sxq = " + as_string(xq[EqnData::S]), Logger::Info);
    logger.msg("Hxq = " + as_string(xq[EqnData::H]), Logger::Info);
  }
  return data;
}

//! Calculates overlap blocks between D space and the rest of the subspace
template <class Q, class P>
auto update_dspace_overlap_data(const CVecRef<P>& pparams, const CVecRef<Q>& qparams, const CVecRef<Q>& dparams,
                                array::ArrayHandler<Q, P>& handler_qp, array::ArrayHandler<Q, Q>& handler_qq,
                                Logger& logger) {
  const auto nP = pparams.size();
  const auto nQ = qparams.size();
  const auto nX = nP + nQ;
  auto nD = dparams.size();
  auto data = NewData(nD, nX);
  data.qq[EqnData::S].slice() = util::overlap(dparams, handler_qq);
  data.qx[EqnData::S].slice({0, 0}, {nD, nP}) = util::overlap(dparams, pparams, handler_qp);
  data.qx[EqnData::S].slice({0, nP}, {nD, nX}) = util::overlap(dparams, qparams, handler_qq);
  transpose_copy(data.xq[EqnData::S].slice(), data.qx[EqnData::S].slice());
  if (logger.data_dump) {
    logger.msg("xspace::update_dspace_overlap_data() nD = " + std::to_string(nD), Logger::Info);
    logger.msg("Sdd = " + as_string(data.qq[EqnData::S]), Logger::Info);
    logger.msg("Sdx = " + as_string(data.qx[EqnData::S]), Logger::Info);
  }
  return data;
}

//! Calculates overlap blocks between D space and the rest of the subspace
template <class Q, class P>
auto update_dspace_action_data(const CVecRef<P>& pparams, const CVecRef<Q>& qparams, const CVecRef<Q>& qactions,
                               const CVecRef<Q>& dparams, const CVecRef<Q>& dactions,
                               array::ArrayHandler<Q, P>& handler_qp, array::ArrayHandler<Q, Q>& handler_qq,
                               Logger& logger) {
  const auto nP = pparams.size();
  const auto nQ = qparams.size();
  const auto nX = nP + nQ;
  auto nD = dparams.size();
  auto data = NewData(nD, nX);
  const auto e = EqnData::H;
  data.qq[e].slice() = util::overlap(dparams, dactions, handler_qq);
  data.xq[e].slice({0, 0}, {nP, nD}) = util::overlap(pparams, dactions, handler_qp);
  data.xq[e].slice({nP, 0}, {nX, nD}) = util::overlap(qparams, dactions, handler_qq);
  data.qx[e].slice({0, nP}, {nD, nX}) = util::overlap(dparams, qactions, handler_qq);
  transpose_copy(data.qx[e].slice({0, 0}, {nD, nP}), data.xq[e].slice({0, 0}, {nP, nD}));
  if (logger.data_dump) {
    logger.msg("xspace::update_dspace_action_data() nD = " + std::to_string(nD), Logger::Info);
    logger.msg("Hdd = " + as_string(data.qq[e]), Logger::Info);
    logger.msg("Hdx = " + as_string(data.qx[e]), Logger::Info);
    logger.msg("Hxd = " + as_string(data.xq[e]), Logger::Info);
  }
  return data;
}

void copy_dspace_eqn_data(const NewData& new_data, SubspaceData& data, const subspace::EqnData e,
                          const Dimensions& dims) {
  const auto& dd = new_data.qq.at(e);
  const auto& dx = new_data.qx.at(e);
  const auto& xd = new_data.xq.at(e);
  data[e].slice({dims.oD, dims.oD}, {dims.oD + dims.nD, dims.oD + dims.nD}) = dd;
  data[e].slice({dims.oD, dims.oP}, {dims.oD + dims.nD, dims.oP + dims.nP}) = dx.slice({0, 0}, {dims.nD, dims.nP});
  data[e].slice({dims.oD, dims.oQ}, {dims.oD + dims.nD, dims.oQ + dims.nQ}) =
      dx.slice({0, dims.nP}, {dims.nD, dims.nP + dims.nQ});
  data[e].slice({dims.oP, dims.oD}, {dims.oP + dims.nP, dims.oD + dims.nD}) = xd.slice({0, 0}, {dims.nP, dims.nD});
  data[e].slice({dims.oQ, dims.oD}, {dims.oQ + dims.nQ, dims.oD + dims.nD}) =
      xd.slice({dims.nP, 0}, {dims.nP + dims.nQ, dims.nD});
}
} // namespace xspace

template <class R, class Q, class P>
class XSpace : public XSpaceI<R, Q, P> {
public:
  using typename XSpaceI<R, Q, P>::value_type;
  using typename XSpaceI<R, Q, P>::value_type_abs;
  using XSpaceI<R, Q, P>::data;

  explicit XSpace(const std::shared_ptr<ArrayHandlers<R, Q, P>>& handlers, const std::shared_ptr<Logger>& logger)
      : pspace(), qspace(handlers, logger), dspace(logger), m_handlers(handlers), m_logger(logger) {
    data = null_data<EqnData::H, EqnData::S>();
  };

  //! Updata parameters in Q space and corresponding equation data
  void update_qspace(const CVecRef<R>& params, const CVecRef<R>& actions) override {
    m_logger->msg("QSpace::update_qspace", Logger::Trace);
    auto new_data = xspace::update_qspace_data(params, actions, cparamsp(), cparamsq(), cactionsq(), cparamsd(),
                                               cactionsd(), m_dim, *m_handlers, *m_logger);
    qspace.update(params, actions, new_data.qq, new_data.qx, new_data.xq, m_dim, data);
    update_dimensions();
  }

  //! Clears old D space container and stores new params and actions. @param lin_trans_only_R R space component of D
  void update_dspace(VecRef<Q>& params, VecRef<Q>& actions) override {
    dspace.update(params, actions);
    update_dimensions();
    for (auto e : {EqnData::H, EqnData::S})
      data[e].resize({m_dim.nX, m_dim.nX});
    auto new_data = xspace::update_dspace_overlap_data(cparamsp(), cparamsq(), cparamsd(), m_handlers->qp(),
                                                       m_handlers->qq(), *m_logger);
    xspace::copy_dspace_eqn_data(new_data, data, EqnData::S, m_dim);
    auto new_data_action = xspace::update_dspace_action_data(
        cparamsp(), cparamsq(), cactionsq(), cparamsd(), cactionsd(), m_handlers->qp(), m_handlers->qq(), *m_logger);
    xspace::copy_dspace_eqn_data(new_data_action, data, EqnData::H, m_dim);
  }

  // FIXME this must be called when XSpace is empty
  void update_pspace(const CVecRef<P>& params, const array::Span<value_type>& pp_action_matrix) override {
    assert(m_dim.nX == 0);
    pspace.update(params, m_handlers->pp());
    update_dimensions();
    const size_t nP = m_dim.nP;
    data[EqnData::S].resize({nP, nP});
    data[EqnData::H].resize({nP, nP});
    data[EqnData::S].slice() = util::overlap(params, m_handlers->pp());
    for (size_t i = 0, ij = 0; i < nP; ++i)
      for (size_t j = 0; j < nP; ++j, ++ij)
        data[EqnData::H](i, j) = pp_action_matrix[ij];
  }

  const Dimensions& dimensions() const override { return m_dim; }

  void erase(size_t i) override {
    if (m_dim.oP >= i && i < m_dim.oP + m_dim.nP) {
      erasep(i - m_dim.oP);
    } else if (m_dim.oQ >= i && i < m_dim.oQ + m_dim.nQ) {
      eraseq(i - m_dim.oQ);
    } else if (m_dim.oD >= i && i < m_dim.oD + m_dim.nD) {
      erased(i - m_dim.oD);
    }
  }

  void eraseq(size_t i) override {
    qspace.erase(i);
    remove_data(m_dim.oQ + i);
    update_dimensions();
  }

  void erasep(size_t i) override {
    pspace.erase(i);
    remove_data(m_dim.oP + i);
    update_dimensions();
  }

  void erased(size_t i) override {
    dspace.erase(i);
    remove_data(m_dim.oD + i);
    update_dimensions();
  }

  VecRef<P> paramsp() override { return pspace.params(); }
  VecRef<P> actionsp() override { return pspace.actions(); }
  VecRef<Q> paramsq() override { return qspace.params(); }
  VecRef<Q> actionsq() override { return qspace.actions(); }
  VecRef<Q> paramsd() override { return dspace.params(); }
  VecRef<Q> actionsd() override { return dspace.actions(); }

  CVecRef<P> paramsp() const override { return pspace.params(); }
  CVecRef<P> actionsp() const override { return pspace.actions(); }
  CVecRef<Q> paramsq() const override { return qspace.params(); }
  CVecRef<Q> actionsq() const override { return qspace.actions(); }
  CVecRef<Q> paramsd() const override { return dspace.cparams(); }
  CVecRef<Q> actionsd() const override { return dspace.cactions(); }

  CVecRef<P> cparamsp() const override { return pspace.cparams(); }
  CVecRef<P> cactionsp() const override { return pspace.cactions(); }
  CVecRef<Q> cparamsq() const override { return qspace.cparams(); }
  CVecRef<Q> cactionsq() const override { return qspace.cactions(); }
  CVecRef<Q> cparamsd() const override { return dspace.cparams(); }
  CVecRef<Q> cactionsd() const override { return dspace.cactions(); }

  PSpace<R, P> pspace;
  QSpace<R, Q, P> qspace;
  DSpace<Q> dspace;

protected:
  void update_dimensions() { m_dim = Dimensions(pspace.size(), qspace.size(), dspace.size()); }

  void remove_data(size_t i) {
    for (auto d : {EqnData::H, EqnData::S})
      data[d].remove_row_col(i, i);
  }
  std::shared_ptr<ArrayHandlers<R, Q, P>> m_handlers;
  std::shared_ptr<Logger> m_logger;
  Dimensions m_dim;
  bool m_hermitian = false; //!< whether the matrix is Hermitian
};

} // namespace molpro::linalg::itsolv::subspace
#endif // LINEARALGEBRA_SRC_MOLPRO_LINALG_ITSOLV_SUBSPACE_XSPACE_H
