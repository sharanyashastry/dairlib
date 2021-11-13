import sys
import lcm
import matplotlib.pyplot as plt
import numpy as np

import dairlib
from pydairlib.multibody import \
    createStateNameVectorFromMap, createActuatorNameVectorFromMap, \
    makeNameToPositionsMap, makeNameToVelocitiesMap, makeNameToActuatorsMap

from process_lcm_log import get_log_data, passthrough_callback
import cassie_plotting_utils


def main():
    filename = sys.argv[1]
    log = lcm.EventLog(filename, "r")

    plant, context = cassie_plotting_utils.make_plant_and_context(
        floating_base=True, springs=True)

    pos_map = makeNameToPositionsMap(plant)
    vel_map = makeNameToVelocitiesMap(plant)
    act_map = makeNameToActuatorsMap(plant)
    x_names = createStateNameVectorFromMap(plant)
    u_names = createActuatorNameVectorFromMap(plant)
    q_names = x_names[:plant.num_positions()]
    v_names = x_names[plant.num_positions():]

    robot_output, robot_input, osc_debug = \
        get_log_data(log,
                     cassie_plotting_utils.cassie_default_channels,
                     cassie_plotting_utils.load_default_channels,
                     plant,
                     'CASSIE_STATE_SIMULATION',
                     'CASSIE_INPUT',
                     'OSC_DEBUG_STANDING')

    t_x_slice = slice(robot_output['t_x'].size)

    cassie_plotting_utils.plot_floating_base_positions(
        robot_output, q_names, t_x_slice)
    cassie_plotting_utils.plot_joint_positions(
        robot_output, q_names, t_x_slice, floating_base=True)
    cassie_plotting_utils.plot_joint_positions_by_name(
        robot_output, ['knee_left', 'knee_right'], t_x_slice, pos_map)

    plt.show()


if __name__ == '__main__':
    main()

