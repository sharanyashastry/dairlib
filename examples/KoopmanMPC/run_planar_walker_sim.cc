#include <memory>
#include <string>

#include <gflags/gflags.h>

#include "common/find_resource.h"
#include "dairlib/lcmt_robot_input.hpp"
#include "dairlib/lcmt_robot_output.hpp"
#include "lcm/lcm_trajectory.h"
#include "multibody/multibody_utils.h"
#include "systems/primitives/subvector_pass_through.h"
#include "systems/robot_lcm_systems.h"

#include "examples/KoopmanMPC/PlanarWalker/planar_walker_model_utils.h"
#include "drake/lcm/drake_lcm.h"
#include "drake/lcmt_contact_results_for_viz.hpp"
#include "drake/multibody/parsing/parser.h"
#include "drake/multibody/plant/contact_results_to_lcm.h"
#include "drake/multibody/plant/multibody_plant.h"
#include "drake/systems/analysis/simulator.h"
#include "drake/systems/framework/diagram_builder.h"
#include "drake/systems/lcm/lcm_interface_system.h"
#include "drake/systems/lcm/lcm_publisher_system.h"
#include "drake/systems/lcm/lcm_subscriber_system.h"

namespace dairlib {

using multibody::makeNameToPositionsMap;
using systems::SubvectorPassThrough;
using drake::geometry::SceneGraph;
using drake::multibody::Body;
using drake::multibody::ContactResultsToLcmSystem;
using drake::multibody::MultibodyPlant;
using drake::multibody::Parser;
using drake::systems::Context;
using drake::systems::DiagramBuilder;
using drake::systems::Simulator;
using drake::systems::lcm::LcmPublisherSystem;
using drake::systems::lcm::LcmSubscriberSystem;
using drake::trajectories::PiecewisePolynomial;
using drake::trajectories::Trajectory;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using Eigen::Vector3d;

// Simulation parameters.

DEFINE_double(start_time, 0.0, "Time to start the simulator at.");
DEFINE_double(sim_time, std::numeric_limits<double>::infinity(),
              "The length of time to run the simulation");
DEFINE_double(target_realtime_rate, 1.0,
              "Desired rate relative to real time.  See documentation for "
              "Simulator::set_target_realtime_rate() for details.");
DEFINE_double(dt, 0, "The step size for the time-stepping simulator.");
DEFINE_double(publish_rate, 1000, "Publish rate of the robot's state in Hz.");
DEFINE_double(penetration_allowance, 1e-5,"Penetration allowance for the contact model.");
DEFINE_double(stiction, 0.001, "Stiction tolerance for the contact model.");
DEFINE_double(mu, 0.8, "Friction coefficient");
DEFINE_double(slope, 0.0, "ground slope");
DEFINE_double(z, 0.75, "initial height of hip joint");
DEFINE_string(folder_path, "",
              "Folder path for the folder that contains the "
              "saved trajectory");
DEFINE_string(channel_x, "PLANAR_STATE","Channel to publish/receive state from simulation");
DEFINE_string(channel_u, "PLANAR_INPUT","Channel to publish/receive inputs from controller");
DEFINE_string(x0_traj_name, "state_traj1", "lcm trajectory to use for initial state");


double calcPositionOffset(MultibodyPlant<double>& plant, Context<double>* context, const Eigen::VectorXd& x0);

int do_main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  DiagramBuilder<double> builder;
  auto lcm = builder.AddSystem<drake::systems::lcm::LcmInterfaceSystem>();

  SceneGraph<double>& scene_graph = *builder.AddSystem<SceneGraph>();
  scene_graph.set_name("scene_graph");

  MultibodyPlant<double>& plant = *builder.AddSystem<MultibodyPlant>(FLAGS_dt);
  LoadPlanarWalkerFromFile(plant, &scene_graph);
  Vector3d normal = Vector3d::Zero();
  normal(0) = -FLAGS_slope;
  normal(2) = 1.0;
  normal.normalize();
  multibody::addFlatTerrain(&plant, &scene_graph, FLAGS_mu, FLAGS_mu, normal);  // Add ground
  plant.Finalize();

  int nv = plant.num_velocities();

  // Contact model parameters
  plant.set_stiction_tolerance(FLAGS_stiction);
  plant.set_penetration_allowance(FLAGS_penetration_allowance);

  // Create input receiver.
  auto input_sub =
      builder.AddSystem(LcmSubscriberSystem::Make<dairlib::lcmt_robot_input>(
          FLAGS_channel_u, lcm));
  auto input_receiver = builder.AddSystem<systems::RobotInputReceiver>(plant);
  // connect input receiver
  auto passthrough = builder.AddSystem<SubvectorPassThrough>(
      input_receiver->get_output_port(0).size(), 0,
      plant.get_actuation_input_port().size());
  // Create state publisher.
  auto state_pub =
      builder.AddSystem(LcmPublisherSystem::Make<dairlib::lcmt_robot_output>(
          FLAGS_channel_x, lcm, 1.0 / FLAGS_publish_rate));
  ContactResultsToLcmSystem<double>& contact_viz =
      *builder.template AddSystem<ContactResultsToLcmSystem<double>>(plant);
  contact_viz.set_name("contact_visualization");
  auto& contact_results_publisher = *builder.AddSystem(
      LcmPublisherSystem::Make<drake::lcmt_contact_results_for_viz>(
          "CONTACT_RESULTS", lcm, 1.0 / FLAGS_publish_rate));
  contact_results_publisher.set_name("contact_results_publisher");
  auto state_sender = builder.AddSystem<systems::RobotOutputSender>(plant);

  // Contact results to lcm msg.
  builder.Connect(*input_sub, *input_receiver);
  builder.Connect(*input_receiver, *passthrough);
  builder.Connect(passthrough->get_output_port(),
                  plant.get_actuation_input_port());
  builder.Connect(plant.get_contact_results_output_port(),
                  contact_viz.get_input_port(0));
  builder.Connect(contact_viz.get_output_port(0),
                  contact_results_publisher.get_input_port());
  builder.Connect(plant.get_state_output_port(),
                  state_sender->get_input_port_state());
  builder.Connect(state_sender->get_output_port(0),
                  state_pub->get_input_port());
  builder.Connect(
      plant.get_geometry_poses_output_port(),
      scene_graph.get_source_pose_port(plant.get_source_id().value()));
  builder.Connect(scene_graph.get_query_output_port(),
                  plant.get_geometry_query_input_port());

  auto diagram = builder.Build();
  // Create a context for this system:
  std::unique_ptr<Context<double>> diagram_context =
      diagram->CreateDefaultContext();
  diagram_context->EnableCaching();
  diagram->SetDefaultContext(diagram_context.get());
  Context<double>& plant_context =
      diagram->GetMutableSubsystemContext(plant, diagram_context.get());

  /// Create a plant for fixed point solver
  MultibodyPlant<double> solver_plant(0.0);
  LoadPlanarWalkerFromFile(solver_plant);
  solver_plant.Finalize();

  VectorXd q = VectorXd::Zero(plant.num_positions());
  VectorXd u = VectorXd::Zero(plant.num_actuators());
  PlanarWalkerFixedPointSolver(solver_plant, FLAGS_z, 0.15, 0.5, &q, &u);

  // set plant position with fixed point solution
  VectorXd x = VectorXd::Zero(plant.num_positions() + plant.num_velocities());
  x.head(plant.num_positions()) = q;
  plant.SetPositionsAndVelocities(&plant_context, x);

  /*** list all of the positions for debugging
  for (auto pos = map.begin(); pos != map.end();
  ++pos) {
    std::cout << pos->first << std::endl;
  } ***/

  diagram_context->SetTime(FLAGS_start_time);
  Simulator<double> simulator(*diagram, std::move(diagram_context));

  simulator.set_publish_every_time_step(false);
  simulator.set_publish_at_initialization(false);
  simulator.set_target_realtime_rate(FLAGS_target_realtime_rate);
  simulator.Initialize();
  simulator.AdvanceTo(FLAGS_start_time + FLAGS_sim_time);

  return 0;
}

double calcPositionOffset(MultibodyPlant<double>& plant,
    Context<double>* context, const Eigen::VectorXd& x0) {

  plant.SetPositionsAndVelocities(context, x0);
  Eigen::Vector3d foot_pos;
  plant.CalcPointsPositions(*context, plant.GetBodyByName("left_lower_leg").body_frame(),
          Eigen::Vector3d(0,0,-0.5), plant.world_frame(), &foot_pos);
  return -foot_pos(2);
}

}  // namespace dairlib
int main(int argc, char* argv[]) { return dairlib::do_main(argc, argv); }