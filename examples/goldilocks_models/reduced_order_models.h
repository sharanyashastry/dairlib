#pragma once

#include <iostream>
#include <set>
#include <memory>
#include <string>
#include <tuple>
#include <Eigen/Dense>

#include "systems/goldilocks_models/file_utils.h"
#include "drake/common/trajectories/piecewise_polynomial.h"
#include "drake/multibody/plant/multibody_plant.h"

namespace dairlib {
namespace goldilocks_models {

using BodyPoint =
    std::pair<const Eigen::Vector3d, const drake::multibody::Frame<double>&>;

class MonomialFeatures {
 public:
  /// Construct a basis features composed of monomials up to order `n_order`.
  /// `n_q` is the input size, and `skip_inds` denotes the indices of the input
  /// which are not used to construct the monomials.
  MonomialFeatures(int n_order, int n_q, std::vector<int> skip_inds = {},
                   const std::string& name = "");

  drake::VectorX<double> Eval(const drake::VectorX<double>& q) const;
  // Note that both EvalJV() and EvalJdotV() use qdot not v.
  // Also, we implement EvalJV instead of EvalJ to exploit the sparsity (though
  // not sure how much this helps)
  drake::VectorX<double> EvalJV(const drake::VectorX<double>& q,
                                const drake::VectorX<double>& qdot) const;
  drake::VectorX<double> EvalJdotV(const drake::VectorX<double>& q,
                                   const drake::VectorX<double>& qdot) const;

  void PrintSymbolicFeatures() const;
  void PrintSymbolicPartialDerivatives(int order) const;

  int length() const { return features_.size(); }

 private:
  static std::set<std::multiset<int>> ConstructSubfeaturesWithOneMoreOrder(
      const std::vector<int>& active_inds,
      const std::set<std::multiset<int>>& terms_of_same_order);

  // The name `EvalFeatureTimeDerivatives` could be misleading since JdotV is
  // not feature_ddot but the "bais term" of feature ddot.
  drake::VectorX<double> EvalFeatureTimeDerivatives(
      const drake::VectorX<double>& q, const drake::VectorX<double>& qdot,
      const std::map<std::pair<int, std::multiset<int>>,
                     std::pair<int, std::multiset<int>>>& partial_diff_map)
      const;

  static void PrintMultiset(const std::multiset<int>& set);

  int n_q_;
  std::string name_;

  // A list of features
  // We use a set of indices to represent each term. E.g. {1, 1, 2} represents
  // q1 * q1 * q2
  std::set<std::multiset<int>> features_;
  // A list of partial derivatives of features.
  // Let features_ijk denote the partial derivatives of i-th feature wrt j-th
  // and k-th element of q.
  // Then we use std::map to map the subscript ijk to the partial derivatives.
  // More specifically, we map pair<i, {jk}> to pair<coefficient, monomial>.
  // For example, map<{1, {4}}, {2, {1, 3}}> corresponds to the first feature's
  // partial derivatives wrt q4 and the expression is 2 * (q1*q3)
  std::map<std::pair<int, std::multiset<int>>,
           std::pair<int, std::multiset<int>>>
      first_ord_partial_diff_;
  std::map<std::pair<int, std::multiset<int>>,
           std::pair<int, std::multiset<int>>>
      second_ord_partial_diff_;
};

/// ReducedOrderModel assumes the following structures of mapping function and
/// dynamics function
///   y = r(q) = Theta_r * phi_r(q)
///   yddot = g(y, ydot, tau) = Theta_g * phi_g(y, ydot) + B * tau
/// For more detail, see equation 6 of
/// https://dair.seas.upenn.edu/wp-content/uploads/Chen2020.pdf
class ReducedOrderModel {
 public:
  ReducedOrderModel(int n_y, int n_tau, const Eigen::MatrixXd& B_tau,
                    int n_feature_y, int n_feature_yddot,
                    const MonomialFeatures& mapping_basis,
                    const MonomialFeatures& dynamic_basis,
                    const std::string& name = "");

  // Clone() is used for deep-copying a polymorphic object
  virtual std::unique_ptr<ReducedOrderModel> Clone() const = 0;

  // We must make the base class destructor virtual for Clone()
  // Otherwise, the computer doesn't call the derived class's destructor for the
  // polymorphic object
  virtual ~ReducedOrderModel() = default;

  // Getters
  const std::string& name() const { return name_; };
  int n_y() const { return n_y_; };
  int n_yddot() const { return n_y_; };
  int n_tau() const { return n_tau_; };
  int n_feature_y() const { return n_feature_y_; };
  int n_feature_yddot() const { return n_feature_yddot_; };
  const Eigen::MatrixXd& B() const { return B_tau_; };

  // Getters for basis functions
  const MonomialFeatures& mapping_basis() const { return mapping_basis_; };
  const MonomialFeatures& dynamic_basis() const { return dynamic_basis_; };

  // Getter/Setters for model parameters
  int n_theta_y() const { return theta_y_.size(); };
  int n_theta_yddot() const { return theta_yddot_.size(); };
  int n_theta() const { return theta_y_.size() + theta_yddot_.size(); };
  const Eigen::VectorXd& theta_y() const { return theta_y_; };
  const Eigen::VectorXd& theta_yddot() const { return theta_yddot_; };
  Eigen::VectorXd theta() const;
  void SetThetaY(const Eigen::VectorXd& theta_y);
  void SetThetaYddot(const Eigen::VectorXd& theta_yddot);
  void SetTheta(const Eigen::VectorXd& theta);

  // Evaluators for y, yddot, y's Jacobian and y's JdotV
  drake::VectorX<double> EvalMappingFunc(const drake::VectorX<double>& q) const;
  drake::VectorX<double> EvalDynamicFunc(
      const drake::VectorX<double>& y, const drake::VectorX<double>& ydot,
      const drake::VectorX<double>& tau) const;
  drake::VectorX<double> EvalMappingFuncJV(
      const drake::VectorX<double>& q, const drake::VectorX<double>& v) const;
  drake::VectorX<double> EvalDynamicFuncJdotV(
      const drake::VectorX<double>& q, const drake::VectorX<double>& v) const;

  // Evaluators for features of y, yddot, y's Jacobian and y's JdotV
  virtual drake::VectorX<double> EvalMappingFeat(
      const drake::VectorX<double>& q) const = 0;
  virtual drake::VectorX<double> EvalDynamicFeat(
      const drake::VectorX<double>& y,
      const drake::VectorX<double>& ydot) const = 0;
  virtual drake::VectorX<double> EvalMappingFeatJV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const = 0;
  virtual drake::VectorX<double> EvalDynamicFeatJdotV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const = 0;

  void CheckModelConsistency() const;

 private:
  std::string name_;
  int n_y_;
  int n_yddot_;
  int n_tau_;
  Eigen::MatrixXd B_tau_;

  int n_feature_y_;
  int n_feature_yddot_;

  const MonomialFeatures& mapping_basis_;
  const MonomialFeatures& dynamic_basis_;

  Eigen::VectorXd theta_y_;
  Eigen::VectorXd theta_yddot_;
};

/// Linear inverted pendulum model (either 2D or 3D, determined by `world_dim`)
class Lipm : public ReducedOrderModel {
 public:
  static int kDimension(int world_dim) {
    DRAKE_DEMAND((world_dim == 2) || (world_dim == 3));
    return world_dim;
  };

  Lipm(const drake::multibody::MultibodyPlant<double>& plant,
       const BodyPoint& stance_contact_point,
       const MonomialFeatures& mapping_basis,
       const MonomialFeatures& dynamic_basis, int world_dim);

  // Copy constructor for the Clone() method
  Lipm(const Lipm&);

  // Use covariant return type for Clone method. It's more useful.
  std::unique_ptr<ReducedOrderModel> Clone() const override {
    return std::make_unique<Lipm>(*this);
  }

  // Evaluators for features of y, yddot, y's Jacobian and y's JdotV
  drake::VectorX<double> EvalMappingFeat(
      const drake::VectorX<double>& q) const final;
  drake::VectorX<double> EvalDynamicFeat(
      const drake::VectorX<double>& y,
      const drake::VectorX<double>& ydot) const final;
  drake::VectorX<double> EvalMappingFeatJV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;
  drake::VectorX<double> EvalDynamicFeatJdotV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;

  // Getters for copy constructor
  const drake::multibody::MultibodyPlant<double>& plant() const {
    return plant_;
  };
  const drake::multibody::BodyFrame<double>& world() const { return world_; };
  const BodyPoint& stance_foot() const { return stance_contact_point_; };
  int world_dim() const { return world_dim_; };

 private:

  const drake::multibody::MultibodyPlant<double>& plant_;
  std::unique_ptr<drake::systems::Context<double>> context_;
  const drake::multibody::BodyFrame<double>& world_;
  // contact body frame and contact point of the stance foot
  const BodyPoint& stance_contact_point_;

  int world_dim_;
};

class TwoDimLipmWithSwingFoot : public ReducedOrderModel {
 public:
  static const int kDimension;

  TwoDimLipmWithSwingFoot(const drake::multibody::MultibodyPlant<double>& plant,
                          const BodyPoint& stance_contact_point,
                          const BodyPoint& swing_contact_point,
                          const MonomialFeatures& mapping_basis,
                          const MonomialFeatures& dynamic_basis);

  // Copy constructor for the Clone() method
  TwoDimLipmWithSwingFoot(const TwoDimLipmWithSwingFoot&);

  // Use covariant return type for Clone method. It's more useful.
  std::unique_ptr<ReducedOrderModel> Clone() const override {
    return std::make_unique<TwoDimLipmWithSwingFoot>(*this);
  }

  // Evaluators for features of y, yddot, y's Jacobian and y's JdotV
  drake::VectorX<double> EvalMappingFeat(
      const drake::VectorX<double>& q) const final;
  drake::VectorX<double> EvalDynamicFeat(
      const drake::VectorX<double>& y,
      const drake::VectorX<double>& ydot) const final;
  drake::VectorX<double> EvalMappingFeatJV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;
  drake::VectorX<double> EvalDynamicFeatJdotV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;

  // Getters for copy constructor
  const drake::multibody::MultibodyPlant<double>& plant() const {
    return plant_;
  };
  const drake::multibody::BodyFrame<double>& world() const { return world_; };
  const BodyPoint& stance_foot() const { return stance_contact_point_; };
  const BodyPoint& swing_foot() const { return swing_contact_point_; };

 private:

  const drake::multibody::MultibodyPlant<double>& plant_;
  std::unique_ptr<drake::systems::Context<double>> context_;
  const drake::multibody::BodyFrame<double>& world_;
  // contact body frame and contact point of the stance foot
  const BodyPoint& stance_contact_point_;
  // contact body frame and contact point of the swing foot
  const BodyPoint& swing_contact_point_;
};

class FixHeightAccel : public ReducedOrderModel {
 public:
  static const int kDimension;

  FixHeightAccel(const drake::multibody::MultibodyPlant<double>& plant,
                 const BodyPoint& stance_contact_point,
                 const MonomialFeatures& mapping_basis,
                 const MonomialFeatures& dynamic_basis);

  // Copy constructor for the Clone() method
  FixHeightAccel(const FixHeightAccel&);

  // Use covariant return type for Clone method. It's more useful.
  std::unique_ptr<ReducedOrderModel> Clone() const override {
    return std::make_unique<FixHeightAccel>(*this);
  }

  // Evaluators for features of y, yddot, y's Jacobian and y's JdotV
  drake::VectorX<double> EvalMappingFeat(
      const drake::VectorX<double>& q) const final;
  drake::VectorX<double> EvalDynamicFeat(
      const drake::VectorX<double>& y,
      const drake::VectorX<double>& ydot) const final;
  drake::VectorX<double> EvalMappingFeatJV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;
  drake::VectorX<double> EvalDynamicFeatJdotV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;

  // Getters for copy constructor
  const drake::multibody::MultibodyPlant<double>& plant() const {
    return plant_;
  };
  const drake::multibody::BodyFrame<double>& world() const { return world_; };
  const BodyPoint& stance_foot() const { return stance_contact_point_; };

 private:

  const drake::multibody::MultibodyPlant<double>& plant_;
  std::unique_ptr<drake::systems::Context<double>> context_;
  const drake::multibody::BodyFrame<double>& world_;
  // contact body frame and contact point of the stance foot
  const BodyPoint& stance_contact_point_;
};

class FixHeightAccelWithSwingFoot : public ReducedOrderModel {
 public:
  static const int kDimension;

  FixHeightAccelWithSwingFoot(
      const drake::multibody::MultibodyPlant<double>& plant,
      const BodyPoint& stance_contact_point,
      const BodyPoint& swing_contact_point,
      const MonomialFeatures& mapping_basis,
      const MonomialFeatures& dynamic_basis);

  // Copy constructor for the Clone() method
  FixHeightAccelWithSwingFoot(const FixHeightAccelWithSwingFoot&);

  // Use covariant return type for Clone method. It's more useful.
  std::unique_ptr<ReducedOrderModel> Clone() const override {
    return std::make_unique<FixHeightAccelWithSwingFoot>(*this);
  }

  // Evaluators for features of y, yddot, y's Jacobian and y's JdotV
  drake::VectorX<double> EvalMappingFeat(
      const drake::VectorX<double>& q) const final;
  drake::VectorX<double> EvalDynamicFeat(
      const drake::VectorX<double>& y,
      const drake::VectorX<double>& ydot) const final;
  drake::VectorX<double> EvalMappingFeatJV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;
  drake::VectorX<double> EvalDynamicFeatJdotV(
      const drake::VectorX<double>& q,
      const drake::VectorX<double>& v) const final;

  // Getters for copy constructor
  const drake::multibody::MultibodyPlant<double>& plant() const {
    return plant_;
  };
  const drake::multibody::BodyFrame<double>& world() const { return world_; };
  const BodyPoint& stance_foot() const { return stance_contact_point_; };
  const BodyPoint& swing_foot() const { return swing_contact_point_; };

 private:

  const drake::multibody::MultibodyPlant<double>& plant_;
  std::unique_ptr<drake::systems::Context<double>> context_;
  const drake::multibody::BodyFrame<double>& world_;
  // contact body frame and contact point of the stance foot
  const BodyPoint& stance_contact_point_;
  // contact body frame and contact point of the swing foot
  const BodyPoint& swing_contact_point_;
};

}  // namespace goldilocks_models
}  // namespace dairlib
