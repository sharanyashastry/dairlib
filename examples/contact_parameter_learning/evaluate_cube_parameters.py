import os
import cube_sim
import drake_cube_sim
import mujoco_cube_sim
import bullet_cube_sim
import os
import sys
import json
from random import choice
from learn_cube_parameters import cube_data_folder, model_folder, log_folder, default_loss
from matplotlib import pyplot as plt
import numpy as np

def visualize_learned_params(params, sim_type, toss_id):
    cube_data = cube_sim.load_cube_toss(cube_sim.make_cube_toss_filename(cube_data_folder, toss_id))
    initial_state = cube_data[0].ravel()

    vis_sim = drake_cube_sim.DrakeCubeSim(visualize=True)

    if (sim_type == 'mujoco'):
        data_sim = mujoco_cube_sim.MujocoCubeSim()
    elif (sim_type == 'drake'):
        data_sim = drake_cube_sim.DrakeCubeSim()
    elif (sim_type == 'bullet'):
        data_sim = bullet_cube_sim.BulletCubeSim()

    data_sim.init_sim(params)
    sim_data = data_sim.get_sim_traj_initial_state(initial_state, cube_data.shape[0], cube_sim.CUBE_DATA_DT)

    vis_sim.visualize_two_cubes(cube_data, sim_data, 0.2)

def load_traj_pairs(sim, params, test_set):
    sim.init_sim(params)

    traj_pairs = {}
    for toss_id in test_set:
        cube_data = cube_sim.load_cube_toss(
            cube_sim.make_cube_toss_filename(cube_data_folder, toss_id))
        initial_state = cube_data[0]
        steps = cube_data.shape[0]
        sim_data = sim.get_sim_traj_initial_state(
            initial_state, steps, cube_sim.CUBE_DATA_DT)
        
        traj_pairs[toss_id] = (cube_data, sim_data)
    
    return traj_pairs

def calc_error_between_trajectories(traj_pair):
    data_traj = traj_pair[0]     
    sim_traj = traj_pair[1]
    errors = {}
    errors['position_error'] = np.linalg.norm(
        data_traj[:,cube_sim.CUBE_DATA_POSITION_SLICE] - \
        sim_traj[:,cube_sim.CUBE_DATA_POSITION_SLICE], axis=1) / cube_sim.BLOCK_HALF_WIDTH

    errors['velocity_error'] = np.linalg.norm(
        data_traj[:,cube_sim.CUBE_DATA_VELOCITY_SLICE] - \
        sim_traj[:,cube_sim.CUBE_DATA_VELOCITY_SLICE], axis=1) / cube_sim.BLOCK_HALF_WIDTH
    
    errors['omega_error'] = np.linalg.norm(
        data_traj[:,cube_sim.CUBE_DATA_OMEGA_SLICE] - \
        sim_traj[:,cube_sim.CUBE_DATA_OMEGA_SLICE], axis=1)
    
    quat_error = np.zeros((data_traj.shape[0]))

    for i in range(data_traj.shape[0]):
        quat_error[i] = cube_sim.LossWeights.calc_rotational_distance(
            data_traj[i, cube_sim.CUBE_DATA_QUATERNION_SLICE], 
            sim_traj[i, cube_sim.CUBE_DATA_QUATERNION_SLICE])
    errors['rotational_error'] = quat_error 

    return errors

def make_sim_to_real_comparison_plots_single_toss(traj_pair):
    data_traj = traj_pair[0]     
    tvec = cube_sim.CubeSim.make_traj_timestamps(data_traj)

    errors = calc_error_between_trajectories(traj_pair)
    
    for key in errors:
        plt.figure()
        plt.plot(tvec, errors[key])
        plt.title(key)

def get_error_and_loss_stats(traj_pairs):
    pos = []
    vel = []
    omega = []
    rot = []
    loss = []
    i = 0
    for pair_idx in traj_pairs:
        pair = traj_pairs[pair_idx]
        errors = calc_error_between_trajectories(pair)
        pos.append(np.mean(errors['position_error']))
        vel.append(np.mean(errors['velocity_error']))
        omega.append(np.mean(errors['omega_error']))
        rot.append(np.mean(errors['rotational_error']))
        loss.append(default_loss.CalculateLoss(pair[0], pair[1]))
        if not (i % 25): print(f'calculating means {i} %')
        i += 1
    pos_mean = np.mean(np.array(pos))
    vel_mean = np.mean(np.array(vel))
    omega_mean = np.mean(np.array(omega))
    rot_mean = np.mean(np.array(rot))
    loss_mean = np.mean(np.array(loss))

    return {'pos_mean' : pos_mean, 
            'vel_mean' : vel_mean,
            'omega_mean' : omega_mean, 
            'rot_mean' : rot_mean,
            'loss_mean' : loss_mean}

def load_params(simulator, id):
    filename = os.path.join(model_folder, simulator + '_' + id +'.json')
    with open(filename, 'r+') as fp:
        return json.load(fp)

if (__name__ == '__main__'):
    
    learning_result = sys.argv[1]
    sim_type = learning_result.split('_')[0]

    if (sim_type == 'mujoco'):
        eval_sim = mujoco_cube_sim.MujocoCubeSim()
    elif (sim_type == 'drake'):
        eval_sim = drake_cube_sim.DrakeCubeSim()
    elif (sim_type == 'bullet'):
        eval_sim = bullet_cube_sim.BulletCubeSim()
    else:
        print(f'{sim_type} is not a supported simulator - please check for spelling mistakes and try again')
        quit()
    
    # load learned parameters and logging info
    with open(os.path.join(model_folder, learning_result + '.json'), 'r') as fp:
        params = json.load(fp)
    
    logdir = os.path.join(log_folder, learning_result)
    with open(os.path.join(logdir, 'test_set.json'), 'r') as fp:
        test_set = json.load(fp)
    
    traj_pairs = load_traj_pairs(eval_sim, params, test_set)
    stats = get_error_and_loss_stats(traj_pairs)
    print(stats)
    
