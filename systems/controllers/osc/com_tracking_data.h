#include "impact_invariant_tracking_data.h"

namespace dairlib {
namespace systems {
namespace controllers {
/// ComTrackingData is used when we want to track center of mass trajectory.
class ComTrackingData final : public ImpactInvariantTrackingData {
 public:
  ComTrackingData(const std::string& name, const Eigen::MatrixXd& K_p,
                  const Eigen::MatrixXd& K_d, const Eigen::MatrixXd& W,
                  const drake::multibody::MultibodyPlant<double>& plant_w_spr,
                  const drake::multibody::MultibodyPlant<double>& plant_wo_spr);

//  // If state is not specified, it will track COM for all states
//  void AddStateToTrack(int state);

 private:
  void UpdateYddotDes(double t) final;
  void UpdateY(const Eigen::VectorXd& x_w_spr,
               const drake::systems::Context<double>& context_w_spr) final;
  void UpdateYError() final;
  void UpdateYdot(const Eigen::VectorXd& x_w_spr,
                  const drake::systems::Context<double>& context_w_spr) final;
  void UpdateYdotError() final;
  void UpdateJ(const Eigen::VectorXd& x_wo_spr,
               const drake::systems::Context<double>& context_wo_spr) final;
  void UpdateJdotV(const Eigen::VectorXd& x_wo_spr,
                   const drake::systems::Context<double>& context_wo_spr) final;

  void CheckDerivedOscTrackingData() final;
};

}  // namespace controllers
}  // namespace systems
}  // namespace dairlib