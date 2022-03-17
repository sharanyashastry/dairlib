from cassie_gym import *
# from cassie_utils import *
from pydairlib.cassie.controllers import OSCRunningControllerFactory
from pydairlib.cassie.simulators import CassieSimDiagram
from pydrake.common.yaml import yaml_load

# from controllers import OSCRunningControllerFactory

def main():
    osc_gains_filename = 'examples/Cassie/osc_run/osc_running_gains.yaml'
    osqp_settings = 'examples/Cassie/osc_run/osc_running_qp_settings.yaml'
    urdf = 'examples/Cassie/urdf/cassie_v2.urdf'
    # osc_gains = yaml_load(filename=osc_gains_filename)
    # import pdb; pdb.set_trace()

    controller_plant = MultibodyPlant(1e-5)
    addCassieMultibody(controller_plant, None, True, urdf, False, False)
    controller_plant.Finalize()
    controller = OSCRunningControllerFactory(controller_plant, osc_gains_filename, osqp_settings)
    gym_env = CassieGym(visualize=True)

    gym_env.make(controller, urdf)
    gym_env.advance_to(10)


if __name__ == '__main__':
    main()
