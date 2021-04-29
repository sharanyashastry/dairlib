import dairlib
import drake
import numpy as np
from pydairlib import lcm_trajectory
from pydrake.trajectories import PiecewisePolynomial

# Class to easily convert list of lcmt_osc_tracking_data_t to numpy arrays

class lcmt_osc_tracking_data_t:
    def __init__(self):
        self.t = []
        self.y_dim = 0
        self.name = ""
        self.is_active = []
        self.y = []
        self.y_des = []
        self.error_y = []
        self.ydot = []
        self.ydot_des = []
        self.error_ydot = []
        self.yddot_des = []
        self.yddot_command = []
        self.yddot_command_sol = []

    def append(self, msg, t):
        self.t.append(t)
        self.is_active.append(msg.is_active)
        self.y.append(msg.y)
        self.y_des.append(msg.y_des)
        self.error_y.append(msg.error_y)
        self.ydot.append(msg.ydot)
        self.ydot_des.append(msg.ydot_des)
        self.error_ydot.append(msg.error_ydot)
        self.yddot_des.append(msg.yddot_des)
        self.yddot_command.append(msg.yddot_command)
        self.yddot_command_sol.append(msg.yddot_command_sol)

    def convertToNP(self):
        self.t = np.array(self.t)
        self.is_active = np.array(self.is_active)
        self.y = np.array(self.y)
        self.y_des = np.array(self.y_des)
        self.error_y = np.array(self.error_y)
        self.ydot = np.array(self.ydot)
        self.ydot_des = np.array(self.ydot_des)
        self.error_ydot = np.array(self.error_ydot)
        self.yddot_des = np.array(self.yddot_des)
        self.yddot_command = np.array(self.yddot_command)
        self.yddot_command_sol = np.array(self.yddot_command_sol)

class mpc_trajectory_block:
    def __init__(self, block):
        self.trajectory_name = block.trajectory_name
        self.time_vec = np.array(block.time_vec)
        self.datapoints = np.array(block.datapoints)
        self.datatypes = block.datatypes

class mpc_trajectory:
    def __init__(self, msg):
        self.trajectories = dict()
        for block in msg.trajectories:
            self.trajectories[block.trajectory_name] = mpc_trajectory_block(block)

    def traj_as_cubic_with_continuous_second_derivatives(self, trajectory_name, npoints):
        traj_block = self.trajectories[trajectory_name]
        dim = int(traj_block.datapoints.shape[0] / 2)
        #import pdb; pdb.set_trace()

        pp_traj = PiecewisePolynomial.CubicWithContinuousSecondDerivatives(
            traj_block.time_vec, traj_block.datapoints[0:dim,:],
            traj_block.datapoints[dim:2*dim,0], traj_block.datapoints[dim:2*dim,-1])

        t = np.linspace(pp_traj.start_time(), pp_traj.end_time(), npoints)
        samples = np.zeros((npoints, pp_traj.value(pp_traj.start_time()).shape[0]))

        for i in range(npoints):
            samples[i] = pp_traj.value(t[i])[:,0]

        return t, samples

    def traj_as_cubic_hermite(self, trajectory_name, npoints):
        traj_block = self.trajectories[trajectory_name]
        dim = int(traj_block.datapoints.shape[0] / 2)
        #import pdb; pdb.set_trace()

        pp_traj = PiecewisePolynomial.CubicHermite(
            traj_block.time_vec, traj_block.datapoints[0:dim,:],
            traj_block.datapoints[dim:2*dim,:])

        t = np.linspace(pp_traj.start_time(), pp_traj.end_time(), npoints)
        samples = np.zeros((npoints, pp_traj.value(pp_traj.start_time()).shape[0]))

        for i in range(npoints):
            samples[i] = pp_traj.value(t[i])[:,0]

        return t, samples

def process_mpc_log(log, pos_map, vel_map, act_map, robot_out_channel,
                mpc_channel, osc_channel, osc_debug_channel):
    t_x = []
    t_u = []
    t_controller_switch = []
    t_contact_info = []
    fsm = []
    q = []
    v = []
    u_meas = []
    u = []
    kp = []
    kd = []
    t_pd = []
    osc_debug = dict()
    contact_forces = [[], []]
    contact_info_locs = [[], []]
    robot_out = []  # robot out types
    osc_output = []
    mpc_output = []


    full_log = dict()
    channel_to_type_map = dict()
    unknown_types = set()
    known_lcm_types = [dairlib.lcmt_robot_output, dairlib.lcmt_saved_traj, dairlib.lcmt_dairlib_signal,
                       dairlib.lcmt_osc_output, dairlib.lcmt_pd_config, dairlib.lcmt_robot_input,
                       drake.lcmt_contact_results_for_viz, dairlib.lcmt_contact]

    for event in log:
        if event.channel not in full_log and event.channel not in unknown_types:
            for lcmtype in known_lcm_types:
                try:
                    lcmtype.decode(event.data)
                    channel_to_type_map[event.channel] = lcmtype
                except ValueError:
                    continue
            if event.channel in channel_to_type_map:
                full_log[event.channel] = []
            else:
                unknown_types.add(event.channel)
        if event.channel in full_log:
            full_log[event.channel].append(channel_to_type_map[event.channel].decode(event.data))
        if event.channel == robot_out_channel:
            msg = dairlib.lcmt_robot_output.decode(event.data)
            q_temp = [[] for i in range(len(msg.position))]
            v_temp = [[] for i in range(len(msg.velocity))]
            u_temp = [[] for i in range(len(msg.effort))]
            for i in range(len(q_temp)):
                q_temp[pos_map[msg.position_names[i]]] = msg.position[i]
            for i in range(len(v_temp)):
                v_temp[vel_map[msg.velocity_names[i]]] = msg.velocity[i]
            for i in range(len(u_temp)):
                u_temp[act_map[msg.effort_names[i]]] = msg.effort[i]
            q.append(q_temp)
            v.append(v_temp)
            u_meas.append(u_temp)
            t_x.append(msg.utime / 1e6)

        if event.channel == mpc_channel:
            msg = dairlib.lcmt_saved_traj.decode(event.data)
            if msg.num_trajectories > 0:
                mpc_output.append(mpc_trajectory(msg))

        if event.channel == osc_channel:
            msg = dairlib.lcmt_robot_input.decode(event.data)
            u.append(msg.efforts)
            t_u.append(msg.utime / 1e6)

        if event.channel == osc_debug_channel:
            msg = dairlib.lcmt_osc_output.decode(event.data)
            osc_output.append(msg)
            num_osc_tracking_data = len(msg.tracking_data)
            for i in range(num_osc_tracking_data):
                if msg.tracking_data[i].name not in osc_debug:
                    osc_debug[msg.tracking_data[i].name] = lcmt_osc_tracking_data_t()
                osc_debug[msg.tracking_data[i].name].append(msg.tracking_data[i], msg.utime / 1e6)
            fsm.append(msg.fsm_state)
        if event.channel == "CONTACT_RESULTS":
            # Need to distinguish between front and rear contact forces
            # Best way is to track the contact location and group by proximity
            msg = drake.lcmt_contact_results_for_viz.decode(event.data)
            t_contact_info.append(msg.timestamp / 1e6)
            num_left_contacts = 0
            num_right_contacts = 0
            for i in range(msg.num_point_pair_contacts):
                if "left" in msg.point_pair_contact_info[i].body1_name:
                    contact_info_locs[num_left_contacts].append(
                        msg.point_pair_contact_info[i].contact_point)
                    contact_forces[num_left_contacts].append(
                        msg.point_pair_contact_info[i].contact_force)
                    num_left_contacts += 1
                elif "right" in msg.point_pair_contact_info[i].body1_name:
                    contact_info_locs[1 + num_right_contacts].append(
                        msg.point_pair_contact_info[i].contact_point)
                    contact_forces[1 + num_right_contacts].append(
                        msg.point_pair_contact_info[i].contact_force)
                    num_right_contacts += 1
                    # print("ERROR")
            while num_left_contacts != 1:
                contact_forces[num_left_contacts].append((0.0, 0.0, 0.0))
                contact_info_locs[num_left_contacts].append((0.0, 0.0, 0.0))
                num_left_contacts += 1
            while num_right_contacts != 1:
                contact_forces[1 + num_right_contacts].append((0.0, 0.0, 0.0))
                contact_info_locs[1 + num_right_contacts].append((0.0, 0.0, 0.0))
                num_right_contacts += 1

    # Convert into numpy arrays
    t_x = np.array(t_x)
    t_u = np.array(t_u)
    t_controller_switch = np.array(t_controller_switch)
    t_contact_info = np.array(t_contact_info)
    t_pd = np.array(t_pd)
    fsm = np.array(fsm)
    q = np.array(q)
    v = np.array(v)
    u_meas = np.array(u_meas)
    u = np.array(u)
    kp = np.array(kp)
    kd = np.array(kd)
    contact_forces = np.array(contact_forces)
    contact_info_locs = np.array(contact_info_locs)

    for key in osc_debug:
        osc_debug[key].convertToNP()

    x = np.hstack((q, v))  # combine into state vector

    return mpc_output, x, u_meas, t_x, u, t_u, contact_forces, contact_info_locs, \
           t_contact_info, osc_debug, fsm, t_controller_switch, t_pd, kp, kd, \
           robot_out, osc_output, full_log
