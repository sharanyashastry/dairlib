import numpy as np
import yaml
from swing_ft_env import CassieSwingFootEnv
from sklearn.gaussian_process import GaussianProcessRegressor
from sklearn.gaussian_process.kernels import RBF
import matplotlib.pyplot as plt
import argparse


def get_default_params(
        gains_file="examples/Cassie/osc/osc_walking_gains_alip.yaml"):
    with open(gains_file, 'r') as f:
        gains = yaml.safe_load(f)
    
    n_knot = 5
    knots = []
    for i in range(n_knot):
        t = i/(n_knot - 1)
        x = [0.5 * (np.sin(np.pi * (t - 0.5)) + 1),
             0.5 * (np.sin(np.pi * (t - 0.5)) + 1),
             (gains["mid_foot_height"] / 0.58) * np.cos(np.pi * (t - 0.5))*t]
        knots += x
    vel_initial = [0, 0, 0]
    vel_final = [0, 0, gains["final_foot_velocity_z"]]
    return [n_knot] + knots + vel_initial + vel_final


class RandomExplorer:
    def __init__(self, nominal_swing, sigmas):
        """
        param nominal_swing: 1 + 3 * n_knots + 3 + 3 array
        param sigmas: 3 * n_knots + 3 + 3 array
        """
        self.nominal_swing = nominal_swing
        if len(sigmas) == 3 * nominal_swing[0] + 6:
            self.sigmas = [0] + sigmas
        elif len(sigmas) == 3 * nominal_swing[0] + 7:
            self.sigmas = sigmas
        else:
            print(f"sigmas must be of the correct length! "
                  f"is currently {len(sigmas)}")
            print(f"needs to be "
                  f"{3 * nominal_swing[0] + 6} or {3 * nominal_swing[0] + 7}")
            self.sigmas = [0] * (3 * nominal_swing[0] + 7)
        
    def select_action(self):
        """ Outputs a random deviation from the nominal
        (excluding the n_knots).
        """
        deviation = np.random.normal(
            np.zeros(1 + 3 * self.nominal_swing[0] + 6), self.sigmas)
        return self.nominal_swing + deviation
         
    def collect_data(self, n_steps, cassie_env,
                     num_steps_same_radio=0,
                     num_steps_same_params=1):
        """ Runs the random explorer for n_steps steps,
        gathering n_steps (s, a, r, s') tuples 
        """
        data = []
        done = False
        s = cassie_env.reset()
        radio_change_counter = 0
        params_change_counter = 0
        radio = [np.random.uniform(low = -1, high = 1), 0, 0, 0]
        a = self.select_action()
        for _ in range(n_steps):
            if params_change_counter > num_steps_same_params:
                params_change_counter = 0
                a = self.select_action()

            if num_steps_same_radio > 0:
                s_prime, r, d = cassie_env.step(a, radio)
            else:
                s_prime, r, d = cassie_env.step(a)

            print(f"finished step and got reward {r}")
            data.append([s, a, r, s_prime, radio])
            s = s_prime
            done = d
            if done or s[4] > 5:
                cassie_env.reset()
            radio_change_counter += 1
            params_change_counter += 1
            if radio_change_counter > num_steps_same_radio:
                radio_change_counter = 0
                radio = [np.random.uniform(low = -1, high = 1), 0, 0, 0]
        return data

def calc_new_params(gpr, nominal_swing, radio):
    return

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fit_gp", type=bool, default=False)
    args = parser.parse_args()
    nominal_swing = get_default_params()
    if not args.fit_gp:
        sigmas = np.ones(len(nominal_swing)) * 0.02
        sigmas[0] = 0
        explorer = RandomExplorer(nominal_swing, sigmas)
        cassie_env = CassieSwingFootEnv(nominal_swing, use_radio=False)
        data = explorer.collect_data(200, cassie_env, num_steps_same_params = 1)
        np.save("swing_foot_reward_data.npy", data)

        cassie_env.kill_procs()
    else:
        data = np.load("swing_foot_reward_data.npy", allow_pickle=True)

    # Use this data to learn the reward function
    # Datapoints are the swing foot params + radio commands
    # for now, just use the first knot point
    # X = np.array([list(d[1]) + list(d[4]) for d in data])

    # as a test, just look at the final swing ft z velocity, filtering out huge negative rewards
    cond = lambda x: x > -0.3
    idx = 2
    X = np.array([d[1][idx] for d in data if cond(d[2])]).reshape(-1, 1)
    y = 1e2 * np.array([d[2] for d in data if cond(d[2])]).reshape(-1, 1)

    kernel = RBF(1.0)  # this is a hyperparameter to tune

    # there's also a alpha hyperparameter here
    gpr = GaussianProcessRegressor(kernel).fit(X, y)  
    
    # plot the fit
    x = np.arange(nominal_swing[idx]-0.02, nominal_swing[idx]+0.02, 0.0001).reshape(-1, 1)
    y_, std = gpr.predict(x, return_std=True)

    ax = plt.figure().gca()
    ax.set_xlim([min(x), max(x)])
    ax.scatter(X, y, s=5)
    ax.scatter([nominal_swing[idx]], [0], s=20, color="green")
    ax.plot(x, y_, color="orange")
    ax.fill_between(x.ravel(), y_.ravel()-std, y_.ravel()+std, alpha=0.2,
                    color="orange")
    plt.show()
    

if __name__ == "__main__":
    main()