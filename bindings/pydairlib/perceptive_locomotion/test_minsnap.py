import time
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

from pydairlib.common.plot_styler import PlotStyler
from pydairlib.common.plot_shapes import Cuboid
from pydairlib.perceptive_locomotion.controllers import \
    MakeMinSnapTrajFromWaypoints

from pydrake.all import PiecewisePolynomial


def plot_traj_time(traj, npoints, deriv=0):

    t = np.linspace(traj.start_time(), traj.end_time(), npoints)
    y = np.zeros((t.size, traj.rows()))

    for i in range(t.size):
        y[i] = traj.EvalDerivative(t[i], derivative_order=deriv).ravel()

    plt.plot(t, y)


def plot_space_traj(traj, npoints):
    t = np.linspace(traj.start_time(), traj.end_time(), npoints)
    y = np.zeros((t.size, traj.rows()))

    for i in range(t.size):
        y[i] = traj.value(t[i]).ravel()

    ax = plt.axes(projection='3d')
    ax.plot3D(y[:, 0], y[:, 1], y[:, 2])
    return ax


def add_cuboid_step(c0, c1, ax):
    box = np.abs(c1 - c0)
    center = np.minimum(c0, c1)
    cuboid = Cuboid(ax, box[0], box[1], box[2])
    cuboid.transform(center)


def minsnap_around_box(p0, p1, t, h):
    assert(h > 0)
    breaks = np.array([0, 0.5, 1.0])
    wp = np.zeros((breaks.size, 3))

    tadj = 0.3

    wp[0] = p0
    wp[-1] = p1

    hdiff = p1[2] - p0[2]

    if hdiff > (h / 4):
        # Middle waypoint for step up
        wp[1] = p0
        wp[1, 2] = p1[2] + h/2
        breaks[1] = tadj
    elif -hdiff > (h / 4):
        wp[1] = p1
        wp[1, 2] = p0[2] + h/2
        breaks[1] = 1.0 - tadj
    else:
        wp[1] = 0.5 * (p0 + p1)
        wp[1, 2] += h

    breaks *= t
    s = time.time()
    traj = MakeMinSnapTrajFromWaypoints(wp.T, breaks.tolist(), 0.5)
    e = time.time()
    print(f't = {e - s}')
    plot_traj_time(traj, 100)
    plt.figure()
    ax3d = plot_space_traj(traj, 100)
    add_cuboid_step(p0, p1, ax3d)
    plt.show()


def test():
    minsnap_around_box(np.zeros((3,)), np.array([-0.1, -0.05, -0.1]), 0.3, 0.10)


if __name__ == "__main__":
    test()