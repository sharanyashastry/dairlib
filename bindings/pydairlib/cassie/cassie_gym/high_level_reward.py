import numpy as np
from pydairlib.cassie.cassie_gym.reward_osudrl import RewardOSUDRL
import copy

class HighLevelReward(RewardOSUDRL):
    def __init__(self, fwd_vel_des):
        super().__init__()
        self.fwd_vel_des = fwd_vel_des
        self.gain = 0.2

    def compute_reward(self, timestep, cassie_env_state, prev_cassie_env_state, swing_foot_error=None):
        low_level_reward = super().compute_reward(timestep, cassie_env_state, prev_cassie_env_state,
                                                  swing_foot_error)
        # should I try with some sort of reward for reaching the goal?
        # low_level_reward = 0  # try without OSU reward, only high level reward
        high_level_reward = -self.gain * (self.fwd_vel_des - cassie_env_state.get_velocities()[0])**2
        survive_reward = 0.5
        return low_level_reward + high_level_reward + survive_reward
