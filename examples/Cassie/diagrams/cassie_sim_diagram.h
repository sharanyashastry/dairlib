#pragma once

#include <memory>
#include <utility>

#include <drake/multibody/plant/multibody_plant.h>

#include "examples/Cassie/systems/sim_cassie_sensor_aggregator.h"
#include "systems/framework/geared_motor.h"

#include "drake/common/drake_copyable.h"
#include "drake/systems/framework/diagram.h"
#include "drake/systems/framework/diagram_builder.h"
#include "drake/systems/framework/system.h"

namespace dairlib {
namespace examples {

class CassieSimDiagram : public drake::systems::Diagram<double> {
 public:
  DRAKE_NO_COPY_NO_MOVE_NO_ASSIGN(CassieSimDiagram)

  /// @param[in] urdf filepath containing the osc_running_gains.
  CassieSimDiagram(std::unique_ptr<drake::multibody::MultibodyPlant<double>> plant,
      const std::string& urdf = "examples/Cassie/urdf/cassie_v2.urdf", bool visualize = false,
      double mu = 0.8, double stiffness = 1e4, double dissipation_rate = 1e2);

  /// @return the input port for the actuation command.
  const drake::systems::InputPort<double>& get_actuation_input_port() const {
    return this->get_input_port(actuation_input_port_index_);
  }

  /// @return the input port for the radio struct.
  const drake::systems::InputPort<double>& get_radio_input_port() const {
    return this->get_input_port(radio_input_port_index_);
  }

  /// @return the output port for the plant state as an OutputVector.
  const drake::systems::OutputPort<double>& get_state_output_port() const {
    return this->get_output_port(state_output_port_index_);
  }

  /// @return the output port for the failure status of the controller.
  const drake::systems::OutputPort<double>& get_cassie_out_output_port_index()
      const {
    return this->get_output_port(cassie_out_output_port_index_);
  }

  drake::multibody::MultibodyPlant<double>& get_plant() {
    return *plant_;
  }
  //  const drake::systems::System<double>& get_sensor_aggregator() { return
  //  *sensor_aggregator_; } const drake::systems::System<double>&
  //  get_radio_parser() { return *radio_parser_; }

 private:
  drake::multibody::MultibodyPlant<double>* plant_;
  const systems::SimCassieSensorAggregator* sensor_aggregator_;
  const systems::GearedMotor* cassie_motor_;
  //  const systems::RadioParser* radio_parser_;
  drake::geometry::SceneGraph<double>* scene_graph_;
  const int actuation_input_port_index_ = 0;
  const int radio_input_port_index_ = 1;
  const int state_output_port_index_ = 0;
  const int cassie_out_output_port_index_ = 1;
  const double actuator_delay = 5e-3;        // 5ms
  const double actuator_update_rate = 1e-3;  // 1ms
  const double dt_ = 8e-5;
};

}  // namespace examples
}  // namespace dairlib
