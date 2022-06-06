#include "c3_controller_franka.h"

#include <utility>
#include <chrono>


#include "external/drake/tools/install/libdrake/_virtual_includes/drake_shared_library/drake/common/sorted_pair.h"
#include "external/drake/tools/install/libdrake/_virtual_includes/drake_shared_library/drake/multibody/plant/multibody_plant.h"
#include "multibody/multibody_utils.h"
#include "solvers/c3.h"
#include "solvers/c3_miqp.h"
#include "solvers/lcs_factory.h"

#include "drake/solvers/moby_lcp_solver.h"
#include "multibody/geom_geom_collider.h"
#include "multibody/kinematic/kinematic_evaluator_set.h"
#include "solvers/lcs_factory.h"
#include "drake/math/autodiff_gradient.h"


//#include
//"external/drake/common/_virtual_includes/autodiff/drake/common/eigen_autodiff_types.h"

using std::vector;

using drake::AutoDiffVecXd;
using drake::AutoDiffXd;
using drake::MatrixX;
using drake::SortedPair;
using drake::geometry::GeometryId;
using drake::math::ExtractGradient;
using drake::math::ExtractValue;
using drake::multibody::MultibodyPlant;
using drake::systems::Context;
using Eigen::MatrixXd;
using Eigen::RowVectorXd;
using Eigen::VectorXd;
using std::vector;

using Eigen::Matrix3d;
using Eigen::Vector3d;
using Eigen::Quaterniond;
using std::vector;
using drake::multibody::JacobianWrtVariable;




namespace dairlib {
namespace systems {
namespace controllers {

C3Controller_franka::C3Controller_franka(
    const drake::multibody::MultibodyPlant<double>& plant,
    drake::multibody::MultibodyPlant<double>& plant_f,
    const drake::multibody::MultibodyPlant<double>& plant_franka,
    drake::systems::Context<double>& context,
    drake::systems::Context<double>& context_f,
    drake::systems::Context<double>& context_franka,
    const drake::multibody::MultibodyPlant<drake::AutoDiffXd>& plant_ad,
    drake::multibody::MultibodyPlant<drake::AutoDiffXd>& plant_ad_f,
    drake::systems::Context<drake::AutoDiffXd>& context_ad,
    drake::systems::Context<drake::AutoDiffXd>& context_ad_f,
    const drake::geometry::SceneGraph<double>& scene_graph,
    const drake::systems::Diagram<double>& diagram,
    std::vector<drake::geometry::GeometryId> contact_geoms,
    int num_friction_directions, double mu, const vector<MatrixXd>& Q,
    const vector<MatrixXd>& R, const vector<MatrixXd>& G,
    const vector<MatrixXd>& U, const vector<VectorXd>& xdesired, const drake::trajectories::PiecewisePolynomial<double>& pp)
    : plant_(plant),
      plant_f_(plant_f),
      plant_franka_(plant_franka),
      context_(context),
      context_f_(context_f),
      context_franka_(context_franka),
      plant_ad_(plant_ad),
      plant_ad_f_(plant_ad_f),
      context_ad_(context_ad),
      context_ad_f_(context_ad_f),
      scene_graph_(scene_graph),
      diagram_(diagram),
      contact_geoms_(contact_geoms),
      num_friction_directions_(num_friction_directions),
      mu_(mu),
      Q_(Q),
      R_(R),
      G_(G),
      U_(U),
      xdesired_(xdesired),
      pp_(pp){
  int num_positions = plant_.num_positions();
  int num_velocities = plant_.num_velocities();
  int num_inputs = plant_.num_actuators();
  int num_states = num_positions + num_velocities;


  state_input_port_ =
      this->DeclareVectorInputPort(
              "x, u, t",
              OutputVector<double>((int)14, (int) 13, (int) 7))
          .get_index();


  state_output_port_ = this->DeclareVectorOutputPort(
                                 "x_lambda, t", TimestampedVector<double>(25),
                                 &C3Controller_franka::CalcControl)
                             .get_index();


  // DRAKE_DEMAND(contact_geoms_.size() >= 4);
  //std::cout << "constructed c3controller" <<std::endl;

  // std::cout << contact_geoms_[0] << std::endl;
}

void C3Controller_franka::CalcControl(const Context<double>& context,
                               TimestampedVector<double>* state_contact_desired) const {
  

  //std::cout << "here" << std::endl;

  /// get values
  auto robot_output = (OutputVector<double>*)this->EvalVectorInput(context, state_input_port_);
  double timestamp = robot_output->get_timestamp();

  // DEBUG CODE FOR ADAM
  // TODO: @Alp comment this section out to use actual admm
  VectorXd debug_state = VectorXd::Zero(25);
  // track 0.6, 0, 0.2 with no velocities or contact forces
  debug_state.head(3) << 0.6, 0, 0.2;
  state_contact_desired->SetDataVector(debug_state);
  state_contact_desired->set_timestamp(timestamp);
  return;

/*
  VectorXd state_franka(27);
  state_franka << robot_output->GetPositions(), robot_output->GetVelocities();

  //update franka context
  plant_franka_.SetPositions(&context_franka_, robot_output->GetPositions());
  plant_franka_.SetVelocities(&context_franka_, robot_output->GetVelocities());

  // forward kinematics
  const drake::math::RigidTransform<double> H_mat =
      plant_franka_.EvalBodyPoseInWorld(context_franka_, plant_franka_.GetBodyByName("panda_link8"));
  VectorXd end_effector = H_mat.translation();

  Vector3d EE_offset_;
  EE_offset_ << 0, 0, 0;
  auto EE_frame_ = &plant_franka_.GetBodyByName("panda_link8").body_frame();
  auto world_frame_ = &plant_franka_.world_frame();

  MatrixXd J_fb (6, plant_franka_.num_velocities());
  plant_franka_.CalcJacobianSpatialVelocity(
      context_franka_, JacobianWrtVariable::kV,
      *EE_frame_, EE_offset_,
      *world_frame_, *world_frame_, &J_fb);
  MatrixXd J_franka = J_fb.block(0, 0, 6, 7);

  VectorXd end_effector_dot = ( J_franka * (robot_output->GetVelocities()).head(7) ).tail(3);



  VectorXd ball = robot_output->GetPositions().tail(7);
  VectorXd ball_dot = robot_output->GetVelocities().tail(6);
  VectorXd state(plant_.num_positions() + plant_.num_velocities());
  state << end_effector, ball, end_effector_dot, ball_dot;
  VectorXd q(10);
  q << end_effector, ball;
  VectorXd v(9);
  v << end_effector_dot, ball_dot;
  VectorXd u = VectorXd::Zero(3);

  VectorXd traj_desired_vector = pp_.value(timestamp);


  traj_desired_vector[0] = state[7]; //- 0.05;
  traj_desired_vector[1] = state[8]; //+ 0.01;

  std::vector<VectorXd> traj_desired(Q_.size() , traj_desired_vector);

  /// update autodiff
  VectorXd xu(plant_f_.num_positions() + plant_f_.num_velocities() +
              plant_f_.num_actuators());


  xu << q, v, u;
  auto xu_ad = drake::math::InitializeAutoDiff(xu);

  plant_ad_f_.SetPositionsAndVelocities(
      &context_ad_f_,
      xu_ad.head(plant_f_.num_positions() + plant_f_.num_velocities()));

  multibody::SetInputsIfNew<AutoDiffXd>(
      plant_ad_f_, xu_ad.tail(plant_f_.num_actuators()), &context_ad_f_);


  /// upddate context

  plant_f_.SetPositions(&context_f_, q);
  plant_f_.SetVelocities(&context_f_, v);
  multibody::SetInputsIfNew<double>(plant_f_, u, &context_f_);

  /// figure out a nice way to do this as SortedPairs with pybind is not working
  /// (potentially pass a matrix 2xnum_pairs?)

std::vector<SortedPair<GeometryId>> contact_pairs;

  contact_pairs.push_back(SortedPair(contact_geoms_[0], contact_geoms_[1]));  //was 0, 3
  contact_pairs.push_back(SortedPair(contact_geoms_[1], contact_geoms_[2]));

  solvers::LCS system_ = solvers::LCSFactory::LinearizePlantToLCS(
      plant_f_, context_f_, plant_ad_f_, context_ad_f_, contact_pairs,
      num_friction_directions_, mu_);

  C3Options options;
  int N = (system_.A_).size();
  int n = ((system_.A_)[0].cols());
  int m = ((system_.D_)[0].cols());
  int k = ((system_.B_)[0].cols());


  /// initialize ADMM variables (delta, w)
  std::vector<VectorXd> delta(N, VectorXd::Zero(n + m + k));
  std::vector<VectorXd> w(N, VectorXd::Zero(n + m + k));

  /// initialize ADMM reset variables (delta, w are reseted to these values)
  std::vector<VectorXd> delta_reset(N, VectorXd::Zero(n + m + k));
  std::vector<VectorXd> w_reset(N, VectorXd::Zero(n + m + k));

  if (options.delta_option == 1) {
    /// reset delta and w (option 1)
    delta = delta_reset;
    w = w_reset;
    for (int j = 0; j < N; j++) {
      //delta[j].head(n) = xdesired_[0]; //state
      delta[j].head(n) << state; //state
    }
  } else {
    /// reset delta and w (default option)
    delta = delta_reset;
    w = w_reset;
  }


  int ts = round(timestamp);


  MatrixXd Qnew;
  Qnew = Q_[0];

  if (ts % 3 == 0){
    Qnew(7,7) = 1;
    Qnew(8,8) = 1;
  }

  std::vector<MatrixXd> Qha(Q_.size(), Qnew);

  solvers::C3MIQP opt(system_, Qha, R_, G_, U_, traj_desired, options);
  //solvers::C3MIQP opt(system_, Q_, R_, G_, U_, xdesired_, options);

//  ///trifinger constraints
//  ///input
//  opt.RemoveConstraints();
//  RowVectorXd LinIneq = RowVectorXd::Zero(k);
//  RowVectorXd LinIneq_r = RowVectorXd::Zero(k);
//  double lowerbound = -10;
//  double upperbound = 10;
//  int inputconstraint = 2;
//
//  for (int i = 0; i < k; i++) {
//    LinIneq_r = LinIneq;
//    LinIneq_r(i) = 1;
//    opt.AddLinearConstraint(LinIneq_r, lowerbound, upperbound, inputconstraint);
//  }
//
//
//
//  ///force
//  RowVectorXd LinIneqf = RowVectorXd::Zero(m);
//  RowVectorXd LinIneqf_r = RowVectorXd::Zero(m);
//  double lowerboundf = 0;
//  double upperboundf = 100;
//  int forceconstraint = 3;
//
//  for (int i = 0; i < m; i++) {
//    LinIneqf_r = LinIneqf;
//    LinIneqf_r(i) = 1;
//    opt.AddLinearConstraint(LinIneqf_r, lowerboundf, upperboundf, forceconstraint);
//  }


  ///state (velocity)
  int stateconstraint = 1;
  RowVectorXd LinIneqs = RowVectorXd::Zero(n);
  RowVectorXd LinIneqs_r = RowVectorXd::Zero(n);
  double lowerbounds = -20;
  double upperbounds = 20;

//  for (int i = 16; i < 25; i++) {
//    LinIneqs_r = LinIneqs;
//    LinIneqs_r(i) = 1;
//    opt.AddLinearConstraint(LinIneqs_r, lowerbounds, upperbounds, stateconstraint);
//  }

  ///state (q)
  double lowerboundsq = 0;
  double upperboundsq = 0.03;
//  for (int i = 0; i < 9; i++) {
//    LinIneqs_r = LinIneqs;
//    LinIneqs_r(i) = 1;
//    opt.AddLinearConstraint(LinIneqs_r, lowerboundsq, upperboundsq, stateconstraint);
//  }

//int i = 2;
//LinIneqs_r = LinIneqs;
//LinIneqs_r(i) = 1;
//opt.AddLinearConstraint(LinIneqs_r, lowerboundsq, upperboundsq, stateconstraint);
//i = 5;
//LinIneqs_r = LinIneqs;
//LinIneqs_r(i) = 1;
//opt.AddLinearConstraint(LinIneqs_r, lowerboundsq, upperboundsq, stateconstraint);
//i = 8;
//LinIneqs_r = LinIneqs;
//LinIneqs_r(i) = 1;
//opt.AddLinearConstraint(LinIneqs_r, lowerboundsq, upperboundsq, stateconstraint);


  /// calculate the input given x[i]
  VectorXd input = opt.Solve(state, delta, w);

  ///calculate state and force
  drake::solvers::MobyLCPSolver<double> LCPSolver;
  VectorXd force;

  auto flag = LCPSolver.SolveLcpLemke(system_.F_[0], system_.E_[0] * state + system_.c_[0] + system_.H_[0] * input,
                                      &force);
  VectorXd state_next = system_.A_[0] * state + system_.B_[0] * input + system_.D_[0] * force + system_.d_[0];


//  ///subject to change (compute J_c)
//  VectorXd phi(contact_pairs.size());
//  MatrixXd J_n(contact_pairs.size(), plant_.num_velocities());
//  MatrixXd J_t(2 * contact_pairs.size() * num_friction_directions_,
//               plant_.num_velocities());
//
//  for (int i = 0; i < contact_pairs.size(); i++) {
//    dairlib::multibody::GeomGeomCollider collider(
//        plant_, contact_pairs[i]);
//
//    auto [phi_i, J_i] = collider.EvalPolytope(context, num_friction_directions_);
//
////    std::cout << "phi_i" << std::endl;
////    std::cout << phi_i << std::endl;
////    std::cout << "phi_i" << std::endl;
//
//
//    phi(i) = phi_i;
//
//    J_n.row(i) = J_i.row(0);
//    J_t.block(2 * i * num_friction_directions_, 0, 2 * num_friction_directions_,
//              plant_.num_velocities()) =
//        J_i.block(1, 0, 2 * num_friction_directions_, plant_.num_velocities());
//  }

  ///add force_next
  //VectorXd f_mapped = J_n

////switch between two
////1 (connected to impedence)

VectorXd force_des = force.head(6);

VectorXd st_desired(force_des.size() + state_next.size() );
st_desired << state_next, force_des.head(6);

////2 (connected to franka)
//VectorXd st_desired = VectorXd::Zero(7);


  state_contact_desired->SetDataVector(st_desired);
  state_contact_desired->set_timestamp(timestamp);

*/
}
}  // namespace controllers
}  // namespace systems
}  // namespace dairlib