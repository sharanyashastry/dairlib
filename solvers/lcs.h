#pragma once
#include <vector>
#include <Eigen/Dense>

#include "solvers/c3_options.h"
#include "drake/solvers/mathematical_program.h"
#include "drake/solvers/osqp_solver.h"
#include "drake/solvers/solve.h"

namespace dairlib {
namespace solvers {
class LCS {
    public:

    /// Constructor for time-varying LCS
    /// @param A, B, D, d Dynamics constraints x_{k+1} = A_k x_k + B_k u_k + D_k \lambda_k + d_k
    /// @param E, F, H, c Complementarity constraints  0 <= \lambda_k \perp E_k x_k + F_k \lambda_k  + H_k u_k + c_k
    LCS(const std::vector<Eigen::MatrixXd>& A, const std::vector<Eigen::MatrixXd>& B,
       const std::vector<Eigen::MatrixXd>& D, const std::vector<Eigen::MatrixXd>& d,
       const std::vector<Eigen::MatrixXd>& E, const std::vector<Eigen::MatrixXd>& F,
       const std::vector<Eigen::MatrixXd>& H, const std::vector<Eigen::VectorXd>& c);

    /// Constructor for time-invariant LCS
    LCS(const Eigen::MatrixXd& A, const Eigen::MatrixXd& B,
    const Eigen::MatrixXd& D, const Eigen::MatrixXd& d,
    const Eigen::MatrixXd& E, const Eigen::MatrixXd& F,
    const Eigen::MatrixXd& H, const Eigen::VectorXd& c, const int& N);

    ///Simulate the system for one-step
    /// @param x_init Initial x value
    /// @param input Input value
    Eigen::VectorXd Simulate( Eigen::VectorXd& x_init, Eigen::VectorXd& input );

public:
    const std::vector<Eigen::MatrixXd> A_;
    const std::vector<Eigen::MatrixXd> B_;
    const std::vector<Eigen::MatrixXd> D_;
    const std::vector<Eigen::MatrixXd> d_;
    const std::vector<Eigen::MatrixXd> E_;
    const std::vector<Eigen::MatrixXd> F_;
    const std::vector<Eigen::MatrixXd> H_;
    const std::vector<Eigen::VectorXd> c_;
    const int N_;

};

} // namespace dairlib
} // namespace solvers
