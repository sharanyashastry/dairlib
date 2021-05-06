import subprocess
import time
import os
from pathlib import Path
from datetime import datetime

import yaml
import csv
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits import mplot3d
from matplotlib import cm
import matplotlib.tri as mtri


def build_files(bazel_file_argument):
  build_cmd = ['bazel', 'build', bazel_file_argument, ]
  build_process = subprocess.Popen(build_cmd)
  while build_process.poll() is None:  # while subprocess is alive
    time.sleep(0.1)


def lcmlog_file_path(rom_iter_idx, task_idx):
  return eval_dir + 'lcmlog-idx_%d_%d' % (rom_iter_idx, task_idx)


def get_nominal_task_given_sample_idx(sample_idx, name):
  # Get task element index by name
  task_element_idx = np.where(task_names == name)[0][0]
  task = np.loadtxt(model_dir + "%d_%d_task.csv" % (0, sample_idx))
  return task[task_element_idx]


# Set `get_init_file` to True if you want to generate the initial traj for both
# planner and controller
# `sample_idx` is used for planner's initial guess and cost regularization term
def run_sim_and_controller(sim_end_time, task_value, task_idx, rom_iter_idx,
    sample_idx, get_init_file):
  # Hacky heuristic parameter
  stride_length_scaling = 1.0
  # stride_length_scaling = 1 + min(rom_iter_idx / 30.0, 1) * 0.15

  # simulation arguments
  target_realtime_rate = 1.0  # 0.04
  pause_second = 2.0 if get_init_file else 0
  init_traj_file = '' if get_init_file else '0_rom_trajectory'

  # planner arguments
  realtime_rate_for_time_limit = target_realtime_rate
  dynamic_time_limit = True
  use_ipopt = False
  knots_per_mode = 10
  feas_tol = 1e-2
  n_step = 2
  # time_limit is optional, set = 0 for realtime
  time_limit = 0.0 if dynamic_time_limit else 1.0 / target_realtime_rate * 0.2
  time_limit = 0.0 if get_init_file else time_limit
  planner_init_file = '' if get_init_file else '0_z.csv'

  planner_cmd = [
    'bazel-bin/examples/goldilocks_models/run_cassie_rom_planner_process',
    '--fix_duration=true',
    '--zero_touchdown_impact=true',
    '--log_solver_info=false',
    '--iter=%d' % rom_iter_idx,
    '--sample=%d' % sample_idx,
    '--knots_per_mode=%d' % knots_per_mode,
    '--n_step=%d' % n_step,
    '--feas_tol=%.6f' % feas_tol,
    '--stride_length=%.3f' % task_value,
    '--stride_length_scaling=%.3f' % stride_length_scaling,
    '--time_limit=%.3f' % time_limit,
    '--realtime_rate_for_time_limit=%.3f' % realtime_rate_for_time_limit,
    '--init_file=%s' % planner_init_file,
    '--use_ipopt=%s' % "true" if use_ipopt else str(get_init_file).lower(),
    '--log_data=%s' % str(get_init_file).lower(),
    '--run_one_loop_to_get_init_file=%s' % str(get_init_file).lower(),
    '--spring_model=%s' % str(spring_model).lower(),
  ]
  controller_cmd = [
    'bazel-bin/examples/goldilocks_models/run_cassie_rom_controller',
    '--channel_u=ROM_WALKING',
    '--const_walking_speed=true',
    '--stride_length=%.3f' % task_value,
    '--stride_length_scaling=%.3f' % stride_length_scaling,
    '--iter=%d' % rom_iter_idx,
    '--init_traj_file_name=%s' % init_traj_file,
    '--spring_model=%s' % str(spring_model).lower(),
  ]
  simulator_cmd = [
    'bazel-bin/examples/Cassie/multibody_sim',
    '--channel_u=ROM_WALKING',
    '--end_time=%.3f' % sim_end_time,
    '--pause_second=%.3f' % pause_second,
    '--init_height=%.3f' % 1.0,
    '--target_realtime_rate=%.3f' % target_realtime_rate,
    '--spring_model=%s' % str(spring_model).lower(),
  ]
  lcm_logger_cmd = [
    'lcm-logger',
    '-f',
    lcmlog_file_path(rom_iter_idx, task_idx),
  ]

  planner_process = subprocess.Popen(planner_cmd)
  controller_process = subprocess.Popen(controller_cmd)
  if not get_init_file:
    logger_process = subprocess.Popen(lcm_logger_cmd)
  time.sleep(3)
  simulator_process = subprocess.Popen(simulator_cmd)

  if get_init_file:
    # Wait for planner to end
    while planner_process.poll() is None:  # while subprocess is alive
      time.sleep(0.1)
    # Kill the rest of process
    controller_process.kill()
    simulator_process.kill()
  else:
    # Wait for simluation to end
    while simulator_process.poll() is None:  # while subprocess is alive
      time.sleep(0.1)
    # Kill the rest of process
    planner_process.kill()
    controller_process.kill()
    logger_process.kill()


# sim_end_time is used to check if the simulation ended early
# sample_idx here is used to name the file
def eval_cost(sim_end_time, rom_iter_idx, task_idx, multithread=False):
  eval_cost_cmd = [
    'bazel-bin/examples/goldilocks_models/eval_single_sim_performance',
    lcmlog_file_path(rom_iter_idx, task_idx),
    'ROM_WALKING',
    str(rom_iter_idx),
    str(task_idx),
    str(sim_end_time),
    str(spring_model),
  ]
  print(' '.join(eval_cost_cmd))
  eval_cost_process = subprocess.Popen(eval_cost_cmd)

  if multithread:
    return eval_cost_process
  else:
    # Wait for evaluation to end
    while eval_cost_process.poll() is None:  # while subprocess is alive
      time.sleep(0.1)


def ConstructSampleIndicesGivenModelAndTask(model_indices, task_list,
    exact_task_match):
  sample_indices = np.zeros((len(model_indices), len(task_list)),
    dtype=np.dtype(int))
  for i in range(len(model_indices)):
    for j in range(len(task_list)):
      sample_indices[i, j] = GetSampleIndexGivenTask(model_indices[i],
        task_list[j], exact_task_match)
  return sample_indices


# Get trajopt sample idx with the most similar task
def GetSampleIndexGivenTask(rom_iter, task, exact_task_match):
  j = 0
  dist_list = []
  while True:
    path = model_dir + "%d_%d_task.csv" % (rom_iter, j)
    # print("try " + path)
    if os.path.exists(path):
      to_task = np.loadtxt(path)
      dist_list.append(np.linalg.norm(to_task - task))
      j += 1
    else:
      break
  # print("dist_list = ")
  # print(dist_list)
  if len(dist_list) == 0:
    raise ValueError("ERROR: This path doesn't exist: " + path)
  sample_idx = np.argmin(np.array(dist_list))

  if exact_task_match:
    if np.min(np.array(dist_list)) != 0:
      raise ValueError("ERROR: no such task in trajopt")

  return sample_idx


def run_sim_and_eval_cost(model_indices, task_list, varying_task_element_idx,
    sample_indices, do_eval_cost=False):
  max_n_fail = 0

  n_total_sim = len(model_indices) * len(task_list)
  counter = 0
  for model_idx in range(len(model_indices)):
    for task_idx in range(len(task_list)):
      rom_iter = model_indices[model_idx]
      task = task_list[task_idx]
      sample_idx = sample_indices[model_idx][task_idx]

      print("\n===========\n")
      print("progress %.1f%%" % (float(counter) / n_total_sim * 100))
      print("run sim for model %d and task %.3f" % \
            (rom_iter, task[varying_task_element_idx]))

      path = eval_dir + '%d_%d_success.csv' % (rom_iter, task_idx)
      n_fail = 0
      # while True:
      while not os.path.exists(path):
        # Get the initial traj
        run_sim_and_controller(sim_end_time, task[varying_task_element_idx],
          task_idx, rom_iter, sample_idx, True)
        # Run the simulation
        run_sim_and_controller(sim_end_time, task[varying_task_element_idx],
          task_idx, rom_iter, sample_idx, False)

        # Evaluate the cost
        if do_eval_cost:
          eval_cost(sim_end_time, rom_iter, task_idx)

        # Delete the lcmlog
        # os.remove(lcmlog_file_path(rom_iter_idx, task_idx))

        if not os.path.exists(path):
          n_fail += 1
        if n_fail > max_n_fail:
          break
      counter += 1

  print("Finished evaluating. Current time = " + str(datetime.now()))


# This function assumes that simulation has been run and there exist lcm logs
def eval_cost_in_multithread(model_indices, task_list):
  working_threads = []
  n_max_thread = 12

  n_total_sim = len(model_indices) * len(task_list)
  counter = 0
  for rom_iter in model_indices:
    for idx in range(len(task_list)):
      print("\n===========\n")
      print("progress %.1f%%" % (float(counter) / n_total_sim * 100))
      print("run sim for model %d and task %d" % (rom_iter, idx))

      # Evaluate the cost
      path = eval_dir + '%d_%d_success.csv' % (rom_iter, idx)
      if not os.path.exists(path):
        working_threads.append(eval_cost(sim_end_time, rom_iter, idx, True))
      counter += 1

      # Wait for threads to finish once is more than n_max_thread
      while len(working_threads) >= n_max_thread:
        for j in range(len(working_threads)):
          if working_threads[j].poll() is None:  # subprocess is alive
            time.sleep(0.1)
          else:
            del working_threads[j]
            break

  print("Wait for all threads to join")
  while len(working_threads) > 0:
    for j in range(len(working_threads)):
      if working_threads[j].poll() is None:  # subprocess is alive
        time.sleep(0.1)
      else:
        del working_threads[j]
        break
  print("Finished evaluating. Current time = " + str(datetime.now()))


def find_cost_in_string(file_string, string_to_search):
  # We search from the end of the file
  word_location = file_string.rfind(string_to_search)
  number_idx_start = 0
  number_idx_end = 0
  idx = word_location
  while True:
    if file_string[idx] == '=':
      number_idx_start = idx
    elif file_string[idx] == '\n':
      number_idx_end = idx
      break
    idx += 1
  cost_value = float(file_string[number_idx_start + 1: number_idx_end])
  return cost_value


def plot_nominal_cost(model_indices, sample_idx):
  filename = '_' + str(sample_idx) + '_trajopt_settings_and_cost_breakdown.txt'

  costs = np.zeros((0, 1))
  for rom_iter_idx in model_indices:
    with open(model_dir + str(rom_iter_idx) + filename, 'rt') as f:
      contents = f.read()
    cost_x = find_cost_in_string(contents, "cost_x =")
    cost_u = find_cost_in_string(contents, "cost_u =")
    total_cost = cost_x + cost_u
    costs = np.vstack([costs, total_cost])

  # figname = "Nominal cost over model iterations"
  # plt.figure(figname, figsize=(6.4, 4.8))
  # plt.plot(model_indices, costs)
  # plt.ylabel('cost')
  # plt.xlabel('model iterations')
  # plt.legend(["total_cost"])
  # plt.show()
  return costs


def plot_cost_vs_model_and_task(model_indices, task_list, task_element_idx,
    sample_indices=[], plot_3d=True, save=False):
  # Parameters for visualization
  max_cost_to_ignore = 3  # 2
  mean_sl = 0.2
  delta_sl = 0.1  # 0.1 #0.005
  min_sl = mean_sl - delta_sl
  max_sl = mean_sl + delta_sl
  min_sl = -100
  max_sl = 100
  # min_sl = -0.33
  # max_sl = -0.31

  plot_nominal = len(sample_indices) > 0

  # mtc that stores model index, task value and cost
  mtc = np.zeros((0, 3))
  for rom_iter in model_indices:
    for idx in range(len(task_list)):
      path0 = eval_dir + '%d_%d_success.csv' % (rom_iter, idx)
      path1 = eval_dir + '%d_%d_cost_values.csv' % (rom_iter, idx)
      path2 = eval_dir + '%d_%d_ave_stride_length.csv' % (rom_iter, idx)
      if os.path.exists(path0):
        current_mtc = np.zeros((1, 3))
        ### Read cost
        cost = np.loadtxt(path1, delimiter=',')
        current_mtc[0, 2] = cost[-1]
        if cost[-1] > max_cost_to_ignore:
          continue
        ### Read desired task
        # task = np.loadtxt(model_dir + "%d_%d_task.csv" % (rom_iter, idx))
        # current_mtc[0, 1] = task[task_element_idx]
        ### Read actual task
        task = np.loadtxt(path2, delimiter=',').item()  # 0-dim scalar
        current_mtc[0, 1] = task
        if (task > max_sl) or (task < min_sl):
          continue
        ### Read model iteration
        current_mtc[0, 0] = rom_iter
        ### Assign values
        # print('Add (iter,idx) = (%d,%d)' % (rom_iter, idx))
        mtc = np.vstack([mtc, current_mtc])
  print(mtc.shape)

  nominal_mtc = np.zeros((0, 3))
  if plot_nominal:
    for rom_iter in model_indices:
      for i in range(len(task_list)):
        sub_mtc = np.zeros((1, 3))
        ### Read cost
        cost = plot_nominal_cost([rom_iter], sample_indices[rom_iter][i])[0][0]
        sub_mtc[0, 2] = cost
        if cost.item() > max_cost_to_ignore:
          continue
        ### Read nominal task
        task = np.loadtxt(model_dir + "%d_%d_task.csv" % (rom_iter, i))[
          task_element_idx]
        sub_mtc[0, 1] = task
        if (task > max_sl) or (task < min_sl):
          continue
        ### Read model iteration
        sub_mtc[0, 0] = rom_iter
        ### Assign values
        nominal_mtc = np.vstack([nominal_mtc, sub_mtc])
  print(nominal_mtc.shape)

  # Plot
  app = "_w_nom" if plot_nominal else ""
  if plot_3d:
    ### scatter plot
    fig = plt.figure(figsize=(10, 7))
    ax = plt.axes(projection="3d")
    ax.scatter3D(mtc[:, 0], mtc[:, 1], mtc[:, 2], color="green")
    if plot_nominal:
      ax.scatter3D(nominal_mtc[:, 0], nominal_mtc[:, 1], nominal_mtc[:, 2], "b")
    ax.set_xlabel('model iterations')
    ax.set_ylabel('stride length (m)')
    ax.set_zlabel('total cost')
    # plt.title("")
    if save:
      ax.view_init(90, -90)  # look from +z axis. model iter vs task
      plt.savefig("%smodel_ter_vs_task_scatterplot%s.png" % (eval_dir, app))
      ax.view_init(0, 0)  # look from x axis. cost vs task
      plt.savefig("%scost_vs_task_scatterplot%s.png" % (eval_dir, app))
      ax.view_init(0, -90)  # look from -y axis. cost vs model iteration
      plt.savefig("%scost_vs_model_iter_scatterplot%s.png" % (eval_dir, app))
    ax.view_init(0, -90)  # look from -y axis. cost vs model iteration

    ### level set plot
    fig = plt.figure(figsize=(10, 7))
    ax = plt.axes(projection="3d")
    if plot_nominal:
      # tcf = ax.tricontour(nominal_mtc[:, 0], nominal_mtc[:, 1],
      #   nominal_mtc[:, 2], zdir='y', cmap=cm.coolwarm)
      ax.scatter3D(nominal_mtc[:, 0], nominal_mtc[:, 1], nominal_mtc[:, 2], "b")
      # tcf = ax.plot_trisurf(nominal_mtc[:, 0], nominal_mtc[:, 1],
      #   nominal_mtc[:, 2], cmap=cm.coolwarm)
      pass
    tcf = ax.tricontour(mtc[:, 0], mtc[:, 1], mtc[:, 2], zdir='y',
      cmap=cm.coolwarm)
    fig.colorbar(tcf)
    ax.set_xlabel('model iterations')
    ax.set_ylabel('stride length (m)')
    ax.set_zlabel('total cost')
    ax.view_init(0, -90)  # look from -y axis. cost vs model iteration
    if save:
      plt.savefig("%scost_vs_model_iter_contour%s.png" % (eval_dir, app))

  else:
    ### 2D plot

    # The line along which we evaluate the cost (using interpolation)
    task = 0
    x = np.linspace(0, 100, 101)
    y = task * np.ones(101)

    plt.figure(figsize=(6.4, 4.8))
    plt.rcParams.update({'font.size': 14})
    triang = mtri.Triangulation(mtc[:, 0], mtc[:, 1])
    interpolator = mtri.LinearTriInterpolator(triang, mtc[:, 2])
    z = interpolator(x, y)
    plt.plot(x, z, 'k-', linewidth=3, label="Drake simulation")
    if plot_nominal:
      triang = mtri.Triangulation(nominal_mtc[:, 0], nominal_mtc[:, 1])
      interpolator = mtri.LinearTriInterpolator(triang, nominal_mtc[:, 2])
      z = interpolator(x, y)
      plt.plot(x, z, 'k--', linewidth=3, label="trajectory optimization")
      # plt.plot(model_indices, nominal_mtc[:, 2], 'k--', linewidth=3, label="trajectory optimization")

    plt.xlabel('model iterations')
    plt.ylabel('total cost')
    plt.legend()
    plt.title('stride length ' + str(task))
    plt.gcf().subplots_adjust(bottom=0.15)
    plt.gcf().subplots_adjust(left=0.15)
    if save:
      plt.savefig("%scost_vs_model_iter%s.png" % (eval_dir, app))


def GetVaryingTaskElementIdx(task_list):
  task_list_copy_for_test = np.copy(task_list)
  # Default to the first element in case all task element are fixed
  task_element_idx = 0

  found_varying_task = False
  for i in range(len(task_names)):
    task_list_copy_for_test[:, i] -= task_list_copy_for_test[0, i]
    if np.linalg.norm(task_list_copy_for_test[:, i]) == 0:
      continue
    else:
      if found_varying_task:
        raise ValueError("ERROR: task element #%d is not fixed" % i)
      else:
        task_element_idx = i
        found_varying_task = True
  return task_element_idx


if __name__ == "__main__":
  # Build files just in case forgetting
  build_files('examples/goldilocks_models/...')
  build_files('examples/Cassie:multibody_sim')

  # Read the controller parameters
  a_yaml_file = open(
    "examples/goldilocks_models/controller/osc_rom_walking_gains.yaml")
  parsed_yaml_file = yaml.load(a_yaml_file)
  model_dir = parsed_yaml_file.get('dir_model')

  eval_dir = "../dairlib_data/goldilocks_models/sim_cost_eval/"
  # eval_dir = "/home/yuming/Desktop/temp/"

  # global parameters
  sim_end_time = 12.0
  spring_model = True

  # Create folder if not exist
  Path(eval_dir).mkdir(parents=True, exist_ok=True)

  # Get task name
  task_names = np.loadtxt(model_dir + "task_names.csv", dtype=str,
    delimiter=',')

  ### Create model iter list
  model_iter_idx_start = 1  # 1
  model_iter_idx_end = 100
  idx_spacing = 5

  model_indices = list(
    range(model_iter_idx_start - 1, model_iter_idx_end + 1, idx_spacing))
  model_indices[0] += 1
  # example list: [1, 5, 10, 15]

  # Remove some indices (remove by value)
  # model_indices.remove(56)

  print("model_indices = ")
  print(np.array(model_indices))

  ### Create task list
  stride_length = np.linspace(0, 0.3, 60)
  ground_incline = 0.0
  duration = 0.4
  turning_rate = 0.0

  # stride_length = np.array([0])

  task_list = np.zeros((len(stride_length), 4))
  task_list[:, 0] = stride_length
  task_list[:, 1] = ground_incline
  task_list[:, 2] = duration
  task_list[:, 3] = turning_rate
  print("task_list = ")
  print(task_list)

  # index of task vector where we sweep through
  varying_task_element_idx = GetVaryingTaskElementIdx(task_list)

  # Make sure the order is correct
  if not ((task_names[0] == "stride_length") &
          (task_names[1] == "ground_incline") &
          (task_names[2] == "duration") &
          (task_names[3] == "turning_rate")):
    raise ValueError("ERROR: unexpected task name or task order")

  ### Construct sample indices from the task list
  # `sample_idx` is used in two places:
  #  1. in simulation: for planner's initial guess and cost regularization term
  #  2. in cost evaluation: for gettting noimnal cost (from trajopt)
  exact_task_match = False
  sample_indices = ConstructSampleIndicesGivenModelAndTask(model_indices,
    task_list, exact_task_match)
  print("sample_indices = ")
  print(sample_indices)

  ### Toggle the functions here to run simulation or evaluate cost
  run_sim_and_eval_cost(model_indices, task_list, varying_task_element_idx,
    sample_indices)

  # Only evaluate cost
  eval_cost_in_multithread(model_indices, task_list)

  ### Plotting
  print("Nominal cost is from: " + model_dir)
  print("Simulation cost is from: " + eval_dir)

  # Save plots
  plot_cost_vs_model_and_task(model_indices, task_list,
    varying_task_element_idx, [], True, True)
  plot_cost_vs_model_and_task(model_indices, task_list,
    varying_task_element_idx, [], False, True)
  if exact_task_match:
    plot_cost_vs_model_and_task(model_indices, task_list,
      varying_task_element_idx, sample_indices, True, True)
    plot_cost_vs_model_and_task(model_indices, task_list,
      varying_task_element_idx, sample_indices, False, True)

  # 3D plot
  # plot_cost_vs_model_and_task(model_indices, task_list, varying_task_element_idx, [], True, False)
  # plot_cost_vs_model_and_task(model_indices, task_list, varying_task_element_idx, [], False, False)
  # if exact_task_match:
  #   plot_cost_vs_model_and_task(model_indices, task_list, varying_task_element_idx, sample_indices, True, False)
  #   plot_cost_vs_model_and_task(model_indices, task_list, varying_task_element_idx, sample_indices, False, False)

  # plt.show()
