#include "alip_minlp_footstep_controller.h"
#include "common/eigen_utils.h"
#include "lcm/lcm_trajectory.h"
#include "multibody/multibody_utils.h"
#include "systems/framework/output_vector.h"

#include <iostream>

namespace dairlib::systems::controllers {

using multibody::ReExpressWorldVector3InBodyYawFrame;
using multibody::GetBodyYawRotation_R_WB;
using multibody::SetPositionsAndVelocitiesIfNew;
using geometry::ConvexFootholdSet;
using geometry::ConvexFoothold;
using alip_utils::Stance;

using Eigen::Vector2d;
using Eigen::Vector3d;
using Eigen::Vector4d;
using Eigen::VectorXd;
using Eigen::MatrixXd;
using Eigen::Matrix3d;

using drake::AbstractValue;
using drake::multibody::MultibodyPlant;
using drake::systems::BasicVector;
using drake::systems::Context;
using drake::systems::State;

AlipMINLPFootstepController::AlipMINLPFootstepController(
    const MultibodyPlant<double>& plant, Context<double>* plant_context,
    std::vector<int> left_right_stance_fsm_states,
    std::vector<int> post_left_right_fsm_states,
    std::vector<double> left_right_stance_durations,
    double double_stance_duration,
    std::vector<PointOnFramed> left_right_foot,
    const AlipMINLPGains& gains,
    const drake::solvers::SolverOptions& trajopt_solver_options) :
    plant_(plant),
    context_(plant_context),
    trajopt_(
        AlipMIQP(
          plant.CalcTotalMass(*plant_context),
          gains.hdes,
          gains.knots_per_mode,
          gains.reset_discretization_method,
          gains.nmodes)
        ),
    left_right_stance_fsm_states_(left_right_stance_fsm_states),
    post_left_right_fsm_states_(post_left_right_fsm_states),
    double_stance_duration_(double_stance_duration),
    single_stance_duration_(left_right_stance_durations.front()),
    gains_(gains) {

  // just alternating single stance phases for now.
  DRAKE_DEMAND(left_right_stance_fsm_states_.size() == 2);
  DRAKE_DEMAND(left_right_stance_durations.size() == 2);
  DRAKE_DEMAND(left_right_foot.size() == 2);
  DRAKE_DEMAND(gains_.t_commit > gains_.t_min);

  nq_ = plant_.num_positions();
  nv_ = plant_.num_velocities();
  nu_ = plant_.num_actuators();

  for (int i = 0; i < left_right_stance_fsm_states_.size(); i++){
    stance_foot_map_.insert(
        {left_right_stance_fsm_states_.at(i), left_right_foot.at(i)});
  }

  // Must declare the discrete states before assigning their output ports so
  // that the indexes can be used to call DeclareStateOutputPort
  fsm_state_idx_ = DeclareDiscreteState(1);
  next_impact_time_state_idx_ = DeclareDiscreteState(1);
  prev_impact_time_state_idx_ = DeclareDiscreteState(1);
  initial_conditions_state_idx_ = DeclareDiscreteState(4+3);

  trajopt_.SetDoubleSupportTime(double_stance_duration);
  auto xd = trajopt_.MakeXdesTrajForVdes(
      Vector2d::Zero(), gains_.stance_width, single_stance_duration_,
      gains_.knots_per_mode, alip_utils::Stance::kLeft);
  trajopt_.AddTrackingCost(xd, gains_.Q, gains_.Qf);
  trajopt_.AddInputCost(gains_.R(0,0));
  trajopt_.UpdateNominalStanceTime(single_stance_duration_,
                                  single_stance_duration_);
  trajopt_.SetMinimumStanceTime(gains_.t_min);
  trajopt_.SetMaximumStanceTime(gains_.t_max);
  trajopt_.SetInputLimit(gains_.u_max);
  trajopt_.Build(trajopt_solver_options);
  trajopt_.UpdateFootholds({ConvexFoothold::MakeFlatGround()});
  trajopt_.CalcOptimalFootstepPlan(
      -0.5 * gains_.stance_width * Vector4d::UnitY(),
      0.5 * gains_.stance_width * Vector3d::UnitY());

  if (gains_.filter_alip_state) {
    auto filter = S2SKalmanFilter(gains_.filter_data);
    std::pair<S2SKalmanFilter, S2SKalmanFilterData> model_filter =
        {filter, gains_.filter_data};
    alip_filter_idx_ = DeclareAbstractState(drake::Value<
        std::pair<S2SKalmanFilter, S2SKalmanFilterData>>(model_filter));
  }

  // State Update
  this->DeclarePerStepUnrestrictedUpdateEvent(
      &AlipMINLPFootstepController::UnrestrictedUpdate);

  // Input ports
  state_input_port_ = DeclareVectorInputPort(
      "x, u, t", OutputVector<double>(nq_, nv_, nu_))
      .get_index();
  vdes_input_port_ = DeclareVectorInputPort("vdes_x_y", 2).get_index();
  foothold_input_port_ = DeclareAbstractInputPort(
      "footholds", drake::Value<ConvexFootholdSet>())
      .get_index();

  // output ports
  next_impact_time_output_port_ = DeclareStateOutputPort(
      "t_next", next_impact_time_state_idx_)
      .get_index();
  prev_impact_time_output_port_ = DeclareVectorOutputPort(
      "t_prev", 1, &AlipMINLPFootstepController::CopyPrevImpactTimeOutput)
      .get_index();
  fsm_output_port_ = DeclareVectorOutputPort(
      "fsm", 1, &AlipMINLPFootstepController::CopyFsmOutput)
      .get_index();
  footstep_target_output_port_ = DeclareVectorOutputPort(
      "p_SW", 3, &AlipMINLPFootstepController::CopyNextFootstepOutput)
      .get_index();
  mpc_debug_output_port_ = DeclareAbstractOutputPort(
      "lcmt_mpc_debug", &AlipMINLPFootstepController::CopyMpcDebugToLcm)
      .get_index();
  ankle_torque_output_port_ = DeclareAbstractOutputPort(
      "lcmt_saved_traj",
      &AlipMINLPFootstepController::CopyAnkleTorque)
      .get_index();
}

drake::systems::EventStatus AlipMINLPFootstepController::UnrestrictedUpdate(
    const Context<double> &context, State<double> *state) const {

  // evaluate input ports
  const auto robot_output = dynamic_cast<const OutputVector<double>*>(
      this->EvalVectorInput(context, state_input_port_));
  const Vector2d& vdes =
      this->EvalVectorInput(context, vdes_input_port_)->get_value();
  double t_next_impact =
      state->get_discrete_state(next_impact_time_state_idx_).get_value()(0);
  double t_prev_impact =
      state->get_discrete_state(prev_impact_time_state_idx_).get_value()(0);
  auto foothold_set = this->EvalAbstractInput(context, foothold_input_port_)->
      get_value<ConvexFootholdSet>();

  // get state and time from robot_output, set plant context
  const VectorXd robot_state = robot_output->GetState();
  double t = robot_output->get_timestamp();
  SetPositionsAndVelocitiesIfNew<double>(plant_, robot_state, context_);

  // re-express footholds in robot yaw frame from world frame
  foothold_set.ReExpressInNewFrame(
      GetBodyYawRotation_R_WB<double>(plant_, *context_, "pelvis"));

  // initialize local variables
  int fsm_idx =
      static_cast<int>(state->get_discrete_state(fsm_state_idx_).get_value()(0));
  bool warmstart = true;
  bool committed = false;
  bool fsm_switch = false;
  Vector3d CoM_w = Vector3d::Zero();
  Vector3d p_w = Vector3d::Zero();
  Vector3d p_next_in_ds = Vector3d::Zero();
  Vector3d L = Vector3d::Zero();

  // On the first iteration, we don't want to switch immediately or warmstart
  if (t_next_impact == 0.0) {
    t_next_impact = t + single_stance_duration_ + double_stance_duration_;
    t_prev_impact = t;
    warmstart = false;
  }

  // Check if it's time to switch the fsm or commit to the current footstep
  if (t >= t_next_impact) {
    warmstart = false;
    fsm_switch = true;
    fsm_idx ++;
    fsm_idx = fsm_idx >= left_right_stance_fsm_states_.size() ? 0 : fsm_idx;
    t_prev_impact = t;
    t_next_impact = t + double_stance_duration_ + single_stance_duration_;
  } else if ((t_next_impact - t) < gains_.t_commit) {
    committed = true;
  }

  const int fsm_state = curr_fsm(fsm_idx);
  Stance stance = left_right_stance_fsm_states_.at(fsm_idx) == 0? Stance::kLeft : Stance::kRight;

  double ds_fraction = std::clamp(
      (t - t_prev_impact) / double_stance_duration_, 0.0, 1.0);
  std::vector<double> CoP_fractions = {ds_fraction, 1.0 - ds_fraction};

  // get the alip state
  alip_utils::CalcAlipState(
      plant_, context_, robot_state,
      {stance_foot_map_.at(fsm_state), stance_foot_map_.at(next_fsm(fsm_idx))},
      CoP_fractions, &CoM_w, &L, &p_w);

  plant_.CalcPointsPositions(
      *context_,
      stance_foot_map_.at(fsm_state).second,
      stance_foot_map_.at(fsm_state).first, plant_.world_frame(),
      &p_next_in_ds);

  p_next_in_ds = ReExpressWorldVector3InBodyYawFrame<double>(
      plant_, *context_, "pelvis", p_next_in_ds);
  Vector3d CoM_b = ReExpressWorldVector3InBodyYawFrame<double>(
      plant_, *context_, "pelvis", CoM_w);
  Vector3d p_b = ReExpressWorldVector3InBodyYawFrame<double>(
      plant_, *context_, "pelvis", p_w);
  Vector3d L_b = ReExpressWorldVector3InBodyYawFrame<double>(
      plant_, *context_, "pelvis", L);

  Vector4d x = Vector4d::Zero();
  x.head<2>() = CoM_b.head<2>() - p_b.head<2>();
  x.tail<2>() = L_b.head<2>();

  if (gains_.filter_alip_state) {
    // TODO: Incorporate double stance reset map into filtering and re-enable
    auto& [filter, filter_data] = state->get_mutable_abstract_state<
        std::pair<S2SKalmanFilter,S2SKalmanFilterData>>(alip_filter_idx_);
    filter_data.A =
        alip_utils::CalcA(CoM_b(2) - p_b(2), plant_.CalcTotalMass(*context_));

    // split this assignment into 2 lines to avoid Eigen compiler error :(
    Vector2d u = (p_b - p_next_in_ds).head<2>();
    u = fsm_switch ? u : Vector2d::Zero();
    filter.Update(filter_data, u, x, t);
    x = filter.x();
  }

  double time_left_in_this_mode = t_next_impact - t;
  if (t - t_prev_impact < double_stance_duration_) {
    double tds = double_stance_duration_ - (t - t_prev_impact);
    x = alip_utils::CalcReset(
        trajopt_.H(), trajopt_.m(), tds, x, p_b, p_next_in_ds,
        gains_.reset_discretization_method);
    p_b = p_next_in_ds;
    time_left_in_this_mode = single_stance_duration_;
  }
  VectorXd init_alip_state_and_stance_pos = VectorXd::Zero(7);
  init_alip_state_and_stance_pos.head<4>() = x;
  init_alip_state_and_stance_pos.tail<3>() = p_b;

  // Update desired trajectory
  auto xd  = trajopt_.MakeXdesTrajForVdes(
      vdes, gains_.stance_width, single_stance_duration_,
      gains_.knots_per_mode, stance);

  // Update the trajopt_ problem data and solve
  //  trajopt_.set_H(h);
  trajopt_.UpdateTrackingCost(xd);
  if (!foothold_set.empty()) {
    trajopt_.UpdateFootholds(foothold_set.footholds());
  } else {
    std::cerr << "WARNING: No new footholds specified!\n";
  }

  trajopt_.UpdateNominalStanceTime(time_left_in_this_mode, single_stance_duration_);

  if (committed) {
    trajopt_.ActivateInitialTimeEqualityConstraint(t_next_impact - t);
  } else {
    trajopt_.UpdateMaximumCurrentStanceTime(gains_.t_max - (t - t_prev_impact));
  }
  if (fsm_switch) {
    trajopt_.UpdateNoCrossoverConstraint();
    trajopt_.UpdateModeTimingsOnTouchdown();
  }
  trajopt_.UpdateModeTiming((!(committed || fsm_switch)) && warmstart);

  ConvexFoothold workspace;
  Vector3d com_xy(CoM_b(0), CoM_b(1), p_b(2));

  workspace.AddFace( Vector3d::UnitY(),  com_xy + 10 * Vector3d::UnitY());
  workspace.AddFace(-Vector3d::UnitY(), com_xy -10 * Vector3d::UnitY());
  workspace.AddFace(Vector3d::UnitX(), com_xy + 10 * Vector3d::UnitX());
  workspace.AddFace(-Vector3d::UnitX(), com_xy - 10 * Vector3d::UnitX());

  trajopt_.UpdateNextFootstepReachabilityConstraint(workspace);
  trajopt_.CalcOptimalFootstepPlan(x, p_b, warmstart);

  // Update discrete states
  double t0 = trajopt_.GetTimingSolution()(0);
  state->get_mutable_discrete_state(fsm_state_idx_).set_value(
      fsm_idx*VectorXd::Ones(1));
  state->get_mutable_discrete_state(next_impact_time_state_idx_).set_value(
      (t + t0) * VectorXd::Ones(1));
  state->get_mutable_discrete_state(prev_impact_time_state_idx_).set_value(
      t_prev_impact * VectorXd::Ones(1));
  state->get_mutable_discrete_state(initial_conditions_state_idx_).set_value(
      init_alip_state_and_stance_pos);

  return drake::systems::EventStatus::Succeeded();
}

void AlipMINLPFootstepController::CopyNextFootstepOutput(
    const Context<double> &context, BasicVector<double> *p_B_FC) const {
  const auto& pp = trajopt_.GetFootstepSolution();
  const auto& xx = trajopt_.GetStateSolution();
  Vector3d footstep_in_com_yaw_frame = pp.at(1);
  footstep_in_com_yaw_frame.head(2) =
      (pp.at(1) - pp.at(0)).head(2) - xx.front().tail<4>().head(2);
  p_B_FC->set_value(footstep_in_com_yaw_frame);
}

void AlipMINLPFootstepController::CopyMpcDebugToLcm(
    const Context<double> &context, lcmt_mpc_debug *mpc_debug) const {

  const auto& ic =
      context.get_discrete_state(initial_conditions_state_idx_).get_value();
  const auto robot_output = dynamic_cast<const OutputVector<double>*>(
      this->EvalVectorInput(context, state_input_port_));

  const auto& x = robot_output->GetState();
  const auto R_WB_x = drake::math::RotationMatrixd(
      Eigen::Quaterniond(x(0), x(1), x(2), x(3))).matrix().col(0);
  const auto Ryaw =
      drake::math::RotationMatrixd::MakeZRotation(atan2(R_WB_x(1), R_WB_x(0)));
  auto foothold_set =
      EvalAbstractInput(context, foothold_input_port_)->get_value<ConvexFootholdSet>();
  foothold_set.CopyToLcm(&mpc_debug->footholds);

  int utime = static_cast<int>(robot_output->get_timestamp() * 1e6);
  double fsmd = context.get_discrete_state(fsm_state_idx_).get_value()(0);
  int fsm = curr_fsm(static_cast<int>(fsmd));

  CopyMpcSolutionToLcm(trajopt_.GetFootstepSolution(),
                       trajopt_.GetStateSolution(),
                       trajopt_.GetInputSolution(),
                       trajopt_.GetTimingSolution(),
                       &mpc_debug->solution);

  CopyMpcSolutionToLcm(trajopt_.GetFootstepDesired(),
                       trajopt_.GetStateDesired(),
                       trajopt_.GetInputDesired(),
                       trajopt_.GetTimingDesired(),
                       &mpc_debug->desired);

  CopyMpcSolutionToLcm(trajopt_.GetFootstepGuess(),
                       trajopt_.GetStateGuess(),
                       trajopt_.GetInputGuess(),
                       trajopt_.GetTimingGuess(),
                       &mpc_debug->guess);

  mpc_debug->utime = utime;
  mpc_debug->fsm_state = fsm;
  mpc_debug->solve_time_us = static_cast<int64_t>(1e6 * trajopt_.solve_time());

  Vector4d::Map(mpc_debug->x0) = ic.head<4>();
  Vector3d::Map(mpc_debug->p0) = ic.tail<3>();

}

void AlipMINLPFootstepController::CopyMpcSolutionToLcm(
    const std::vector<Vector3d> &pp,
    const std::vector<VectorXd> &xx,
    const std::vector<VectorXd> &uu,
    const Eigen::VectorXd &tt,
    lcmt_mpc_solution *solution) const {

  DRAKE_DEMAND(pp.size() == gains_.nmodes);
  DRAKE_DEMAND(xx.size() == gains_.nmodes && xx.front().size() == 4 * gains_.knots_per_mode);
  DRAKE_DEMAND(uu.size() == gains_.nmodes && uu.front().size() == gains_.knots_per_mode - 1);
  DRAKE_DEMAND(tt.rows() == gains_.nmodes);

  solution->nx = 4;
  solution->nu = 1;
  solution->np = 3;
  solution->nm = gains_.nmodes;
  solution->nk = gains_.knots_per_mode;
  solution->nk_minus_one = solution->nk - 1;

  solution->pp.clear();
  solution->pp.reserve(gains_.nmodes);
  solution->xx.clear();
  solution->xx.reserve(gains_.nmodes);
  solution->uu.clear();
  solution->uu.reserve(gains_.nmodes);

  for (int n = 0; n < gains_.nmodes; n++) {
    solution->pp.push_back(CopyVectorXdToStdVector(pp.at(n)));
    solution->xx.push_back(vector<vector<double>>{});
    solution->uu.push_back(vector<vector<double>>{});
    for (int k = 0; k < gains_.knots_per_mode; k++) {
      solution->xx.back().push_back(CopyVectorXdToStdVector(
          AlipMultiQP::GetStateAtKnot(xx.at(n), k)
      ));
    }
    for(int k = 0; k < gains_.knots_per_mode - 1; k++) {
      solution->uu.back().push_back(CopyVectorXdToStdVector(
          AlipMultiQP::GetInputAtKnot(uu.at(n), k)
      ));
    }
  }
  solution->tt = CopyVectorXdToStdVector(tt);
}

void AlipMINLPFootstepController::CopyFsmOutput(
    const Context<double> &context, BasicVector<double> *fsm) const {
  double t_prev =
      context.get_discrete_state(prev_impact_time_state_idx_).get_value()(0);
  const auto robot_output = dynamic_cast<const OutputVector<double>*>(
      this->EvalVectorInput(context, state_input_port_));
  int fsm_idx = static_cast<int>(
      context.get_discrete_state(fsm_state_idx_).get_value()(0));

  if(robot_output->get_timestamp() - t_prev < double_stance_duration_) {
    fsm->set_value(VectorXd::Ones(1) * post_left_right_fsm_states_.at(fsm_idx));
  } else {
    fsm->set_value(VectorXd::Ones(1) * left_right_stance_fsm_states_.at(fsm_idx));
  }
}

void AlipMINLPFootstepController::CopyPrevImpactTimeOutput(
    const Context<double> &context, BasicVector<double> *t) const {
  double t_prev =
      context.get_discrete_state(prev_impact_time_state_idx_).get_value()(0);
  const auto robot_output = dynamic_cast<const OutputVector<double>*>(
      this->EvalVectorInput(context, state_input_port_));
  if (robot_output->get_timestamp() - t_prev < double_stance_duration_) {
    t->set_value(VectorXd::Ones(1) * t_prev);
  } else {
    t->set_value(VectorXd::Ones(1) * (t_prev + double_stance_duration_));
  }
}


void AlipMINLPFootstepController::CopyAnkleTorque(
    const Context<double> &context, lcmt_saved_traj *traj) const {
  double t  = dynamic_cast<const OutputVector<double>*>(
      this->EvalVectorInput(context, state_input_port_))->get_timestamp();

  const int nk = gains_.knots_per_mode;

  MatrixXd knots = trajopt_.GetInputSolution().front().transpose();
  VectorXd breaks = VectorXd::Zero(nk - 1);
  double T = trajopt_.GetTimingSolution()(0);
  for (int n = 0; n < nk - 1; n++) {
    breaks(n) = t + static_cast<double>(n) / T;
  }
  LcmTrajectory::Trajectory input_traj;
  input_traj.traj_name = "input_traj";
  input_traj.datatypes = vector<std::string>(1, "double");
  input_traj.datapoints = knots;
  input_traj.time_vector = breaks;
  LcmTrajectory lcm_traj(
      {input_traj}, {"input_traj"}, "input_traj", "input_traj");
  *traj = lcm_traj.GenerateLcmObject();
}


}