#include "examples/goldilocks_models/planning/rom_traj_opt.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "drake/math/autodiff.h"
#include "drake/solvers/decision_variable.h"

#include "examples/goldilocks_models/planning/FoM_guard_constraint.h"
#include "examples/goldilocks_models/planning/FoM_reset_map_constraint.h"
#include "examples/goldilocks_models/planning/FoM_stance_foot_constraint.h"
#include "examples/goldilocks_models/planning/FoM_stride_length_constraint.h"
#include "examples/goldilocks_models/planning/dynamics_constraint.h"
#include "examples/goldilocks_models/planning/kinematics_constraint.h"

typedef std::numeric_limits<double> dbl;

namespace dairlib {
namespace goldilocks_models {

using std::cout;
using std::endl;
using std::pair;
using std::string;
using std::to_string;
using std::vector;

using Eigen::MatrixXd;
using Eigen::Vector3d;
using Eigen::VectorXd;

using drake::AutoDiffXd;
using drake::VectorX;
using drake::multibody::Frame;
using drake::multibody::MultibodyPlant;
using drake::solvers::Binding;
using drake::solvers::Constraint;
using drake::solvers::MathematicalProgram;
using drake::solvers::MathematicalProgramResult;
using drake::solvers::VectorXDecisionVariable;
using drake::symbolic::Expression;
using drake::systems::trajectory_optimization::MultipleShooting;
using drake::trajectories::PiecewisePolynomial;

// Note: In the zero foot impact case, adding post impact vel into decision
// variables (the so-called "slack variable") with linear constraint actually
// solves faster than without the variable. About 20% faster...

// We are currently using the same ROM input for pre/post impacts

RomTrajOpt::RomTrajOpt(
    const vector<int>& num_time_samples, const MatrixXd& Q, const MatrixXd& R,
    const ReducedOrderModel& rom, const MultibodyPlant<double>& plant,
    const StateMirror& state_mirror, const vector<BodyPoint>& left_contacts,
    const vector<BodyPoint>& right_contacts, const BodyPoint& left_origin,
    const BodyPoint& right_origin,
    const vector<std::tuple<string, double, double>>& fom_joint_name_lb_ub,
    VectorXd x_init, const std::vector<double>& max_swing_distance,
    bool start_with_left_stance, bool zero_touchdown_impact,
    const std::set<int>& relax_index, bool print_status)
    : MultipleShooting(
          rom.n_tau(), 2 * rom.n_y(),
          std::accumulate(num_time_samples.begin(), num_time_samples.end(), 0) -
              num_time_samples.size() + 1,
          1e-8, 1e8),
      num_modes_(num_time_samples.size()),
      mode_lengths_(num_time_samples),
      z_post_impact_vars_(NewContinuousVariables(
          (2 * rom.n_y()) * (num_time_samples.size() - 1), "xp")),
      x0_var_(NewContinuousVariables(
          (plant.num_positions() + plant.num_velocities()), "x0_FOM")),
      xf_vars_(NewContinuousVariables(
          (plant.num_positions() + plant.num_velocities()) *
              num_time_samples.size(),
          "xf_FOM")),
      v_post_impact_vars_(NewContinuousVariables(
          plant.num_velocities() * num_time_samples.size(), "vp_FOM")),
      impulse_vars_(zero_touchdown_impact
                        ? NewContinuousVariables(0, "Lambda_FOM")
                        : NewContinuousVariables(3 * left_contacts.size() *
                                                     num_time_samples.size(),
                                                 "Lambda_FOM")),
      n_y_(rom.n_y()),
      n_z_(2 * rom.n_y()),
      n_q_(plant.num_positions()),
      n_v_(plant.num_velocities()),
      n_x_(plant.num_positions() + plant.num_velocities()),
      n_lambda_(zero_touchdown_impact ? 0 : 3 * left_contacts.size()),
      plant_(plant),
      rom_(rom),
      start_with_left_stance_(start_with_left_stance),
      print_status_(print_status) {
  DRAKE_DEMAND(max_swing_distance.size() == num_time_samples.size());

  /// Some paramters
  double impulse_limit = 50;
  const double back_limit = -0.5;
  const double front_limit = 0.5;
  const double right_limit = 0.03;
  const double left_limit = 0.4;

  /// Setups
  PrintStatus("Getting things needed for costs and constraints");
  map<string, int> positions_map = multibody::makeNameToPositionsMap(plant);
  // Initial swing foot position
  Eigen::Vector3d swing_foot_init_pos;
  auto context = plant_.CreateDefaultContext();
  plant_.SetPositions(context.get(), x_init.head(n_q_));
  const auto& swing_origin =
      start_with_left_stance ? right_origin : left_origin;
  this->plant_.CalcPointsPositions(*context, swing_origin.second,
                                   swing_origin.first, plant_.world_frame(),
                                   &swing_foot_init_pos);
  // Friction cone constraint
  //     mu_*lambda_c(3*i+2) - lambda_c(3*i+0) >= 0
  //     mu_*lambda_c(3*i+2) + lambda_c(3*i+0) >= 0
  //     mu_*lambda_c(3*i+2) - lambda_c(3*i+1) >= 0
  //     mu_*lambda_c(3*i+2) + lambda_c(3*i+1) >= 0
  double mu = 1;
  MatrixXd A = MatrixXd::Zero(4, 3);
  A.block(0, 2, 4, 1) = mu * VectorXd::Ones(4, 1);
  A(0, 0) = -1;
  A(1, 0) = 1;
  A(2, 1) = -1;
  A(3, 1) = 1;
  auto friction_constraint = std::make_shared<drake::solvers::LinearConstraint>(
      A, VectorXd::Zero(4),
      std::numeric_limits<double>::infinity() * VectorXd::Ones(4));

  /// Adding costs and constraints
  // Add cost
  PrintStatus("Adding cost...");
  auto y = this->state();
  auto tau = this->input();
  this->DoAddRunningCost((y.tail(n_y_).transpose() * Q * y.tail(n_y_))(0, 0),
                         &rom_state_cost_bindings_);
  this->DoAddRunningCost((tau.transpose() * R * tau)(0, 0),
                         &rom_input_cost_bindings_);

  // Initial pose constraint for the full order model
  PrintStatus("Adding constraint -- initial pose of full-order model...");
  bool soft_init_constraint = false;
  if (soft_init_constraint) {
    // Relaxing only the velocity would help with the infeasibility issue,
    // because both pelvis and stance foot shift with the floating base
    // coordinates
    VectorXDecisionVariable x0 = x0_vars_by_mode(0);
    /// relax the state
    /* PrintStatus("(relax the whole state)");
    auto eps = NewContinuousVariables(n_x_, "eps_x0_FOM");
    MatrixXd Aeq = MatrixXd::Ones(1, 2);
    for (int i = 0; i < n_x_; i++) {
      AddLinearEqualityConstraint(
          Aeq, x_init.segment<1>(i),
          {x0.segment<1>(i), eps.segment<1>(i)});
    }
    MatrixXd Q_x0 = 100 * MatrixXd::Identity(n_x_, n_x_);
    VectorXd b_x0 = VectorXd::Zero(n_x_);
    x0_relax_cost_bindings_.push_back(AddQuadraticCost(Q_x0, b_x0, eps));
    SetInitialGuess(eps, VectorXd::Zero(n_x_));*/
    /// relax only the velocity
    // TODO: not sure why the runtime is so slow. maybe tune Q_v0?
    /*PrintStatus("(relax only the velocity)");
    int start_idx_v_relax = 3;
    int len_v_relax = 3;
    auto eps = NewContinuousVariables(len_v_relax, "eps_v0_FOM");
    const VectorXDecisionVariable& v0_float_vars =
        x0.segment(n_q + start_idx_v_relax, len_v_relax);
    const VectorXd v_init =
        x_init.segment(n_q + start_idx_v_relax, len_v_relax);
    MatrixXd Aeq = MatrixXd::Ones(1, 2);
    for (int i = 0; i < len_v_relax; i++) {
      AddLinearEqualityConstraint(
          Aeq, v_init.segment<1>(i),
          {v0_float_vars.segment<1>(i), eps.segment<1>(i)});
    }
    MatrixXd Q_v0 = 1 * MatrixXd::Identity(len_v_relax, len_v_relax);
    VectorXd b_v0 = VectorXd::Zero(len_v_relax);
    v0_relax_cost_bindings_.push_back(AddQuadraticCost(Q_v0, b_v0, eps));
    SetInitialGuess(eps, 0 * VectorXd::Ones(len_v_relax));
    // The rest of the state should be hard-constrained
    AddBoundingBoxConstraint(x_init.head(n_q + start_idx_v_relax),
                             x_init.head(n_q + start_idx_v_relax),
                             x0.head(n_q + start_idx_v_relax));
    AddBoundingBoxConstraint(
        x_init.segment(n_q + start_idx_v_relax + len_v_relax,
                       n_v - start_idx_v_relax - len_v_relax),
        x_init.segment(n_q + start_idx_v_relax + len_v_relax,
                       n_v - start_idx_v_relax - len_v_relax),
        x0.segment(n_q + start_idx_v_relax + len_v_relax,
                   n_v - start_idx_v_relax - len_v_relax));*/
  } else {
    AddBoundingBoxConstraint(x_init, x_init, x0_vars_by_mode(0));
    // AddLinearConstraint(x0_vars_by_mode(i)(0) == 0);
  }

  // Loop over modes to add more constraints
  int counter = 0;
  bool left_stance = start_with_left_stance;
  for (int i = 0; i < num_modes_; i++) {
    PrintStatus("Mode " + std::to_string(i) + "============================");
    mode_start_.push_back(counter);

    VectorXDecisionVariable x0 = x0_vars_by_mode(i);
    VectorXDecisionVariable xf = xf_vars_by_mode(i);
    VectorXDecisionVariable x0_post = x0_vars_by_mode(i + 1);

    // Testing -- penalize the velocity of the FOM states (help to
    // regularize)
    //    MatrixXd Q_v_FOM = 0.01 * MatrixXd::Identity(n_v, n_v);
    //    VectorXd b_v_FOM = VectorXd::Zero(n_v);
    // Version 1
    //    if (i != 0) {
    //      AddQuadraticCost(Q_v_FOM, b_v_FOM, x0.tail(n_v));
    //    }
    //    AddQuadraticCost(Q_v_FOM, b_v_FOM, xf.tail(n_v));
    // Version 2
    //    if (i == num_modes_ - 1) {
    //      AddQuadraticCost(Q_v_FOM, b_v_FOM, xf.tail(n_v));
    //    }

    // Add dynamics constraints at collocation points
    PrintStatus("Adding constraint -- bounding box on ROM state");
    for (int j = 0; j < mode_lengths_[i]; j++) {
      int time_index = mode_start_[i] + j;
      AddBoundingBoxConstraint(-10, 10, state_vars_by_mode(i, j));
    }

    // Add dynamics constraints at collocation points
    PrintStatus("Adding constraint -- dynamics");
    for (int j = 0; j < mode_lengths_[i] - 1; j++) {
      auto dyn_constraint = std::make_shared<planning::DynamicsConstraint>(
          rom, "rom_dyn_" + to_string(i) + "_" + to_string(j));
      DRAKE_DEMAND(static_cast<int>(dyn_constraint->num_constraints()) ==
                   num_states());
      dyn_constraint->SetConstraintScaling(rom_dyn_constraint_scaling_);
      int time_index = mode_start_[i] + j;
      AddConstraint(
          dyn_constraint,
          {state_vars_by_mode(i, j),
           u_vars().segment(time_index * num_inputs(), num_inputs()),
           state_vars_by_mode(i, j + 1),
           u_vars().segment((time_index + 1) * num_inputs(), num_inputs()),
           h_vars().segment(time_index, 1)});
    }

    // Add RoM-FoM mapping constraints
    // TODO: might need to rotate the local frame to align with the global
    std::set<int> empty_idx = {};
    if (i == 0) {
      PrintStatus(
          "Adding constraint -- RoM-FoM mapping (start of mode; relaxed)");
      int n_eps = relax_index.size();
      auto eps_rom = NewContinuousVariables(n_eps, "eps_rom");
      init_rom_relax_cost_bindings_.push_back(AddQuadraticCost(
          MatrixXd::Identity(n_eps, n_eps), VectorXd::Zero(n_eps), eps_rom));
      /* // "linear cost + lower bound" version
      if (relax_index.size() == 1 && *(relax_index.begin()) == 5) {
        init_rom_relax_cost_bindings_.push_back(AddLinearCost(eps_rom(0)));
        AddBoundingBoxConstraint(0, std::numeric_limits<double>::infinity(),
                                 eps_rom);
      } else {
        init_rom_relax_cost_bindings_.push_back(AddQuadraticCost(
            MatrixXd::Identity(n_eps, n_eps), VectorXd::Zero(n_eps), eps_rom));
      }*/
      auto kin_constraint_start =
          std::make_shared<planning::KinematicsConstraint>(
              rom, plant, left_stance, state_mirror, relax_index,
              "rom_fom_mapping_" + to_string(i) + "_start");
      kin_constraint_start->SetConstraintScaling(
          rom_fom_mapping_constraint_scaling_);
      VectorXDecisionVariable z_0 = state_vars_by_mode(i, 0);
      AddConstraint(kin_constraint_start, {z_0, x0, eps_rom});
    } else {
      PrintStatus("Adding constraint -- RoM-FoM mapping (start of mode)");
      auto kin_constraint_start =
          std::make_shared<planning::KinematicsConstraint>(
              rom, plant, left_stance, state_mirror, empty_idx,
              "rom_fom_mapping_" + to_string(i) + "_start");
      kin_constraint_start->SetConstraintScaling(
          rom_fom_mapping_constraint_scaling_);
      VectorXDecisionVariable z_0 = state_vars_by_mode(i, 0);
      AddConstraint(kin_constraint_start, {z_0, x0});
    }
    PrintStatus("Adding constraint -- RoM-FoM mapping (end of mode)");
    auto kin_constraint_end = std::make_shared<planning::KinematicsConstraint>(
        rom, plant, left_stance, state_mirror, empty_idx,
        "rom_fom_mapping_" + to_string(i) + "_end");
    kin_constraint_end->SetConstraintScaling(
        rom_fom_mapping_constraint_scaling_);
    VectorXDecisionVariable z_f = state_vars_by_mode(i, mode_lengths_[i] - 1);
    AddConstraint(kin_constraint_end, {z_f, xf});

    // Add guard constraint
    PrintStatus("Adding constraint -- guard");
    const auto& swing_contacts = left_stance ? right_contacts : left_contacts;
    //    if (i != num_modes_ - 1) {
    VectorXd lb_per_contact = VectorXd::Zero(2);
    if (!zero_touchdown_impact)
      lb_per_contact << 0, -std::numeric_limits<double>::infinity();
    VectorXd lb_guard(2 * swing_contacts.size());
    for (int i = 0; i < swing_contacts.size(); i++) {
      lb_guard.segment<2>(2 * i) = lb_per_contact;
    }
    VectorXd ub_guard = VectorXd::Zero(2 * swing_contacts.size());
    auto guard_constraint = std::make_shared<planning::FomGuardConstraint>(
        plant, swing_contacts, lb_guard, ub_guard, "fom_guard_" + to_string(i));
    guard_constraint->SetConstraintScaling(fom_guard_constraint_scaling_);
    AddConstraint(guard_constraint, xf);
    //    }

    // Add (impact) discrete map constraint
    if (zero_touchdown_impact) {
      PrintStatus("Adding constraint -- FoM identity reset map");
      // TODO: could use a more specific API so that drake doesn't have to
      //  parse the expression
      AddLinearConstraint(xf.segment(n_q_, n_v_) ==
                          x0_post.segment(n_q_, n_v_));
    } else {
      PrintStatus("Adding constraint -- FoM identity impact map");
      auto reset_map_constraint =
          std::make_shared<planning::FomResetMapConstraint>(
              plant_, swing_contacts, "fom_discrete_dyn_" + to_string(i));
      reset_map_constraint->SetConstraintScaling(
          fom_discrete_dyn_constraint_scaling_);
      VectorXDecisionVariable Lambda = impulse_vars(i);
      AddConstraint(reset_map_constraint, {xf, x0_post.tail(n_v_), Lambda});

      // Constraint on impact impulse
      PrintStatus("Adding constraint -- FoM impulse friction");
      for (int k = 0; k < swing_contacts.size(); k++) {
        AddConstraint(friction_constraint, Lambda.segment(3 * k, 3));
      }
      PrintStatus("Adding constraint -- bounding box on FoM impulse");
      for (int k = 0; k < swing_contacts.size(); k++) {
        AddBoundingBoxConstraint(-impulse_limit, impulse_limit,
                                 Lambda(3 * k + 0));
        AddBoundingBoxConstraint(-impulse_limit, impulse_limit,
                                 Lambda(3 * k + 1));
        AddBoundingBoxConstraint(0, impulse_limit, Lambda(3 * k + 2));
      }

      // SetInitialGuess(Lambda, VectorXd::Zero(n_Lambda));

      // Regularization term (there is a 1DoF null space in the force)
      //  MatrixXd Q_lambda = 0.01 * MatrixXd::Identity(n_Lambda, n_Lambda);
      //  VectorXd b_lambda = VectorXd::Zero(n_Lambda);
      //  AddQuadraticCost(Q_lambda, b_lambda, Lambda);

      // debugging
      // Somehow this speed up the solve...???
      //        PrintStatus("Debugging -- encourage contact force...");
      //        lambda_cost_bindings_.push_back(
      //            AddLinearCost(-1 * (Lambda(2) + Lambda(5))));
    }

    /// Constraints on FOM states
    // Note that we don't impose constraint on the initial state (because it's
    // constrained already)

    // Quaternion unit norm constraint (it solves faster with this constraint)
    PrintStatus("Adding constraint -- full-order model unit norm quaternion");
    auto quat_norm_constraint =
        std::make_shared<drake::solvers::QuadraticConstraint>(
            2 * MatrixXd::Identity(4, 4), VectorXd::Zero(4), 1, 1);
    AddConstraint(quat_norm_constraint, xf.head(4));

    // Full order model joint limits
    PrintStatus("Adding constraint -- full-order model joint limit");
    for (const auto& name_lb_ub : fom_joint_name_lb_ub) {
      AddBoundingBoxConstraint(std::get<1>(name_lb_ub), std::get<2>(name_lb_ub),
                               xf(positions_map.at(std::get<0>(name_lb_ub))));
    }
    PrintStatus(
        "Adding constraint -- full-order model floating base pos (with "
        "heuristics!)");
    // TODO: make a bound on the quaternion. E.g. We don't want the robot to
    //  turn to the back.
    AddBoundingBoxConstraint(0, 1, xf(0));              // qw
    AddBoundingBoxConstraint(-1, 1, xf.segment<3>(1));  // qx, qy, qz
    AddBoundingBoxConstraint(-2, 2, xf.segment<2>(4));  // x,y
    // Heuristics -- prevent the pelvis go too low
    AddBoundingBoxConstraint(0.5, 1.1, xf.segment<1>(6));  // z

    // Full order model vel limits
    PrintStatus("Adding constraint -- full-order model generalized vel");
    AddBoundingBoxConstraint(-2, 2, xf.segment<6>(n_q_));
    AddBoundingBoxConstraint(-2, 2, x0_post.segment<6>(n_q_));
    AddBoundingBoxConstraint(-10, 10, xf.tail(n_v_ - 6));
    AddBoundingBoxConstraint(-10, 10, x0_post.tail(n_v_ - 6));

    // Stitching x0 and xf (full-order model stance foot constraint)
    PrintStatus("Adding constraint -- full-order model stance foot pos");
    const auto& stance_contacts = left_stance ? left_contacts : right_contacts;
    auto fom_sf_pos_constraint =
        std::make_shared<planning::FomStanceFootPosConstraint>(
            plant_, stance_contacts, "fom_stance_ft_pos_" + to_string(i));
    fom_sf_pos_constraint->SetConstraintScaling(
        fom_stance_ft_pos_constraint_scaling_);
    AddConstraint(fom_sf_pos_constraint, {x0.head(n_q_), xf.head(n_q_)});

    // Zero velocity for stance foot
    PrintStatus("Adding constraint -- full-order model stance foot vel");
    auto fom_ft_vel_constraint_preimpact =
        std::make_shared<planning::FomStanceFootVelConstraint>(
            plant_, stance_contacts,
            "fom_stance_ft_vel_" + to_string(i) + "_preimpact");
    fom_ft_vel_constraint_preimpact->SetConstraintScaling(
        fom_stance_ft_vel_constraint_scaling_);
    AddConstraint(fom_ft_vel_constraint_preimpact, xf);
    const auto& post_stance_contacts =
        left_stance ? right_contacts : left_contacts;
    auto fom_ft_vel_constraint_postimpact =
        std::make_shared<planning::FomStanceFootVelConstraint>(
            plant_, post_stance_contacts,
            "fom_stance_ft_vel_" + to_string(i) + "_postimpact");
    fom_ft_vel_constraint_postimpact->SetConstraintScaling(
        fom_stance_ft_vel_constraint_scaling_);
    AddConstraint(fom_ft_vel_constraint_postimpact, x0_post);

    // Foot collision avoidance (full-order model swing foot constraint)
    const auto& swing_origin = left_stance ? right_origin : left_origin;
    Eigen::Vector2d lb_swing(back_limit,
                             left_stance ? -left_limit : right_limit);
    Eigen::Vector2d ub_swing(front_limit,
                             left_stance ? -right_limit : left_limit);
    PrintStatus(
        "Adding constraint -- FOM swing collision avoidance (end of mode)");
    auto fom_sw_ft_pos_constraint =
        std::make_shared<planning::FomSwingFootPosConstraint>(
            plant_, plant.GetFrameByName("pelvis"), swing_origin, lb_swing,
            ub_swing, "fom_swing_ft_pos_" + to_string(i));
    AddConstraint(fom_sw_ft_pos_constraint, xf.head(n_q_));

    // Foot travel distance constraint (full-order model swing foot constraint)
    PrintStatus("Adding constraint -- FOM swing foot travel distance");
    auto fom_sw_ft_dist_constraint =
        std::make_shared<planning::FomSwingFootDistanceConstraint>(
            plant_, swing_origin, swing_foot_init_pos, max_swing_distance.at(i),
            i == 0, "fom_swing_ft_dist_constraint" + to_string(i));
    if (i == 0) {
      AddConstraint(fom_sw_ft_dist_constraint, xf.head(n_q_));
    } else {
      AddConstraint(fom_sw_ft_dist_constraint, {x0.head(n_q_), xf.head(n_q_)});
    }

    // Stride length constraint
    // cout << "Adding stride length constraint for full-order model...\n";
    // V1
    // AddLinearConstraint(xf(0) - x0(0) ==
    // 0.304389); V2
    /*VectorXd stride_length(1); stride_length << 0.304389 * 2;
    auto fom_sl_constraint =
    std::make_shared<planning::FomStrideLengthConstraint>( left_stance, n_q_,
    stride_length); AddConstraint(fom_sl_constraint,
    {x0.head(n_q_), xf.head(n_q_)
                                     });*/

    // Stride length cost
    /*if (i == num_modes_ - 1) {
      cout << "Adding final position cost for full-order model...\n";
      this->AddLinearCost(-10 * xf(0));
    }*/

    counter += mode_lengths_[i] - 1;
    left_stance = !left_stance;
  }
}

void addConstraintScaling(std::unordered_map<int, double>* map,
                          vector<int> idx_vec, vector<double> s_vec) {
  DRAKE_DEMAND(idx_vec.size() == s_vec.size());
  for (int i = 0; i < idx_vec.size(); i++) {
    int idx = idx_vec[i];
    double s = s_vec[i];

    DRAKE_DEMAND(0 <= idx);
    DRAKE_DEMAND(0 < s);
    if (map->find(idx) != map->end()) {
      // Update the scaling factor
      (*map)[idx] = s;
    } else {
      // Add a new scaling factor
      map->insert(std::pair<int, double>(idx, s));
    }
  }
}

std::vector<int> CreateIdxVector(int size) {
  vector<int> ret(size);
  for (int i = 0; i < size; i++) {
    ret[i] = i;
  }
  return ret;
}

void RomTrajOpt::SetScalingForLIPM() {
  addConstraintScaling(
      &fom_discrete_dyn_constraint_scaling_, CreateIdxVector(18),
      {0.256749956352507, 0.256749956352507, 0.576854298141375,
       0.030298256032383, 0.030298256032383, 0.030298256032383,
       0.599067850424739, 0.807943702482811, 1.1232888099092, 0.779696697984484,
       0.764239696138297, 0.718478549822895, 1.16295973251926, 1.09613666631956,
       2.15622729223133, 3.78941464911915, 9.09810486475667, 61.721918070326});
  addConstraintScaling(&fom_guard_constraint_scaling_, CreateIdxVector(4),
                       {1, 0.040500915320686, 1, 0.038541734917656});
  addConstraintScaling(&fom_stance_ft_pos_constraint_scaling_,
                       CreateIdxVector(6),
                       {0.523823492435989, 0.523823492435989, 1,
                        0.52382074853985, 0.52382074853985, 0.884415710760686});
  addConstraintScaling(
      &fom_stance_ft_vel_constraint_scaling_, CreateIdxVector(6),
      {0.28070333026431, 0.114098983149862, 0.288711940548437,
       0.254999260502145, 0.107781849536538, 0.207878166764023});
  addConstraintScaling(
      &rom_dyn_constraint_scaling_, CreateIdxVector(6),
      {0.02775672892501, 0.02775672892501, 0.027777777777778, 0.005674724775848,
       0.006428925019448, 0.027777777777778});
  addConstraintScaling(
      &rom_fom_mapping_constraint_scaling_, CreateIdxVector(6),
      {0.600254507911354, 0.600254507911354, 1, 0.277406361482681,
       0.127149946660597, 0.324725931313971});
}

void RomTrajOpt::AddTimeStepConstraint(
    std::vector<double> minimum_timestep, std::vector<double> maximum_timestep,
    bool fix_duration, bool equalize_timestep_size, double first_mode_duration,
    double remaining_mode_duration_per_mode) {
  if (fix_duration && equalize_timestep_size) {
    double dt_first_mode = first_mode_duration / (mode_lengths_[0] - 1);
    PrintStatus("Fix all timestep size in the first mode " +
                to_string(dt_first_mode));
    for (int i = 0; i < mode_lengths_[0] - 1; i++) {
      AddBoundingBoxConstraint(dt_first_mode, dt_first_mode, timestep(i));
    }
    if (num_modes_ > 1) {
      double dt_rest_of_modes =
          remaining_mode_duration_per_mode / (mode_lengths_[1] - 1);
      PrintStatus("Fix all timestep size in the rest of the modes to " +
                  to_string(dt_rest_of_modes));
      for (int i = mode_lengths_[0] - 1; i < this->N() - 1; i++) {
        AddBoundingBoxConstraint(dt_rest_of_modes, dt_rest_of_modes,
                                 timestep(i));
      }
    }
  } else {
    for (int i = 0; i < num_modes_; i++) {
      // Set timestep bounds
      for (int j = 0; j < mode_lengths_[i] - 1; j++) {
        AddBoundingBoxConstraint(minimum_timestep[i], maximum_timestep[i],
                                 timestep(mode_start_[i] + j));
      }

      // all timesteps within a mode must be equal
      for (int j = 0; j < mode_lengths_[i] - 2; j++) {
        AddLinearConstraint(timestep(mode_start_[i] + j) ==
                            timestep(mode_start_[i] + j + 1));
      }
    }

    // Duration bound
    if (fix_duration) {
      double duration = first_mode_duration +
                        remaining_mode_duration_per_mode * (num_modes_ - 1);
      PrintStatus("Fix time duration: total duration = " + to_string(duration));
      AddDurationBounds(duration, duration);
    }

    // Make the timesteps between modes the same (except the first one)
    if (equalize_timestep_size) {
      PrintStatus("Equalize time steps between modes (except the first one)");
      for (int i = 2; i < num_modes_; i++) {
        if (mode_start_[i] > 0) {
          AddLinearConstraint(timestep(mode_start_[i] - 1) ==
                              timestep(mode_start_[i]));
        }
      }
    }
  }
}

const Eigen::VectorBlock<const VectorXDecisionVariable>
RomTrajOpt::z_post_impact_vars_by_mode(int mode) const {
  return z_post_impact_vars_.segment(mode * n_z_, n_z_);
}
VectorXDecisionVariable RomTrajOpt::x0_vars_by_mode(int mode) const {
  if (mode == 0) {
    return x0_var_;
  } else {
    VectorXDecisionVariable ret(n_x_);
    ret << xf_vars_.segment(n_x_ * (mode - 1), plant_.num_positions()),
        v_post_impact_vars_.segment(plant_.num_velocities() * (mode - 1),
                                    plant_.num_velocities());
    return ret;
  }
}
const Eigen::VectorBlock<const VectorXDecisionVariable>
RomTrajOpt::xf_vars_by_mode(int mode) const {
  return xf_vars_.segment(mode * n_x_, n_x_);
}

VectorX<Expression> RomTrajOpt::SubstitutePlaceholderVariables(
    const VectorX<Expression>& f, int interval_index) const {
  VectorX<Expression> ret(f.size());
  for (int i = 0; i < f.size(); i++) {
    ret(i) =
        MultipleShooting::SubstitutePlaceholderVariables(f(i), interval_index);
  }
  return ret;
}

// Eigen::VectorBlock<const VectorXDecisionVariable>
// RomTrajOpt::state_vars_by_mode(int mode, int
// time_index)  {
VectorXDecisionVariable RomTrajOpt::state_vars_by_mode(int mode,
                                                       int time_index) const {
  if (time_index == 0 && mode > 0) {
    return z_post_impact_vars_by_mode(mode - 1);
  } else {
    VectorXDecisionVariable ret(num_states());
    return x_vars().segment((mode_start_[mode] + time_index) * num_states(),
                            num_states());
    // std::cout << Eigen::VectorBlock<VectorXDecisionVariable>(ret, 0,
    // num_states())  << std::endl; return
    // Eigen::VectorBlock<VectorXDecisionVariable>(ret, 0, num_states());
  }
}

drake::solvers::VectorXDecisionVariable RomTrajOpt::impulse_vars(
    int mode) const {
  return impulse_vars_.segment(n_lambda_ * mode, n_lambda_);
}

// TODO: need to configure this to handle the hybrid discontinuities properly
void RomTrajOpt::DoAddRunningCost(const drake::symbolic::Expression& g) {
  // Trapezoidal integration:
  //    sum_{i=0...N-2} h_i/2.0 * (g_i + g_{i+1}), or
  // g_0*h_0/2.0 + [sum_{i=1...N-2} g_i*(h_{i-1} + h_i)/2.0] +
  // g_{N-1}*h_{N-2}/2.0.

  AddCost(MultipleShooting::SubstitutePlaceholderVariables(g, 0) * h_vars()(0) /
          2);
  for (int i = 1; i <= N() - 2; i++) {
    AddCost(MultipleShooting::SubstitutePlaceholderVariables(g, i) *
            (h_vars()(i - 1) + h_vars()(i)) / 2);
  }
  AddCost(MultipleShooting::SubstitutePlaceholderVariables(g, N() - 1) *
          h_vars()(N() - 2) / 2);
}

// TODO: need to configure this to handle the hybrid discontinuities properly
void RomTrajOpt::DoAddRunningCost(const drake::symbolic::Expression& g,
                                  std::vector<Binding<Cost>>* bindings) {
  // Trapezoidal integration:
  //    sum_{i=0...N-2} h_i/2.0 * (g_i + g_{i+1}), or
  // g_0*h_0/2.0 + [sum_{i=1...N-2} g_i*(h_{i-1} + h_i)/2.0] +
  // g_{N-1}*h_{N-2}/2.0.

  bindings->push_back(
      AddCost(MultipleShooting::SubstitutePlaceholderVariables(g, 0) *
              h_vars()(0) / 2));
  for (int i = 1; i <= N() - 2; i++) {
    bindings->push_back(
        AddCost(MultipleShooting::SubstitutePlaceholderVariables(g, i) *
                (h_vars()(i - 1) + h_vars()(i)) / 2));
  }
  bindings->push_back(
      AddCost(MultipleShooting::SubstitutePlaceholderVariables(g, N() - 1) *
              h_vars()(N() - 2) / 2));
}

void RomTrajOpt::GetStateAndDerivativeSamples(
    const drake::solvers::MathematicalProgramResult& result,
    std::vector<Eigen::MatrixXd>* state_samples,
    std::vector<Eigen::MatrixXd>* derivative_samples,
    std::vector<Eigen::VectorXd>* state_breaks) const {
  DRAKE_ASSERT(state_samples->empty());
  DRAKE_ASSERT(derivative_samples->empty());
  DRAKE_ASSERT(state_breaks->empty());

  VectorXd times(GetSampleTimes(result));

  for (int i = 0; i < num_modes_; i++) {
    MatrixXd states_i(num_states(), mode_lengths_[i]);
    MatrixXd derivatives_i(num_states(), mode_lengths_[i]);
    VectorXd times_i(mode_lengths_[i]);
    for (int j = 0; j < mode_lengths_[i]; j++) {
      int k_data = mode_start_[i] + j;

      VectorX<double> zk = result.GetSolution(state_vars_by_mode(i, j));
      VectorX<double> tauk = result.GetSolution(input(k_data));

      // z = [y; ydot]
      // Calculate zdot.
      // Copied from: examples/goldilocks_models/planning/dynamics_constraint.h
      VectorX<double> zdot(n_z_);
      zdot << zk.tail(n_y_),
          rom_.EvalDynamicFunc(zk.head(n_y_), zk.tail(n_y_), tauk);

      states_i.col(j) = drake::math::DiscardGradient(zk);
      derivatives_i.col(j) = drake::math::DiscardGradient(zdot);
      times_i(j) = times(k_data);
    }
    state_samples->push_back(states_i);
    derivative_samples->push_back(derivatives_i);
    state_breaks->push_back(times_i);
  }
}

void RomTrajOpt::GetStateSamples(
    const drake::solvers::MathematicalProgramResult& result,
    std::vector<Eigen::MatrixXd>* state_samples,
    std::vector<Eigen::VectorXd>* state_breaks) const {
  DRAKE_ASSERT(state_samples->empty());
  DRAKE_ASSERT(state_breaks->empty());

  VectorXd times(GetSampleTimes(result));

  for (int i = 0; i < num_modes_; i++) {
    MatrixXd states_i(num_states(), mode_lengths_[i]);
    VectorXd times_i(mode_lengths_[i]);
    for (int j = 0; j < mode_lengths_[i]; j++) {
      int k_data = mode_start_[i] + j;

      VectorX<double> zk = result.GetSolution(state_vars_by_mode(i, j));

      states_i.col(j) = drake::math::DiscardGradient(zk);
      times_i(j) = times(k_data);
    }
    state_samples->push_back(states_i);
    state_breaks->push_back(times_i);
  }
}

PiecewisePolynomial<double> RomTrajOpt::ReconstructInputTrajectory(
    const MathematicalProgramResult& result) const {
  Eigen::VectorXd times = GetSampleTimes(result);
  vector<double> times_vec(N());
  vector<Eigen::MatrixXd> inputs(N());
  for (int i = 0; i < N(); i++) {
    times_vec[i] = times(i);
    inputs[i] = result.GetSolution(input(i));
  }
  return PiecewisePolynomial<double>::FirstOrderHold(times_vec, inputs);
}

PiecewisePolynomial<double> RomTrajOpt::ReconstructStateTrajectory(
    const MathematicalProgramResult& result) const {
  VectorXd times_all(GetSampleTimes(result));
  VectorXd times(N() + num_modes_ - 1);

  MatrixXd states(num_states(), N() + num_modes_ - 1);
  MatrixXd inputs(num_inputs(), N() + num_modes_ - 1);
  MatrixXd derivatives(num_states(), N() + num_modes_ - 1);

  for (int i = 0; i < num_modes_; i++) {
    for (int j = 0; j < mode_lengths_[i]; j++) {
      int k = mode_start_[i] + j + i;
      int k_data = mode_start_[i] + j;
      times(k) = times_all(k_data);

      // False timestep to match velocities
      if (i > 0 && j == 0) {
        times(k) += +1e-6;
      }
      VectorX<double> yk = result.GetSolution(state_vars_by_mode(i, j));
      VectorX<double> tauk = result.GetSolution(input(k_data));
      states.col(k) = yk;
      inputs.col(k) = tauk;

      // TODO(yminchen): need to modify the following code
      /*auto context = multibody::createContext(plant_, yk, tauk);
      constraints_[i]->updateData(*context, result.GetSolution(force(i, j)));
      derivatives.col(k) =
        drake::math::DiscardGradient(constraints_[i]->getXDot());*/
      DRAKE_UNREACHABLE();  // Put it here to test where we are using this.
    }
  }
  // return PiecewisePolynomial<double>::CubicHermite(times, states,
  // derivatives);
  return PiecewisePolynomial<double>::FirstOrderHold(times, states);
}

RomTrajOptCassie::RomTrajOptCassie(
    const std::vector<int>& num_time_samples, const Eigen::MatrixXd& Q,
    const Eigen::MatrixXd& R, const ReducedOrderModel& rom,
    const drake::multibody::MultibodyPlant<double>& plant,
    const StateMirror& state_mirror, const vector<BodyPoint>& left_contacts,
    const vector<BodyPoint>& right_contacts, const BodyPoint& left_origin,
    const BodyPoint& right_origin,
    const vector<std::tuple<string, double, double>>& fom_joint_name_lb_ub,
    Eigen::VectorXd x_init, const std::vector<double>& max_swing_distance,
    bool start_with_left_stance, bool zero_touchdown_impact,
    const std::set<int>& relax_index, bool print_status)
    : RomTrajOpt(num_time_samples, Q, R, rom, plant, state_mirror,
                 left_contacts, right_contacts, left_origin, right_origin,
                 fom_joint_name_lb_ub, x_init, max_swing_distance,
                 start_with_left_stance, zero_touchdown_impact, relax_index,
                 print_status) {}

void RomTrajOptCassie::AddRegularizationCost(
    const std::vector<Eigen::VectorXd>& des_xy_pos,
    const Eigen::VectorXd& x_guess_left_in_front_pre,
    const Eigen::VectorXd& x_guess_right_in_front_pre,
    const Eigen::VectorXd& x_guess_left_in_front_post,
    const Eigen::VectorXd& x_guess_right_in_front_post, double w_reg_quat,
    double w_reg_xy, double w_reg_z, double w_reg_joints, double w_reg_hip_yaw,
    bool straight_leg_cost) {
  PrintStatus("Adding regularization cost ...");

  // Adding cost on FOM state increases convergence rate
  // If we only add position (not velocity) in the cost, then higher cost
  // results in spacing out each step more evenly
  MatrixXd Id_quat = w_reg_quat * MatrixXd::Identity(4, 4);
  MatrixXd Id_xy = w_reg_xy * MatrixXd::Identity(2, 2);
  MatrixXd Id_z = w_reg_z * MatrixXd::Identity(1, 1);
  MatrixXd Id_joints = w_reg_joints * MatrixXd::Identity(n_q_ - 7, n_q_ - 7);
  Id_joints(2, 2) = w_reg_hip_yaw;  // left hip yaw
  Id_joints(3, 3) = w_reg_hip_yaw;  // right hip yaw
  //  MatrixXd Id_vel = 0.0 * MatrixXd::Identity(n_v_, n_v_);
  MatrixXd Id_x_vel = 0.1 * MatrixXd::Identity(1, 1);

  bool left_stance = start_with_left_stance_;
  for (int i = 0; i < num_modes_; i++) {
    auto x_preimpact = xf_vars_by_mode(i);
    auto x_postimpact = x0_vars_by_mode(i + 1);

    const VectorXd& x_guess_pre =
        left_stance ? x_guess_right_in_front_pre : x_guess_left_in_front_pre;
    const VectorXd& x_guess_post =
        left_stance ? x_guess_right_in_front_post : x_guess_left_in_front_post;

    // 1. Position
    fom_reg_z_cost_bindings_.push_back(AddQuadraticErrorCost(
        Id_z, x_guess_pre.segment<1>(6), x_preimpact.segment<1>(6)));
    fom_reg_joint_cost_bindings_.push_back(
        AddQuadraticErrorCost(Id_joints, x_guess_pre.segment(7, n_q_ - 7),
                              x_preimpact.segment(7, n_q_ - 7)));
    fom_reg_xy_cost_bindings_.push_back(AddQuadraticErrorCost(
        Id_xy, des_xy_pos.at(i + 1), x_preimpact.segment<2>(4)));
    VectorX<double> quat_identity(4);
    quat_identity << 1, 0, 0, 0;
    fom_reg_quat_cost_bindings_.push_back(
        AddQuadraticErrorCost(Id_quat, quat_identity, x_preimpact.head(4)));

    // 2. Velocity
    // Preimpact
    /*fom_reg_vel_cost_bindings_.push_back(AddQuadraticErrorCost(
        Id_vel, x_guess_pre.segment(n_q_, n_v_),
        x_preimpact.segment(n_q_, n_v_)));*/
    fom_reg_vel_cost_bindings_.push_back(
        AddQuadraticErrorCost(Id_x_vel, x_guess_pre.segment(n_q_ + 3, 1),
                              x_preimpact.segment(n_q_ + 3, 1)));
    // Postimpact
    /*fom_reg_vel_cost_bindings_.push_back(AddQuadraticErrorCost(
        Id_vel, x_guess_post.segment(n_q_, n_v_),
        x_postimpact.segment(n_q_, n_v_)));*/
    fom_reg_vel_cost_bindings_.push_back(
        AddQuadraticErrorCost(Id_x_vel, x_guess_post.segment(n_q_ + 3, 1),
                              x_postimpact.segment(n_q_ + 3, 1)));

    left_stance = !left_stance;
  }

  // Note: Cassie can exploit the "one-contact per foot" constraint to lean
  // forward at the end pose, so we add a hard constraint on quaternion here
  /*PrintStatus("Adding constraint -- quaternion of the final pose...");
  VectorX<double> quat_identity(4);
  quat_identity << 1, 0, 0, 0;
  AddBoundingBoxConstraint(quat_identity, quat_identity,
                           xf_vars_by_mode(num_modes_ - 1).head(4));*/
}

void RomTrajOptCassie::SetHeuristicInitialGuess(
    const Eigen::VectorXd& h_guess, const Eigen::MatrixXd& r_guess,
    const Eigen::MatrixXd& dr_guess, const Eigen::MatrixXd& tau_guess,
    const Eigen::VectorXd& x_guess_left_in_front_pre,
    const Eigen::VectorXd& x_guess_right_in_front_pre,
    const Eigen::VectorXd& x_guess_left_in_front_post,
    const Eigen::VectorXd& x_guess_right_in_front_post,
    const std::vector<Eigen::VectorXd>& des_xy_pos, int fisrt_mode_phase_index,
    int starting_mode_index) {
  // PrintStatus("Adding initial guess ...");

  MatrixXd y_guess(r_guess.rows() + dr_guess.rows(), r_guess.cols());
  y_guess << r_guess, dr_guess;

  bool left_stance = start_with_left_stance_;
  for (int i = starting_mode_index; i < num_modes_; i++) {
    // Time steps
    for (int j = 0; j < mode_lengths_[i] - 1; j++) {
      SetInitialGuess(timestep(mode_start_[i] + j), h_guess.segment(1, 1));
    }
    // Rom states and inputs
    for (int j = 0; j < mode_lengths_[i]; j++) {
      // The intial state might start in the middle of the stride
      if (i == 0) {
        SetInitialGuess(state_vars_by_mode(i, j),
                        y_guess.col(fisrt_mode_phase_index + j));
        int time_index = mode_start_[i] + j;
        SetInitialGuess(
            u_vars().segment(time_index * rom_.n_tau(), rom_.n_tau()),
            tau_guess.col(fisrt_mode_phase_index + j));
      } else {
        SetInitialGuess(state_vars_by_mode(i, j), y_guess.col(j));
        int time_index = mode_start_[i] + j;
        SetInitialGuess(
            u_vars().segment(time_index * rom_.n_tau(), rom_.n_tau()),
            tau_guess.col(j));
      }
    }

    auto x_preimpact = xf_vars_by_mode(i);
    auto x_postimpact = x0_vars_by_mode(i + 1);
    // FOM floating base position
    SetInitialGuess(x_preimpact.segment(4, 2), des_xy_pos.at(i + 1));
    Eigen::Vector4d quat_identity(1, 0, 0, 0);
    SetInitialGuess(x_preimpact.head(4), quat_identity);
    // FOM states
    SetInitialGuess(x_preimpact.tail(n_x_ - 6),
                    left_stance ? x_guess_right_in_front_pre.tail(n_x_ - 6)
                                : x_guess_left_in_front_pre.tail(n_x_ - 6));
    SetInitialGuess(x_postimpact.tail(n_x_ - 6),
                    left_stance ? x_guess_right_in_front_post.tail(n_x_ - 6)
                                : x_guess_left_in_front_post.tail(n_x_ - 6));

    left_stance = !left_stance;
  }
}

void RomTrajOptCassie::AddRomRegularizationCost(
    const Eigen::VectorXd& h_guess, const Eigen::MatrixXd& r_guess,
    const Eigen::MatrixXd& dr_guess, const Eigen::MatrixXd& tau_guess,
    int fisrt_mode_phase_index, double w_reg) {
  PrintStatus("Adding cost -- regularization for ROM state ...");

  MatrixXd y_guess(r_guess.rows() + dr_guess.rows(), r_guess.cols());
  y_guess << r_guess, dr_guess;

  MatrixXd I_h = w_reg * MatrixXd::Identity(1, 1);
  MatrixXd I_z = w_reg * MatrixXd::Identity(n_z_, n_z_);
  MatrixXd I_tau = w_reg * MatrixXd::Identity(rom_.n_tau(), rom_.n_tau());

  /*for (int i = 0; i < num_modes_; i++) {
    // Time steps
    for (int j = 0; j < mode_lengths_[i] - 1; j++) {
      rom_regularization_cost_bindings_.push_back(AddQuadraticErrorCost(
          I_h, h_guess.segment(1, 1), timestep(mode_start_[i] + j)));
    }
    // Rom states and inputs
    for (int j = 0; j < mode_lengths_[i]; j++) {
      // The intial state might start in the middle of the stride
      if (i == 0) {
        rom_regularization_cost_bindings_.push_back(
            AddQuadraticErrorCost(I_z, y_guess.col(fisrt_mode_phase_index + j),
                                  state_vars_by_mode(i, j)));
        int time_index = mode_start_[i] + j;
        rom_regularization_cost_bindings_.push_back(AddQuadraticErrorCost(
            I_tau, tau_guess.col(fisrt_mode_phase_index + j),
            u_vars().segment(time_index * rom_.n_tau(), rom_.n_tau())));
      } else {
        rom_regularization_cost_bindings_.push_back(AddQuadraticErrorCost(
            I_z, y_guess.col(j), state_vars_by_mode(i, j)));
        int time_index = mode_start_[i] + j;
        rom_regularization_cost_bindings_.push_back(AddQuadraticErrorCost(
            I_tau, tau_guess.col(j),
            u_vars().segment(time_index * rom_.n_tau(), rom_.n_tau())));
      }
    }
  }*/
}

RomTrajOptFiveLinkRobot::RomTrajOptFiveLinkRobot(
    const std::vector<int>& num_time_samples, const Eigen::MatrixXd& Q,
    const Eigen::MatrixXd& R, const ReducedOrderModel& rom,
    const drake::multibody::MultibodyPlant<double>& plant,
    const StateMirror& state_mirror, const vector<BodyPoint>& left_contacts,
    const vector<BodyPoint>& right_contacts,
    const vector<std::tuple<string, double, double>>& fom_joint_name_lb_ub,
    Eigen::VectorXd x_init, bool start_with_left_stance,
    bool zero_touchdown_impact)
    : RomTrajOpt(num_time_samples, Q, R, rom, plant, state_mirror,
                 left_contacts, right_contacts, left_contacts.at(0),
                 right_contacts.at(0), fom_joint_name_lb_ub, x_init, {},
                 start_with_left_stance, zero_touchdown_impact, {}) {
  DRAKE_UNREACHABLE();  // I added a few things to RomTrajOpt which are not
                        // generalized to the five-link robot.
}

void RomTrajOptFiveLinkRobot::AddRegularizationCost(
    const Eigen::VectorXd& final_position,
    const Eigen::VectorXd& x_guess_left_in_front,
    const Eigen::VectorXd& x_guess_right_in_front, bool straight_leg_cost) {
  cout << "Adding cost -- regularization terms...\n";

  int n_q = plant_.num_positions();

  bool left_stance = start_with_left_stance_;
  for (int i = 0; i < num_modes_; i++) {
    // Adding cost on FOM state increases convergence rate
    // If we only add position (not velocity) in the cost, then higher cost
    // results in spacing out each step more evenly
    MatrixXd Id_7 = 100 * MatrixXd::Identity(n_q - 1, n_q - 1);
    // Id_7(1,1) = 10;
    MatrixXd Id_1 = 100 * MatrixXd::Identity(1, 1);

    double torso_lean_forward_angle = 0.1;
    VectorXd modified_x_guess_left_in_front = x_guess_left_in_front;
    // modified_x_guess_left_in_front(2) = torso_lean_forward_angle;
    VectorXd modified_x_guess_right_in_front = x_guess_right_in_front;
    // modified_x_guess_right_in_front(2) = torso_lean_forward_angle;
    if (straight_leg_cost) {
      Id_7(5, 5) = 10;
      Id_7(6, 6) = 10;
      modified_x_guess_left_in_front(5) = 0;
      modified_x_guess_left_in_front(6) = 0;
      modified_x_guess_right_in_front(5) = 0;
      modified_x_guess_right_in_front(6) = 0;
    }

    if (left_stance) {
      AddQuadraticErrorCost(
          Id_7, modified_x_guess_left_in_front.head(n_q).tail(n_q - 1),
          x0_vars_by_mode(i).head(n_q).tail(n_q - 1));
      AddQuadraticErrorCost(
          Id_7, modified_x_guess_right_in_front.head(n_q).tail(n_q - 1),
          xf_vars_by_mode(i).head(n_q).tail(n_q - 1));
    } else {
      AddQuadraticErrorCost(
          Id_7, modified_x_guess_right_in_front.head(n_q).tail(n_q - 1),
          x0_vars_by_mode(i).head(n_q).tail(n_q - 1));
      AddQuadraticErrorCost(
          Id_7, modified_x_guess_left_in_front.head(n_q).tail(n_q - 1),
          xf_vars_by_mode(i).head(n_q).tail(n_q - 1));
    }
    AddQuadraticErrorCost(Id_1,
                          VectorXd::Ones(1) * final_position * i / num_modes_,
                          x0_vars_by_mode(i).head(1));
    AddQuadraticErrorCost(
        Id_1, VectorXd::Ones(1) * final_position * (i + 1) / num_modes_,
        xf_vars_by_mode(i).head(1));

    left_stance = !left_stance;
  }
}

void RomTrajOptFiveLinkRobot::SetAllInitialGuess(
    const Eigen::VectorXd& h_guess, const Eigen::MatrixXd& r_guess,
    const Eigen::MatrixXd& dr_guess, const Eigen::MatrixXd& tau_guess,
    const Eigen::VectorXd& x_guess_left_in_front,
    const Eigen::VectorXd& x_guess_right_in_front,
    const Eigen::VectorXd& final_position) {
  cout << "Adding initial guess ...\n";

  MatrixXd y_guess(r_guess.rows() + dr_guess.rows(), r_guess.cols());
  y_guess << r_guess, dr_guess;

  bool left_stance = start_with_left_stance_;
  for (int i = 0; i < num_modes_; i++) {
    // Initial guess
    for (int j = 0; j < mode_lengths_[i] - 1; j++) {
      SetInitialGuess(timestep(mode_start_[i] + j), h_guess.segment(1, 1));
    }
    for (int j = 0; j < mode_lengths_[i]; j++) {
      SetInitialGuess(state_vars_by_mode(i, j),
                      y_guess.block(0, j, 2 * rom_.n_y(), 1));
      int time_index = mode_start_[i] + j;
      SetInitialGuess(u_vars().segment(time_index * rom_.n_tau(), rom_.n_tau()),
                      tau_guess.col(j));
    }
    if (left_stance) {
      SetInitialGuess(x0_vars_by_mode(i).tail(n_x_ - 1),
                      x_guess_left_in_front.tail(n_x_ - 1));
      SetInitialGuess(xf_vars_by_mode(i).tail(n_x_ - 1),
                      x_guess_right_in_front.tail(
                          n_x_ - 1));  // TODO: this should be preimpact
    } else {
      SetInitialGuess(x0_vars_by_mode(i).tail(n_x_ - 1),
                      x_guess_right_in_front.tail(
                          n_x_ - 1));  // TODO: this should be preimpact
      SetInitialGuess(xf_vars_by_mode(i).tail(n_x_ - 1),
                      x_guess_left_in_front.tail(n_x_ - 1));
    }
    SetInitialGuess(x0_vars_by_mode(i)(0), final_position(0) * i / num_modes_);
    SetInitialGuess(xf_vars_by_mode(i)(0),
                    final_position(0) * (i + 1) / num_modes_);

    left_stance = !left_stance;
  }
}

}  // namespace goldilocks_models
}  // namespace dairlib
