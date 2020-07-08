#pragma once

#include <string>

#include "drake/common/drake_throw.h"
#include "drake/math/autodiff_gradient.h"
#include "drake/solvers/mathematical_program.h"
#include "drake/solvers/snopt_solver.h"
#include "multibody/multibody_utils.h"

#include "drake/common/drake_assert.h"
#include "examples/goldilocks_models/goldilocks_utils.h"
#include "examples/goldilocks_models/reduced_order_models.h"

#include "solvers/nonlinear_constraint.h"

#include "systems/trajectory_optimization/dircon_opt_constraints.h"

using std::map;
using std::string;
using std::vector;
using std::list;
using std::unique_ptr;
using std::make_unique;
using std::make_shared;
using std::isnan;
using std::isinf;

using Eigen::Dynamic;
using Eigen::AutoDiffScalar;
using Eigen::VectorXd;
using Eigen::Vector3d;
using Eigen::Matrix;
using Eigen::MatrixXd;
using drake::VectorX;
using drake::MatrixX;
using drake::AutoDiffVecXd;
using drake::AutoDiffXd;
using drake::math::DiscardGradient;
using drake::math::autoDiffToValueMatrix;
using drake::math::autoDiffToGradientMatrix;
using drake::math::initializeAutoDiff;
using drake::solvers::to_string;
using drake::solvers::VectorXDecisionVariable;
using drake::solvers::Constraint;
using drake::solvers::MathematicalProgram;
using drake::solvers::Constraint;
using drake::solvers::VariableRefList;
using drake::solvers::Binding;
using drake::symbolic::Variable;
using drake::symbolic::Expression;
using drake::multibody::MultibodyPlant;


namespace dairlib {
namespace goldilocks_models {
namespace find_models {

using dairlib::solvers::NonlinearConstraint;

class DynamicsConstraint : public NonlinearConstraint<double> {
 public:
  DynamicsConstraint(const ReducedOrderModel& rom,
                     const MultibodyPlant<double>& plant, bool is_head,
                     const std::string& description = "rom_dyn_constraint");

  // Getters
  VectorXd GetY(const VectorXd& q) const;
  VectorXd GetYdot(const VectorXd& x) const;
  VectorXd GetYddot(const VectorXd& y, const VectorXd& ydot,
                    const VectorXd& tau) const;

  MatrixXd getGradientWrtTheta(
    const VectorXd & x_i_double, const VectorXd & tau_i_double,
    const VectorXd & x_iplus1_double, const VectorXd & tau_iplus1_double,
    const VectorXd & h_i_double) const;

  // Extend the model by assuming the parameters of the new dynamics row are 0's
  // the new dynamics row = tau.
  VectorXd computeTauToExtendModel(
    const VectorXd & x_i_double, const VectorXd & x_iplus1_double,
    const VectorXd & h_i, const VectorXd & theta_y_append);

 private:
  void EvaluateConstraint(const Eigen::Ref<const drake::VectorX<double>>& x,
                          drake::VectorX<double>* y) const override;
  VectorXd EvalConstraintWithModelParams(
    const VectorXd & x_i, const VectorXd & tau_i,
    const VectorXd & x_iplus1, const VectorXd & tau_iplus1,
    const VectorXd & h_i,
    const VectorXd & theta_y, const VectorXd & theta_yddot) const;

  std::unique_ptr<ReducedOrderModel> rom_;

  int n_q_;
  int n_v_;
  int n_u_;
  int n_tau_;

  bool is_head_;

  // Finite differencing to get gradient of feature wrt q
  double eps_fd_feature_ = 1e-8;  // this is tuned. difference norm = 1e-8
  double eps_cd_feature_ = 1e-5;  // this is tuned. difference norm = 1e-11
  double eps_ho_feature_ = 1e-3;  // this is tuned. difference norm = 1e-12

  // Finite differencing to get gradient of constraints wrt theta
  double eps_fd_ = 1e-6;
  double eps_cd_ = 1e-4;
  double eps_ho_ = 1e-3;
  // The above number is tuned in getGradientWrtTheta(), and the result is:
  // 1e-6 good for fd
  // 1e-4 good for cd;  // B matrix error ~ 1e-13 to 1e-15
  // 1e-3 good for ho;
  vector<double> fd_shift_vec_{0, eps_fd_};  // forward difference
  vector<double> cd_shift_vec_{ -eps_cd_ / 2, eps_cd_ / 2};  // central difference
  vector<double> ho_shift_vec_{ -eps_ho_ / 2, -eps_ho_ / 4,
                                eps_ho_ / 4, eps_ho_ / 2};
};


////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Below is Autodiff version (DoEval is implemented in Autodiff one)

//class DynamicsConstraintAutodiffVersion : public Constraint {
// public:
//  DynamicsConstraintAutodiffVersion(int n_s, int n_feature_y,
//                                    const VectorXd & theta_y,
//                                    int n_yddot, int n_feature_yddot,
//                                    const VectorXd & theta_yddot,
//                                    int n_tau,
//                                    MatrixXd B_tau,
//                                    const MultibodyPlant<AutoDiffXd> * plant,
//                                    bool is_head,
//                                    int robot_option,
//                                    const std::string& description = "");
//  void DoEval(const Eigen::Ref<const Eigen::VectorXd>& q,
//              Eigen::VectorXd* y) const override;
//
//  void DoEval(const Eigen::Ref<const drake::AutoDiffVecXd>& qvtqvth,
//              drake::AutoDiffVecXd* y) const override;
//
//  void DoEval(const Eigen::Ref<const VectorX<Variable>>& q,
//              VectorX<Expression>*y) const override;
//
//  void getSAndSDot(const VectorXd & x,
//                   VectorXd & s, VectorXd & ds) const;
//
//  VectorXd getSDDot(const VectorXd & s, const VectorXd & ds,
//                    const VectorXd & tau) const {
//    return dyn_expression_.getExpression(theta_yddot_, s, ds, tau);
//  };
//
//  MatrixXd getGradientWrtTheta(
//    const VectorXd & x_i_double, const VectorXd & tau_i_double,
//    const VectorXd & x_iplus1_double, const VectorXd & tau_iplus1_double,
//    const VectorXd & h_i_double) const;
//
//  VectorXd getDynFeatures(const VectorXd & s, const VectorXd & ds,
//                          const VectorXd & tau) const {
//    return dyn_expression_.getFeature(s, ds);
//  };
//
//  // Extend the model by assuming the parameters of the new dynamics row are 0's
//  // the new dynamics row = tau.
//  VectorXd computeTauToExtendModel(
//    const VectorXd & x_i_double, const VectorXd & x_iplus1_double,
//    const VectorXd & h_i, const VectorXd & theta_y_append);
//
// private:
//  AutoDiffVecXd getConstraintValueInAutoDiff(
//    const AutoDiffVecXd & x_i, const AutoDiffVecXd & tau_i,
//    const AutoDiffVecXd & x_iplus1, const AutoDiffVecXd & tau_iplus1,
//    const AutoDiffVecXd & h_i,
//    const VectorXd & theta_y, const VectorXd & theta_yddot) const;
//  void getSAndSDotInAutoDiff(AutoDiffVecXd x_i,
//                             AutoDiffVecXd & s_i,
//                             AutoDiffVecXd & ds_i,
//                             const int & i_start,
//                             const VectorXd & theta_y) const;
//
//  VectorXd getConstraintValueInDouble(
//    const AutoDiffVecXd & x_i, const VectorXd & tau_i,
//    const AutoDiffVecXd & x_iplus1, const VectorXd & tau_iplus1,
//    const VectorXd & h_i,
//    const VectorXd & theta_y, const VectorXd & theta_yddot) const;
//  void getSAndSDotInDouble(AutoDiffVecXd x,
//                           VectorXd & s, VectorXd & ds,
//                           const int & i_start,
//                           const VectorXd & theta_y) const;
//
//  const MultibodyPlant<AutoDiffXd> * plant_;
//  int n_q_;
//  int n_v_;
//  int n_y_;
//  int n_feature_y_;
//  int n_theta_y_;
//  VectorXd theta_y_;
//  int n_yddot_;
//  int n_feature_yddot_;
//  int n_theta_yddot_;
//  VectorXd theta_yddot_;
//  int n_tau_;
//  KinematicsExpression<AutoDiffXd> kin_expression_;
//  DynamicsExpression dyn_expression_;
//  bool is_head_;
//  double eps_fd_ = 1e-6;
//  double eps_cd_ = 1e-4;
//  double eps_ho_ = 1e-3;
//  // The above number is tested in getGradientWrtTheta(), and the result is:
//  // 1e-6 good for fd
//  // 1e-4 good for cd;  // B matrix error ~ 1e-13 to 1e-15
//  // 1e-3 good for ho;
//  vector<double> fd_shift_vec_{0, eps_fd_};  // forward difference
//  vector<double> cd_shift_vec_{ -eps_cd_ / 2, eps_cd_ / 2};  // central difference
//  vector<double> ho_shift_vec_{ -eps_ho_ / 2, -eps_ho_ / 4,
//                                eps_ho_ / 4, eps_ho_ / 2};
//};

}  // namespace find_models
}  // namespace goldilocks_models
}  // namespace dairlib
