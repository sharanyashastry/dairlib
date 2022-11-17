#include <string>

#include <gflags/gflags.h>

#include "dairlib/lcmt_controller_switch.hpp"
#include "dairlib/lcmt_robot_output.hpp"
#include "dairlib/lcmt_target_standing_height.hpp"

#include "drake/lcm/drake_lcm.h"
#include "drake/systems/analysis/simulator.h"
#include "drake/systems/framework/diagram_builder.h"
#include "drake/systems/lcm/lcm_publisher_system.h"
#include "drake/systems/lcm/serializer.h"

/// This switch send two messages simultaneously. One to dispatcher_in for which
/// controller channel to listen to, and the other to the controller thread to
/// trigger the FSM.
/// The diagram contains only two leafsystems (two LcmPublisherSystem's).
/// The whole diagram is a lcm driven loop which triggered by dispatcher_out
/// messages.

namespace dairlib {

using drake::systems::TriggerType;
using drake::systems::lcm::LcmPublisherSystem;
using drake::systems::lcm::TriggerTypeSet;

DEFINE_string(channel_x, "CASSIE_STATE_DISPATCHER",
              "The name of the channel which receives state");

DEFINE_int32(n_publishes, 3,
             "The simulation gets updated until it publishes the channel name "
             "n_publishes times");

// Message to trigger the start of FSM (in the controller thread)
DEFINE_string(fsm_trigger_channel, "MPC_SWITCH",
              "The name of the channel which sends the trigger value");

// Message to change which channel dispatcher_in listens to
DEFINE_string(switch_channel, "INPUT_SWITCH",
              "The name of the channel which sends the channel name that "
              "dispatcher_in listens to");
DEFINE_string(new_channel, "ROM_WALKING",
              "The name of the new lcm channel that dispatcher_in listens to "
              "after switch");
DEFINE_double(blend_duration, 1.0,
              "Duration to blend efforts between previous and current "
              "controller command");

int do_main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Parameters
  drake::lcm::DrakeLcm lcm_local("udpm://239.255.76.67:7667?ttl=0");

  // Build the diagram
  drake::systems::DiagramBuilder<double> builder;
  auto fsm_trigger_pub = builder.AddSystem(
      LcmPublisherSystem::Make<dairlib::lcmt_target_standing_height>(
          FLAGS_fsm_trigger_channel, &lcm_local,
          TriggerTypeSet({TriggerType::kForced})));
  auto name_pub = builder.AddSystem(
      LcmPublisherSystem::Make<dairlib::lcmt_controller_switch>(
          FLAGS_switch_channel, &lcm_local,
          TriggerTypeSet({TriggerType::kForced})));
  auto owned_diagram = builder.Build();
  owned_diagram->set_name(("switch publisher"));

  // Create simulator
  drake::systems::Diagram<double>* diagram_ptr = owned_diagram.get();
  drake::systems::Simulator<double> simulator(std::move(owned_diagram));
  auto& diagram_context = simulator.get_mutable_context();

  // Create subscriber for lcm driven loop
  drake::lcm::Subscriber<dairlib::lcmt_robot_output> input_sub(&lcm_local,
                                                               FLAGS_channel_x);

  // Wait for the first message and initialize the context time..
  drake::log()->info("Waiting for first lcm input message");
  LcmHandleSubscriptionsUntil(&lcm_local,
                              [&]() { return input_sub.count() > 0; });
  const double t0 = input_sub.message().utime * 1e-6;
  diagram_context.SetTime(t0);

  // Create output message
  dairlib::lcmt_target_standing_height trigger_msg;
  trigger_msg.timestamp = 0;      // doesn't matter
  trigger_msg.target_height = 1;  // high signal (greater 0.5)

  dairlib::lcmt_controller_switch msg;
  msg.channel = FLAGS_new_channel;
  msg.blend_duration = FLAGS_blend_duration;

  // Run the simulation until it publishes the channel name `n_publishes` times
  drake::log()->info(diagram_ptr->get_name() + " started");
  int pub_count = 0;
  while (pub_count < FLAGS_n_publishes) {
    // Wait for input message.
    input_sub.clear();
    LcmHandleSubscriptionsUntil(&lcm_local,
                                [&]() { return input_sub.count() > 0; });

    // Get message time from the input channel
    double t_current = input_sub.message().utime * 1e-6;
    std::cout << "publish at t = " << t_current << std::endl;

    fsm_trigger_pub->get_input_port().FixValue(
        &(diagram_ptr->GetMutableSubsystemContext(*fsm_trigger_pub,
                                                  &diagram_context)),
        trigger_msg);

    msg.utime = (int)input_sub.message().utime;
    name_pub->get_input_port().FixValue(
        &(diagram_ptr->GetMutableSubsystemContext(*name_pub, &diagram_context)),
        msg);

    // Force-publish via the diagram
    /// We don't need AdvanceTo(time) because we manually force publish lcm
    /// message, and there is nothing in the diagram that needs to be updated.
    diagram_ptr->Publish(diagram_context);

    pub_count++;
  }
  drake::log()->info(diagram_ptr->get_name() + " ended");

  return 0;
}

}  // namespace dairlib

int main(int argc, char* argv[]) { return dairlib::do_main(argc, argv); }
