#include <iostream>

#include "robot_lcm_systems.h"

#include "multibody/multibody_utils.h"

#include "drake/multibody/plant/multibody_plant.h"
#include "drake/systems/lcm/lcm_publisher_system.h"
#include "drake/systems/lcm/lcm_subscriber_system.h"
#include "drake/systems/primitives/discrete_time_delay.h"

#include "dairlib/lcmt_robot_input.hpp"
#include "dairlib/lcmt_robot_output.hpp"

namespace dairlib {
namespace systems {

using drake::multibody::JointActuatorIndex;
using drake::multibody::JointIndex;
using drake::multibody::MultibodyPlant;
using drake::systems::Context;
using drake::systems::lcm::LcmPublisherSystem;
using drake::systems::lcm::LcmSubscriberSystem;
using drake::systems::LeafSystem;
using Eigen::VectorXd;
using std::string;
using systems::OutputVector;

/*--------------------------------------------------------------------------*/
// methods implementation for RobotOutputReceiver.

RobotOutputReceiver::RobotOutputReceiver(
    const drake::multibody::MultibodyPlant<double>& plant) {
  num_positions_ = plant.num_positions();
  num_velocities_ = plant.num_velocities();
  num_efforts_ = plant.num_actuators();
  positionIndexMap_ = multibody::MakeNameToPositionsMap(plant);
  velocityIndexMap_ = multibody::MakeNameToVelocitiesMap(plant);
  effortIndexMap_ = multibody::MakeNameToActuatorsMap(plant);
  this->DeclareAbstractInputPort("lcmt_robot_output",
                                 drake::Value<dairlib::lcmt_robot_output>{});
  this->DeclareVectorOutputPort(
      "x, u, t",
      OutputVector<double>(plant.num_positions(), plant.num_velocities(),
                           plant.num_actuators()),
      &RobotOutputReceiver::CopyOutput);
}

void RobotOutputReceiver::CopyOutput(const Context<double>& context,
                                     OutputVector<double>* output) const {
  const drake::AbstractValue* input = this->EvalAbstractInput(context, 0);
  DRAKE_ASSERT(input != nullptr);
  const auto& state_msg = input->get_value<dairlib::lcmt_robot_output>();

  VectorXd positions = VectorXd::Zero(num_positions_);
  for (int i = 0; i < state_msg.num_positions; i++) {
    int j = positionIndexMap_.at(state_msg.position_names[i]);
    positions(j) = state_msg.position[i];
  }
  VectorXd velocities = VectorXd::Zero(num_velocities_);
  for (int i = 0; i < state_msg.num_velocities; i++) {
    int j = velocityIndexMap_.at(state_msg.velocity_names[i]);
    velocities(j) = state_msg.velocity[i];
  }
  VectorXd efforts = VectorXd::Zero(num_efforts_);
  for (int i = 0; i < state_msg.num_efforts; i++) {
    int j = effortIndexMap_.at(state_msg.effort_names[i]);
    efforts(j) = state_msg.effort[j];
  }

  VectorXd imu = VectorXd::Zero(3);
  if (num_positions_ != num_velocities_) {
    for (int i = 0; i < 3; ++i) {
      imu[i] = state_msg.imu_accel[i];
    }
  }

  output->SetPositions(positions);
  output->SetVelocities(velocities);
  output->SetEfforts(efforts);
  output->SetIMUAccelerations(imu);
  output->set_timestamp(state_msg.utime * 1.0e-6);
}

/*--------------------------------------------------------------------------*/
// methods implementation for RobotOutputSender.

RobotOutputSender::RobotOutputSender(
    const drake::multibody::MultibodyPlant<double>& plant,
    const bool publish_efforts, const bool publish_imu)
    : publish_efforts_(publish_efforts), publish_imu_(publish_imu) {
  num_positions_ = plant.num_positions();
  num_velocities_ = plant.num_velocities();
  num_efforts_ = plant.num_actuators();

  positionIndexMap_ = multibody::MakeNameToPositionsMap(plant);
  velocityIndexMap_ = multibody::MakeNameToVelocitiesMap(plant);
  effortIndexMap_ = multibody::MakeNameToActuatorsMap(plant);

  // Loop through the maps to extract ordered names
  for (int i = 0; i < num_positions_; ++i) {
    for (auto& x : positionIndexMap_) {
      if (x.second == i) {
        ordered_position_names_.push_back(x.first);
        break;
      }
    }
  }

  for (int i = 0; i < num_velocities_; ++i) {
    for (auto& x : velocityIndexMap_) {
      if (x.second == i) {
        ordered_velocity_names_.push_back(x.first);
        break;
      }
    }
  }

  for (int i = 0; i < num_efforts_; i++) {
    for (auto& x : effortIndexMap_) {
      if (x.second == i) {
        ordered_effort_names_.push_back(x.first);
        break;
      }
    }
  }

  state_input_port_ =
      this->DeclareVectorInputPort(
              "x", BasicVector<double>(num_positions_ + num_velocities_))
          .get_index();
  if (publish_efforts_) {
    effort_input_port_ =
        this->DeclareVectorInputPort("u", BasicVector<double>(num_efforts_))
            .get_index();
  }
  if (publish_imu_) {
    imu_input_port_ =
        this->DeclareVectorInputPort("imu_acceleration", BasicVector<double>(3))
            .get_index();
  }

  this->DeclareAbstractOutputPort("lcmt_robot_output",
                                  &RobotOutputSender::Output);
}

/// Populate a state message with all states
void RobotOutputSender::Output(const Context<double>& context,
                               dairlib::lcmt_robot_output* state_msg) const {
  const auto state = this->EvalVectorInput(context, state_input_port_);

  // using the time from the context
  state_msg->utime = context.get_time() * 1e6;

  state_msg->num_positions = num_positions_;
  state_msg->num_velocities = num_velocities_;
  state_msg->position_names.resize(num_positions_);
  state_msg->velocity_names.resize(num_velocities_);
  state_msg->position.resize(num_positions_);
  state_msg->velocity.resize(num_velocities_);

  for (int i = 0; i < num_positions_; i++) {
    state_msg->position_names[i] = ordered_position_names_[i];
    if (std::isnan(state->GetAtIndex(i))) {
      state_msg->position[i] = 0;
    } else {
      state_msg->position[i] = state->GetAtIndex(i);
    }
  }
  for (int i = 0; i < num_velocities_; i++) {
    state_msg->velocity[i] = state->GetAtIndex(num_positions_ + i);
    state_msg->velocity_names[i] = ordered_velocity_names_[i];
  }

  if (publish_efforts_) {
    const auto efforts = this->EvalVectorInput(context, effort_input_port_);

    state_msg->num_efforts = num_efforts_;
    state_msg->effort_names.resize(num_efforts_);
    state_msg->effort.resize(num_efforts_);

    for (int i = 0; i < num_efforts_; i++) {
      state_msg->effort[i] = efforts->GetAtIndex(i);
      state_msg->effort_names[i] = ordered_effort_names_[i];
    }
  }

  if (publish_imu_) {
    const auto imu = this->EvalVectorInput(context, imu_input_port_);
    for (int i = 0; i < 3; ++i) {
      state_msg->imu_accel[i] = imu->get_value()[i];
    }
  }
}

/*--------------------------------------------------------------------------*/
// methods implementation for RobotInputReceiver.

RobotInputReceiver::RobotInputReceiver(
    const drake::multibody::MultibodyPlant<double>& plant) {
  num_actuators_ = plant.num_actuators();
  actuatorIndexMap_ = multibody::MakeNameToActuatorsMap(plant);
  this->DeclareAbstractInputPort("lcmt_robot_input",
                                 drake::Value<dairlib::lcmt_robot_input>{});
  this->DeclareVectorOutputPort("u, t",
                                TimestampedVector<double>(num_actuators_),
                                &RobotInputReceiver::CopyInputOut);
}

void RobotInputReceiver::CopyInputOut(const Context<double>& context,
                                      TimestampedVector<double>* output) const {
  const drake::AbstractValue* input = this->EvalAbstractInput(context, 0);
  DRAKE_ASSERT(input != nullptr);
  const auto& input_msg = input->get_value<dairlib::lcmt_robot_input>();

  VectorXd input_vector = VectorXd::Zero(num_actuators_);

  for (int i = 0; i < input_msg.num_efforts; i++) {
    int j = actuatorIndexMap_.at(input_msg.effort_names[i]);
    input_vector(j) = input_msg.efforts[i];
  }
  output->SetDataVector(input_vector);
  output->set_timestamp(input_msg.utime * 1.0e-6);
}

/*--------------------------------------------------------------------------*/
// methods implementation for RobotCommandSender.

RobotCommandSender::RobotCommandSender(
    const drake::multibody::MultibodyPlant<double>& plant) {
  num_actuators_ = plant.num_actuators();
  actuatorIndexMap_ = multibody::MakeNameToActuatorsMap(plant);

  for (JointActuatorIndex i(0); i < plant.num_actuators(); ++i) {
    ordered_actuator_names_.push_back(plant.get_joint_actuator(i).name());
  }

  this->DeclareVectorInputPort("u, t",
                               TimestampedVector<double>(num_actuators_));
  this->DeclareAbstractOutputPort("lcmt_robot_input",
                                  &RobotCommandSender::OutputCommand);
}

void RobotCommandSender::OutputCommand(
    const Context<double>& context,
    dairlib::lcmt_robot_input* input_msg) const {
  const TimestampedVector<double>* command =
      (TimestampedVector<double>*)this->EvalVectorInput(context, 0);

  input_msg->utime = command->get_timestamp() * 1e6;
  input_msg->num_efforts = num_actuators_;
  input_msg->effort_names.resize(num_actuators_);
  input_msg->efforts.resize(num_actuators_);
  for (int i = 0; i < num_actuators_; i++) {
    input_msg->effort_names[i] = ordered_actuator_names_[i];
    if (std::isnan(command->GetAtIndex(i))) {
      input_msg->efforts[i] = 0;
    } else {
      input_msg->efforts[i] = command->GetAtIndex(i);
    }
  }
}

SubvectorPassThrough<double>* AddActuationRecieverAndStateSenderLcm(
    drake::systems::DiagramBuilder<double>* builder,
    const MultibodyPlant<double>& plant,
    drake::systems::lcm::LcmInterfaceSystem* lcm,
    std::string actuator_channel,
    std::string state_channel,
    double publish_rate,
    bool publish_efforts,
    double actuator_delay) {

  // Create LCM input for actuators
  auto input_sub =
      builder->AddSystem(LcmSubscriberSystem::Make<dairlib::lcmt_robot_input>(
          actuator_channel, lcm));
  auto input_receiver = builder->AddSystem<RobotInputReceiver>(plant);
  auto passthrough = builder->AddSystem<SubvectorPassThrough>(
      input_receiver->get_output_port(0).size(), 0,
      plant.get_actuation_input_port().size());
  builder->Connect(*input_sub, *input_receiver);
  builder->Connect(*input_receiver, *passthrough);

  // Create LCM output for state and efforts
  auto state_pub =
      builder->AddSystem(LcmPublisherSystem::Make<dairlib::lcmt_robot_output>(
          state_channel, lcm, 1.0 / publish_rate));
  auto state_sender = builder->AddSystem<RobotOutputSender>(
      plant, publish_efforts);
  builder->Connect(plant.get_state_output_port(),
                   state_sender->get_input_port_state());

  // Add delay, if used, and associated connections
  if (actuator_delay > 0) {
      auto discrete_time_delay =
          builder->AddSystem<drake::systems::DiscreteTimeDelay>(
              1.0 / publish_rate, actuator_delay * publish_rate,
              plant.num_actuators());
      builder->Connect(*passthrough, *discrete_time_delay);
      builder->Connect(discrete_time_delay->get_output_port(),
                       plant.get_actuation_input_port());

      if (publish_efforts) {
        builder->Connect(discrete_time_delay->get_output_port(),
                         state_sender->get_input_port_effort());
      }
  } else {
      builder->Connect(passthrough->get_output_port(),
                       plant.get_actuation_input_port());
      if (publish_efforts) {
        builder->Connect(passthrough->get_output_port(),
                         state_sender->get_input_port_effort());
      }
  }

  builder->Connect(*state_sender, *state_pub);

  return passthrough;
}

void InitializeRobotOutputSubscriberQuaternionPositions(
    Context<double>& context, const MultibodyPlant<double>& plant) {

  auto& state_msg = context.get_mutable_abstract_state<lcmt_robot_output>(0);

  // using the time from the context
  state_msg.utime = context.get_time() * 1e6;

  const int nq = plant.num_positions();
  const int nv = plant.num_velocities();
  const int nu = plant.num_actuators();


  const auto& positionIndexMap = multibody::MakeNameToPositionsMap(plant);
  const auto& velocityIndexMap = multibody::MakeNameToVelocitiesMap(plant);
  const auto& effortIndexMap = multibody::MakeNameToActuatorsMap(plant);

  std::vector<std::string> ordered_position_names;
  std::vector<std::string> ordered_velocity_names;
  std::vector<std::string> ordered_effort_names;


  // Loop through the maps to extract ordered names
  for (int i = 0; i < nq; ++i) {
    for (auto& x : positionIndexMap) {
      if (x.second == i) {
        ordered_position_names.push_back(x.first);
        break;
      }
    }
  }

  for (int i = 0; i < nv; ++i) {
    for (auto& x : velocityIndexMap) {
      if (x.second == i) {
        ordered_velocity_names.push_back(x.first);
        break;
      }
    }
  }

  for (int i = 0; i < nu; i++) {
    for (auto& x : effortIndexMap) {
      if (x.second == i) {
        ordered_effort_names.push_back(x.first);
        break;
      }
    }
  }

  state_msg.num_positions = nq;
  state_msg.num_velocities = nv;
  state_msg.position_names.resize(nq);
  state_msg.velocity_names.resize(nv);
  state_msg.position.resize(nq);
  state_msg.velocity.resize(nv);

  for (int i = 0; i < nq; i++) {
    state_msg.position_names[i] = ordered_position_names[i];
    state_msg.position[i] = 0;
  }

  // Set quaternion w = 1, assumes drake quaternion ordering of wxyz
  for (const auto& body_idx : plant.GetFloatingBaseBodies()) {
    const auto& body = plant.get_body(body_idx);
    if (body.has_quaternion_dofs()) {
      state_msg.position[body.floating_positions_start()] = 1;
    }
  }

  for (int i = 0; i < nv; i++) {
    state_msg.velocity[i] = 0;
    state_msg.velocity_names[i] = ordered_velocity_names[i];
  }
}

}  // namespace systems
}  // namespace dairlib
