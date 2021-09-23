#include <gflags/gflags.h>

#include "dairlib/lcmt_robot_output.hpp"

#include "drake/systems/controllers/linear_quadratic_regulator.h"
#include "drake/multibody/plant/multibody_plant.h"
#include "drake/multibody/parsing/parser.h"
#include "drake/systems/lcm/lcm_publisher_system.h"
#include "drake/systems/framework/diagram_builder.h"
#include "drake/systems/primitives/multiplexer.h"


#include "systems/robot_lcm_systems.h"
#include "common/find_resource.h"
#include "systems/framework/lcm_driven_loop.h"
#include "systems/primitives/subvector_pass_through.h"

using drake::multibody::MultibodyPlant;
using drake::multibody::Parser;
using drake::systems::controllers::LinearQuadraticRegulator;
using drake::systems::DiagramBuilder;
using drake::systems::Diagram;
using drake::systems::Simulator;
using drake::systems::Context;
using drake::systems::lcm::LcmSubscriberSystem;
using drake::systems::lcm::LcmPublisherSystem;


using dairlib::systems::LcmDrivenLoop;
using dairlib::FindResourceOrThrow;
using dairlib::systems::RobotOutputReceiver;
using dairlib::systems::RobotCommandSender;
using dairlib::systems::SubvectorPassThrough;

using Eigen::MatrixXd;
using Eigen::VectorXd;

namespace dairlib::examples::cartpole {

DEFINE_string(channel_x, "CARTPOLE_STATE", "Channel to recieve state");
DEFINE_string(channel_u, "CARTPOLE_INPUT", "channel to publish input");

int controller_main(int argc, char* argv[]) {
  DiagramBuilder<double> builder;

  MultibodyPlant<double> plant(0.0);
  Parser(&plant).AddModelFromFile(
      FindResourceOrThrow(
          "examples/cartpole/urdf/cartpole.urdf"));
  plant.Finalize();
  auto plant_context  = plant.CreateDefaultContext();
  plant.get_actuation_input_port().FixValue(plant_context.get(),
      VectorXd::Zero(1));

  MatrixXd Q = 10 * MatrixXd::Identity(4,4);
  MatrixXd R = MatrixXd::Identity(1,1);


  auto lqr = LinearQuadraticRegulator(
      plant, *plant_context, Q, R, MatrixXd::Zero(0,0),
      plant.get_actuation_input_port().get_index());
  lqr->set_name("lqr");
  builder.AddSystem(std::move(lqr));


  // LCM
  drake::lcm::DrakeLcm lcm;
  auto input_publisher = builder.AddSystem(
      LcmPublisherSystem::Make<dairlib::lcmt_robot_input>(
          FLAGS_channel_u, &lcm));

  auto state_receiver = builder.AddSystem<RobotOutputReceiver>(plant);
  auto input_sender = builder.AddSystem<RobotCommandSender>(plant);


  // Wire diagram
  auto state_dmux = builder.AddSystem<SubvectorPassThrough>(
      state_receiver->get_output_port().size(), 0,
      plant.num_positions() + plant.num_velocities());

  auto time_dmux = builder.AddSystem<SubvectorPassThrough>(
      state_receiver->get_output_port().size(),
      state_receiver->get_output_port().size() -1, 1);
  std::vector sizes = {1, 1};

  /// TODO: new input mux which creates timestamped vector
  auto input_mux = builder.AddSystem<drake::systems::Multiplexer>(sizes);

  builder.Connect(*state_receiver, *state_dmux);
  builder.Connect(*state_receiver, *time_dmux);
  builder.Connect(*state_dmux, *builder.GetSystems().at(0));
  builder.Connect(builder.GetSystems().at(0)->get_output_port(),
      input_mux->get_input_port(0));
  builder.Connect(time_dmux->get_output_port(),
      input_mux->get_input_port(1));
  builder.Connect(*input_mux, *input_sender);
  builder.Connect(*input_sender, *input_publisher);

  // Build diagram
  auto diagram = builder.Build();
  diagram->set_name("cartpole_lqr_controller");

  LcmDrivenLoop<dairlib::lcmt_robot_output> loop(
      &lcm,
      std::move(diagram),
      state_receiver,
      FLAGS_channel_x,
      true);
  loop.Simulate();
}
}

int main(int argc, char* argv[]) {
  return dairlib::examples::cartpole::controller_main(argc, argv);
}