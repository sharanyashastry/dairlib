import sys
import matplotlib.pyplot as plt
from pydairlib.lcm import lcm_trajectory
from pydairlib.common import FindResourceOrThrow
from pydrake.trajectories import PiecewisePolynomial
import numpy as np
from pydairlib.common import plot_styler

def main():
  global ps

  figure_directory = '/home/yangwill/Documents/research/projects/'
  ps = plot_styler.PlotStyler()
  ps.set_default_styling(directory=figure_directory)
  # Default filename for the example
  # filename = FindResourceOrThrow("examples/Cassie/saved_trajectories/walking_0.16.0")
  # filename = FindResourceOrThrow("examples/Cassie/saved_trajectories/jumping_0.5h_0.3d")
  # filename = FindResourceOrThrow("examples/Cassie/saved_trajectories/jumping_box_0.5h_0.3d_2")
  filename = FindResourceOrThrow("examples/Cassie/saved_trajectories/jumping_box_0.4h_0.3d_3")
  # filename = "/home/yangwill/Documents/research/projects/cassie/hardware/backup/dair/saved_trajectories/jumping_0.15h_0.3d"

  # filename = FindResourceOrThrow("examples/Cassie/saved_trajectories/" + sys.argv[1])
  dircon_traj = lcm_trajectory.DirconTrajectory(filename)

  # Reconstructing state and input trajectory as piecewise polynomials
  state_traj = dircon_traj.ReconstructStateTrajectory()
  input_traj = dircon_traj.ReconstructInputTrajectory()
  state_datatypes = dircon_traj.GetTrajectory("state_traj0").datatypes
  input_datatypes = dircon_traj.GetTrajectory("input_traj").datatypes
  force_samples = dircon_traj.GetTrajectory("force_vars0").datapoints
  force_t_samples = dircon_traj.GetStateBreaks(0)
  force_traj = dircon_traj.ReconstructLambdaTrajectory()
  force_datatypes = dircon_traj.GetTrajectory("force_vars0").datatypes
  # force_datatypes = dircon_traj.GetTrajectory("force_vars1").datatypes

  collocation_force_points = dircon_traj.GetCollocationForceSamples(0)

  n_points = 500
  t = np.linspace(state_traj.start_time(), state_traj.end_time(), n_points)
  state_samples = np.zeros((n_points, state_traj.value(0).shape[0]))
  input_samples = np.zeros((n_points, input_traj.value(0).shape[0]))
  force_samples = np.zeros((n_points, force_traj[2].value(0).shape[0]))
  for i in range(n_points):
    state_samples[i] = state_traj.value(t[i])[:, 0]
    input_samples[i] = input_traj.value(t[i])[:, 0]
    force_samples[i] = force_traj[2].value(t[i])[:, 0]


  pos_indices = slice(0 + 7, 11)
  vel_indices = slice(23 + 6, 45)
  # floating base states
  pos_indices = slice(0, 7)
  vel_indices = slice(23, 23 + 6)
  # vel_indices = slice(23 + 6, 23 + 10)
  # all motor torques
  u_indices = slice(0, 10)

  # reflected_state_samples = state_samples @ M
  # Plotting reconstructed state trajectories
  plt.figure("state trajectory")
  ps.plot(t, state_samples[:, pos_indices])
  # plt.plot(t + state_traj.end_time(), reflected_state_samples[:, 0:7])
  # plt.plot(t, state_samples[:, -18:])
  # plt.plot(t + state_traj.end_time(), reflected_state_samples[:, 7:13])
  # plt.plot(t, state_samples[:, 25:31])
  # plt.plot(t + state_traj.end_time(), reflected_state_samples[:, 25:31])
  # plt.legend(state_datatypes[0:7])
  plt.legend(state_datatypes[pos_indices])

  plt.figure("input trajectory")
  ps.plot(t, input_samples[:, u_indices])
  plt.legend(input_datatypes[u_indices])

  plt.figure("force trajectory")
  # ps.plot(t, force_samples[:, :12])
  ps.plot(t, force_samples)
  plt.legend(force_datatypes)

  plt.show()

def reflected_joints():

  mirror = np.zeros((37, 37))
  mirror[0:7, 0:7] = np.eye(7)
  mirror[19:25, 19:25] = np.eye(6)
  joint_slice = range(7, 19, 2)
  joint_vel_slice = range(19 + 6, 19 + 18, 2)
  asy_indices = {7, 9, 25, 27}
  mirror[1, 1] = -1
  mirror[3, 3] = -1
  mirror[5, 5] = -1

  mirror[19, 19] = -1
  mirror[21, 21] = -1
  mirror[23, 23] = -1
  for i in joint_slice:
    if(i in asy_indices):
      mirror[i,i+1] = -1
      mirror[i+1,i] = -1
    else:
      mirror[i,i+1] = 1
      mirror[i+1,i] = 1
  for i in joint_vel_slice:
    if(i in asy_indices):
      mirror[i,i+1] = -1
      mirror[i+1,i] = -1
    else:
      mirror[i,i+1] = 1
      mirror[i+1,i] = 1
  return mirror

if __name__ == "__main__":
  main()


