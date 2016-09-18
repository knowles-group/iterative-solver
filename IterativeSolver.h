#ifndef ITERATIVESOLVER_H
#define ITERATIVESOLVER_H
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <numeric>
#include "ParameterVector.h"
#include <Eigen/Dense>

namespace IterativeSolver {

/*!
 * \brief Place-holding template for residual calculation. It just returns the input as output.
 * \param inputs The parameters.
 * \param outputs On output, contains the corresponding residual vectors.
 * \param shift
 */
inline void noOp(const ParameterVectorSet & inputs, ParameterVectorSet & outputs, std::vector<ParameterScalar> shift=std::vector<ParameterScalar>()) { outputs=inputs; }
/*!
 * \brief Place-holding template for update calculation. It just returns the input as output.
 * \param inputs The parameters.
 * \param outputs On output, contains the corresponding residual vectors.
 * \param shift
 */
inline void steepestDescent(const ParameterVectorSet & inputs, ParameterVectorSet & outputs, std::vector<ParameterScalar> shift=std::vector<ParameterScalar>()) {
    for (size_t k=0; k<inputs.size(); k++)
        outputs[k].axpy(-1,inputs[k]);
}
/*!
 * \brief A base class for iterative solvers such as DIIS, KAIN, Davidson. The class provides support for preconditioned update, via a provided function.
 *
 * The user needs to provide the two routines residualFunction() and updateFunction() through the class constructor. These define the problem being solved: the first should calculate the residual
 * or action vector from a solution vector, and the second should update a provided solution vector using a provided residual vector.  The user also needs to provide an initial guess in the call to solve() or iterate().
 *
 * Two drivers are provided: the calling program can set up its own iterative loop, and in each loop call residualFunction() and iterate(); this gives the flexibility to pass additional parameters
 * to residualFunction(). The simpler mode of use is a single call to solve(), which manages the iterations itself.
 *
 * Classes that derive from this will, in the simplest case, need to provide just the extrapolate() method that governs how the solution and residual vectors from successive iterations
 * should be combined to form an optimum solution with minimal residual.  In more complicated cases - for example, in Davidson's method, where the preconditioner depends on the current energy -
 * it will be necessary to reimplement also the iterate() method.
 *
 * The underlying vector spaces are accessed through instances of the ParameterVectorSet class (or derivatives). These consist of a set of ParameterVector objects, which are the vectors themselves; the ParameterVectorSet
 * object has dimension greater than one in, for example, multi-root diagonalisations where the residual is calculated simultaneously for a number of vectors.
 * Two instances of ParameterVectorSet have to be passed to iterate() or solve(), and these are used to construct solutions and residuals; this class also creates unlimited additional instances of ParameterVectorSet,
 * and in memory-sensitive environments, consideration might be given to implementing a derivative of ParameterVectorSet where the data is resident in external storage.
 */
class IterativeSolverBase
{
protected:
  typedef void (*ParameterSetTransformation)(const ParameterVectorSet & inputs, ParameterVectorSet & outputs, std::vector<ParameterScalar> shift);
  /*!
   * \brief IterativeSolverBase
   * \param updateFunction A function that applies a preconditioner to a residual to give an update. Used by methods iterate() and solve().
   * \param residualFunction A function that evaluates the residual vectors. Used by method solve(); does not have to be provided if iterations are constructed explicitly in the calling program.
   */
  IterativeSolverBase(ParameterSetTransformation updateFunction=&IterativeSolver::steepestDescent, ParameterSetTransformation residualFunction=&IterativeSolver::noOp);
  virtual ~IterativeSolverBase();
protected:
  /*!
   * \brief The function that will take the current solution and residual, and produce the predicted solution.
   */
  ParameterSetTransformation m_updateFunction;
  /*!
   * \brief The function that will take a current solution and calculate the residual.
   */
  ParameterSetTransformation m_residualFunction;
public:
  /*!
   * \brief Take, typically, a current solution and residual, and return new solution.
   * In the context of Lanczos-like methods, the input will be a current expansion vector and the result of
   * acting on it with the matrix, and the output will be a new expansion vector.
   * iterate() saves the vectors, calls extrapolate(), calls m_updateFunction(), calls calculateErrors(), and then assesses the error.
   * Derivative classes may often be able to be implemented by changing only extrapolate(), not iterate() or solve().
   * \param residual On input, the residual for solution on entry. On exit, the extrapolated residual.
   * \param solution On input, the current solution or expansion vector. On exit, the next solution or expansion vector.
   * \param other Optional additional vectors that should be extrapolated.
   * \param options A string of options to be interpreted by extrapolate().
   */
  virtual bool iterate(ParameterVectorSet & residual, ParameterVectorSet & solution, ParameterVectorSet & other, std::string options="");
  virtual bool iterate(ParameterVectorSet & residual, ParameterVectorSet & solution, std::string options="") { ParameterVectorSet other; return iterate(residual,solution,other,options); }
  /*!
   * \brief Solve iteratively by repeated calls to residualFunction() and iterate().
   * \param residual Ignored on input; on exit, contains the final residual; used as working space.
   * \param solution On input, contains an initial guess; on exit, contains the final solution.
   * \param options A string of options to be interpreted by extrapolate().
   * \return Whether or not convergence has been reached.
   */
  virtual bool solve(ParameterVectorSet & residual, ParameterVectorSet & solution, std::string options="");
  /*!
   * \brief Set convergence threshold
   */

  virtual void moderateUpdate(ParameterVectorSet & solution);

  void setThresholds(double thresh) { m_thresh=thresh;}

public:
  int m_verbosity; //!< How much to print.
  int m_maxIterations; //!< Maximum number of iterations in solve()
  double m_thresh; //!< If predicted residual . solution is less than this, converged, irrespective of cthresh and gthresh.
  std::vector<double> m_errors; //!< Error at last iteration
  double m_error; //!< worst error at last iteration
  size_t m_worst; //!< worst-converged solution, ie m_error = m_errors[m_worst]

protected:
  virtual void extrapolate(ParameterVectorSet & residual, ParameterVectorSet & solution, ParameterVectorSet & other, std::string options="");
  virtual void extrapolate(ParameterVectorSet & residual, ParameterVectorSet & solution, std::string options="") { ParameterVectorSet other; extrapolate(residual,solution,other,options); }
  void calculateErrors(const ParameterVectorSet & solution);
  std::vector<ParameterVectorSet> m_residuals;
  std::vector<ParameterVectorSet> m_solutions;
  std::vector<ParameterVectorSet> m_others;
  size_t m_lastVectorIndex;
  std::vector<ParameterScalar> m_updateShift;
};
}


#endif // ITERATIVESOLVER_H
