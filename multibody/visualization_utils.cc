#include "multibody/visualization_utils.h"

#include "multibody/com_pose_system.h"
#include "systems/primitives/subvector_pass_through.h"
#include "drake/geometry/drake_visualizer.h"
#include "drake/systems/primitives/trajectory_source.h"

namespace dairlib {
namespace multibody {
using drake::geometry::Sphere;
using drake::math::RigidTransformd;
using drake::multibody::MultibodyPlant;
using drake::multibody::RigidBody;
using drake::multibody::SpatialInertia;
using drake::multibody::UnitInertia;
using drake::systems::rendering::MultibodyPositionToGeometryPose;
using drake::trajectories::Trajectory;
using systems::SubvectorPassThrough;

std::unique_ptr<MultibodyPlant<double>> ConstructBallPlant(
    drake::geometry::SceneGraph<double>* scene_graph) {
  auto ball_plant = std::make_unique<MultibodyPlant<double>>(0.0);
  double radius = .02;
  UnitInertia<double> G_Bcm = UnitInertia<double>::SolidSphere(radius);
  SpatialInertia<double> M_Bcm(1, Eigen::Vector3d::Zero(), G_Bcm);
  const RigidBody<double>& ball = ball_plant->AddRigidBody("Ball", M_Bcm);
  ball_plant->RegisterAsSourceForSceneGraph(scene_graph);
  const Eigen::Vector4d orange(1.0, 0.55, 0.0, 1.0);
  const RigidTransformd X_BS = RigidTransformd::Identity();
  ball_plant->RegisterVisualGeometry(ball, X_BS, Sphere(radius), "visual",
                                     orange);
  ball_plant->Finalize();

  return ball_plant;
}

void connectTrajectoryVisualizer(
    const MultibodyPlant<double>* plant,
    drake::systems::DiagramBuilder<double>* builder,
    drake::geometry::SceneGraph<double>* scene_graph,
    const Trajectory<double>& trajectory) {
  auto empty_plant = std::make_unique<MultibodyPlant<double>>(0.0);
  connectTrajectoryVisualizer(plant, builder, scene_graph, trajectory,
                              *empty_plant);
}

void connectTrajectoryVisualizer(
    const MultibodyPlant<double>* plant,
    drake::systems::DiagramBuilder<double>* builder,
    drake::geometry::SceneGraph<double>* scene_graph,
    const Trajectory<double>& trajectory,
    const MultibodyPlant<double>& ball_plant) {
  auto traj_source =
      builder->AddSystem<drake::systems::TrajectorySource>(trajectory);
  auto passthrough = builder->AddSystem<SubvectorPassThrough>(
      plant->num_positions() + plant->num_velocities(), 0,
      plant->num_positions());
  builder->Connect(traj_source->get_output_port(),
                   passthrough->get_input_port());
  auto to_pose =
      builder->AddSystem<MultibodyPositionToGeometryPose<double>>(*plant);
  builder->Connect(passthrough->get_output_port(), to_pose->get_input_port());

  builder->Connect(
      to_pose->get_output_port(),
      scene_graph->get_source_pose_port(plant->get_source_id().value()));

  // *******Add COM visualization**********
  if (ball_plant.is_finalized()) {
    // connect
    auto q_passthrough = builder->AddSystem<SubvectorPassThrough>(
        traj_source->get_output_port().size(), 0, plant->num_positions());
    builder->Connect(traj_source->get_output_port(),
                     q_passthrough->get_input_port());
    auto rbt_passthrough = builder->AddSystem<multibody::ComPoseSystem>(*plant);

    auto ball_to_pose =
        builder->AddSystem<MultibodyPositionToGeometryPose<double>>(ball_plant);
    builder->Connect(*q_passthrough, *rbt_passthrough);
    builder->Connect(rbt_passthrough->get_xy_com_output_port(),
                     ball_to_pose->get_input_port());
    builder->Connect(
        ball_to_pose->get_output_port(),
        scene_graph->get_source_pose_port(ball_plant.get_source_id().value()));
  }

  drake::geometry::DrakeVisualizer::AddToBuilder(builder, *scene_graph);
}

}  // namespace multibody
}  // namespace dairlib
