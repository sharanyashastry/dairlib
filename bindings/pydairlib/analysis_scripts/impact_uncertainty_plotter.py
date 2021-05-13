import sys

import lcm
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import scipy.linalg as linalg
from scipy import interpolate

import pathlib
from pydrake.multibody.parsing import Parser
from pydrake.multibody.plant import AddMultibodyPlantSceneGraph
from pydrake.multibody.tree import JacobianWrtVariable
from pydrake.systems.framework import DiagramBuilder
from pydrake.trajectories import PiecewisePolynomial
from pydairlib.lcm import lcm_trajectory
from pydairlib.lcm import process_lcm_log
import pydairlib.multibody
from impact_invariant_scripts import plot_ii_projection
from pydairlib.cassie.cassie_utils import *

from pydairlib.common import FindResourceOrThrow
from pydairlib.common import plot_styler



def main():
  global ps
  figure_directory = '/home/yangwill/Documents/research/projects/impact_uncertainty/data/'
  ps = plot_styler.PlotStyler()
  ps.set_default_styling(figure_directory)
  plot_error_bands()


def plot_error_bands():

  data_range = np.arange(28, 35, 1)
  # data_range = np.concatenate((np.arange(12, 17, 1), data_range))
  # v_all = np.empty()
  v_all = []
  vproj_all = []
  n_samples = 10000
  joint_idx = 6

  t_master = np.load(ps.directory + 't_28.npy')[:n_samples]
  for i in data_range:
    t = np.load(ps.directory + 't_' + '%.2d.npy' % i)
    v = np.load(ps.directory + 'v_' + '%.2d.npy' % i)
    vproj = np.load(ps.directory + 'vproj_' + '%.2d.npy' % i)
    v_interp = interpolate.interp1d(t, v, axis=0, bounds_error=False)
    vproj_interp = interpolate.interp1d(t, vproj, axis=0, bounds_error=False)
    # v_all.append(v[:n_samples, :])
    # vproj_all.append(vproj[:n_samples, :])
    v_all.append(v_interp(t_master))
    vproj_all.append(vproj_interp(t_master))
    # ps.plot(t, v[:n_samples, joint_idx], color='b')
    # ps.plot(t, vproj[:n_samples, joint_idx], color='r')
  plt.xlim([-10, 30])
  v_all = np.stack(v_all, axis=-1)
  vproj_all = np.stack(vproj_all, axis=-1)


  v_std = np.std(v_all, axis=2)
  v_mean = np.mean(v_all, axis=2)
  vproj_std = np.std(vproj_all, axis=2)
  vproj_mean = np.mean(vproj_all, axis=2)
  plt.figure('joint velocities')
  for i in range(12):
    ps.plot(t_master, v_mean[:, i], color=ps.cmap(i))
    ps.plot_bands(t_master, t_master, (v_mean - v_std)[:, i], (v_mean + v_std)[:, i], color=ps.cmap(i))
  plt.xlim([-10, 50])
  plt.ylim([-10, 10])
  ps.save_fig('joint_velocities_0.25dev.png')
  plt.figure('projected joint velocities')
  for i in range(12):
    ps.plot(t_master, vproj_mean[:, i], color=ps.cmap(i))
    ps.plot_bands(t_master, t_master, (vproj_mean - vproj_std)[:, i], (vproj_mean + vproj_std)[:, i], color=ps.cmap(i))
  plt.xlim([-10, 50])
  plt.ylim([-5, 5])
  ps.save_fig('projected_joint_velocities_0.25dev.png')

  plt.show()

if __name__ == '__main__':
    main()