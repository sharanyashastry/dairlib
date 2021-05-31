#pragma once

#include "yaml-cpp/yaml.h"
#include "drake/common/yaml/yaml_read_archive.h"

using Eigen::MatrixXd;

struct OSCRomWalkingGains {
  int rows;
  int cols;
  int rom_option;
  int model_iter;
  int sample_idx;
  double stride_length;
  double w_Q;
  double w_R;
  double w_rom_reg;
  double w_reg_quat;
  double w_reg_xy;
  double w_reg_z;
  double w_reg_joints;
  double w_reg_hip_yaw;
  double w_reg_xy_vel;
  double w_reg_vel;
  double w_predict_lipm_p;
  double w_predict_lipm_v;
  double max_speed_lipm_mpc;
  double w_p_lipm_mpc;
  double w_v_lipm_mpc;
  double left_support_duration;
  double right_support_duration;
  double double_support_duration;
  double max_foot_speed;
  double max_step_length;
  double max_desired_step_length;
  double max_lipm_step_length;
  double back_limit_wrt_pelvis;
  double front_limit_wrt_pelvis;
  double right_limit_wrt_pelvis;
  double left_limit_wrt_pelvis;
  std::string dir_model;
  std::string dir_data;
  double mu;
  double w_accel;
  double w_soft_constraint;
  std::vector<double> RomW;
  std::vector<double> RomKp;
  std::vector<double> RomKd;
  std::vector<double> PelvisHeadingW;
  std::vector<double> PelvisHeadingKp;
  std::vector<double> PelvisHeadingKd;
  std::vector<double> PelvisBalanceW;
  std::vector<double> PelvisBalanceKp;
  std::vector<double> PelvisBalanceKd;
  std::vector<double> SwingFootW;
  std::vector<double> SwingFootKp;
  std::vector<double> SwingFootKd;
  double w_swing_toe;
  double swing_toe_kp;
  double swing_toe_kd;
  double w_hip_yaw;
  double hip_yaw_kp;
  double hip_yaw_kd;
  double period_of_no_heading_control;
  double max_CoM_to_footstep_dist;
  double center_line_offset;
  double footstep_offset;
  double mid_foot_height;
  double final_foot_height;
  double final_foot_velocity_z;
  double lipm_height;
  double ss_time;
  double ds_time;
  double k_ff_lateral;
  double k_fb_lateral;
  double k_ff_sagittal;
  double k_fb_sagittal;
  double kp_pos_sagital;
  double kd_pos_sagital;
  double vel_max_sagital;
  double kp_pos_lateral;
  double kd_pos_lateral;
  double vel_max_lateral;
  double kp_yaw;
  double kd_yaw;
  double vel_max_yaw;
  double target_pos_offset;
  double global_target_position_x;
  double global_target_position_y;
  double yaw_deadband_blur;
  double yaw_deadband_radius;
  double vel_scale_rot;
  double vel_scale_trans_sagital;
  double vel_scale_trans_lateral;

  MatrixXd W_rom;
  MatrixXd K_p_rom;
  MatrixXd K_d_rom;
  MatrixXd W_pelvis_heading;
  MatrixXd K_p_pelvis_heading;
  MatrixXd K_d_pelvis_heading;
  MatrixXd W_pelvis_balance;
  MatrixXd K_p_pelvis_balance;
  MatrixXd K_d_pelvis_balance;
  MatrixXd W_swing_foot;
  MatrixXd K_p_swing_foot;
  MatrixXd K_d_swing_foot;
  MatrixXd W_swing_toe;
  MatrixXd K_p_swing_toe;
  MatrixXd K_d_swing_toe;
  MatrixXd W_hip_yaw;
  MatrixXd K_p_hip_yaw;
  MatrixXd K_d_hip_yaw;

  // IK gains
  double kp_hip_roll_stance;
  double kp_hip_yaw_stance;
  double kp_hip_pitch_stance;
  double kp_knee_stance;
  double kp_toe_stance;

  double kd_hip_roll_stance;
  double kd_hip_yaw_stance;
  double kd_hip_pitch_stance;
  double kd_knee_stance;
  double kd_toe_stance;

  double kp_hip_roll_swing;
  double kp_hip_yaw_swing;
  double kp_hip_pitch_swing;
  double kp_knee_swing;
  double kp_toe_swing;

  double kd_hip_roll_swing;
  double kd_hip_yaw_swing;
  double kd_hip_pitch_swing;
  double kd_knee_swing;
  double kd_toe_swing;

  template <typename Archive>
  void Serialize(Archive* a) {
    a->Visit(DRAKE_NVP(rows));
    a->Visit(DRAKE_NVP(cols));
    a->Visit(DRAKE_NVP(rom_option));
    a->Visit(DRAKE_NVP(model_iter));
    a->Visit(DRAKE_NVP(sample_idx));
    a->Visit(DRAKE_NVP(stride_length));
    a->Visit(DRAKE_NVP(w_Q));
    a->Visit(DRAKE_NVP(w_R));
    a->Visit(DRAKE_NVP(w_rom_reg));
    a->Visit(DRAKE_NVP(w_reg_quat));
    a->Visit(DRAKE_NVP(w_reg_xy));
    a->Visit(DRAKE_NVP(w_reg_z));
    a->Visit(DRAKE_NVP(w_reg_joints));
    a->Visit(DRAKE_NVP(w_reg_hip_yaw));
    a->Visit(DRAKE_NVP(w_reg_xy_vel));
    a->Visit(DRAKE_NVP(w_reg_vel));
    a->Visit(DRAKE_NVP(w_predict_lipm_p));
    a->Visit(DRAKE_NVP(w_predict_lipm_v));
    a->Visit(DRAKE_NVP(max_speed_lipm_mpc));
    a->Visit(DRAKE_NVP(w_p_lipm_mpc));
    a->Visit(DRAKE_NVP(w_v_lipm_mpc));
    a->Visit(DRAKE_NVP(left_support_duration));
    a->Visit(DRAKE_NVP(right_support_duration));
    a->Visit(DRAKE_NVP(double_support_duration));
    a->Visit(DRAKE_NVP(max_foot_speed));
    a->Visit(DRAKE_NVP(max_step_length));
    a->Visit(DRAKE_NVP(max_desired_step_length));
    a->Visit(DRAKE_NVP(max_lipm_step_length));
    a->Visit(DRAKE_NVP(back_limit_wrt_pelvis));
    a->Visit(DRAKE_NVP(front_limit_wrt_pelvis));
    a->Visit(DRAKE_NVP(right_limit_wrt_pelvis));
    a->Visit(DRAKE_NVP(left_limit_wrt_pelvis));
    a->Visit(DRAKE_NVP(dir_model));
    a->Visit(DRAKE_NVP(dir_data));
    a->Visit(DRAKE_NVP(mu));
    a->Visit(DRAKE_NVP(w_accel));
    a->Visit(DRAKE_NVP(w_soft_constraint));
    a->Visit(DRAKE_NVP(RomW));
    a->Visit(DRAKE_NVP(RomKp));
    a->Visit(DRAKE_NVP(RomKd));
    a->Visit(DRAKE_NVP(PelvisHeadingW));
    a->Visit(DRAKE_NVP(PelvisHeadingKp));
    a->Visit(DRAKE_NVP(PelvisHeadingKd));
    a->Visit(DRAKE_NVP(PelvisBalanceW));
    a->Visit(DRAKE_NVP(PelvisBalanceKp));
    a->Visit(DRAKE_NVP(PelvisBalanceKd));
    a->Visit(DRAKE_NVP(SwingFootW));
    a->Visit(DRAKE_NVP(SwingFootKp));
    a->Visit(DRAKE_NVP(SwingFootKd));
    a->Visit(DRAKE_NVP(w_swing_toe));
    a->Visit(DRAKE_NVP(swing_toe_kp));
    a->Visit(DRAKE_NVP(swing_toe_kd));
    a->Visit(DRAKE_NVP(w_hip_yaw));
    a->Visit(DRAKE_NVP(hip_yaw_kp));
    a->Visit(DRAKE_NVP(hip_yaw_kd));
    a->Visit(DRAKE_NVP(period_of_no_heading_control));
    // swing foot heuristics
    a->Visit(DRAKE_NVP(max_CoM_to_footstep_dist));
    a->Visit(DRAKE_NVP(center_line_offset));
    a->Visit(DRAKE_NVP(footstep_offset));
    a->Visit(DRAKE_NVP(mid_foot_height));
    a->Visit(DRAKE_NVP(final_foot_height));
    a->Visit(DRAKE_NVP(final_foot_velocity_z));
    // lipm heursitics
    a->Visit(DRAKE_NVP(lipm_height));
    // stance times
    a->Visit(DRAKE_NVP(ss_time));
    a->Visit(DRAKE_NVP(ds_time));
    // Speed control gains
    a->Visit(DRAKE_NVP(k_ff_lateral));
    a->Visit(DRAKE_NVP(k_fb_lateral));
    a->Visit(DRAKE_NVP(k_ff_sagittal));
    a->Visit(DRAKE_NVP(k_fb_sagittal));
    // High level command gains (without radio)
    a->Visit(DRAKE_NVP(kp_pos_sagital));
    a->Visit(DRAKE_NVP(kd_pos_sagital));
    a->Visit(DRAKE_NVP(vel_max_sagital));
    a->Visit(DRAKE_NVP(kp_pos_lateral));
    a->Visit(DRAKE_NVP(kd_pos_lateral));
    a->Visit(DRAKE_NVP(vel_max_lateral));
    a->Visit(DRAKE_NVP(kp_yaw));
    a->Visit(DRAKE_NVP(kd_yaw));
    a->Visit(DRAKE_NVP(vel_max_yaw));
    a->Visit(DRAKE_NVP(target_pos_offset));
    a->Visit(DRAKE_NVP(global_target_position_x));
    a->Visit(DRAKE_NVP(global_target_position_y));
    a->Visit(DRAKE_NVP(yaw_deadband_blur));
    a->Visit(DRAKE_NVP(yaw_deadband_radius));
    // High level command gains (with radio)
    a->Visit(DRAKE_NVP(vel_scale_rot));
    a->Visit(DRAKE_NVP(vel_scale_trans_sagital));
    a->Visit(DRAKE_NVP(vel_scale_trans_lateral));
    // IK gains
    a->Visit(DRAKE_NVP(kp_hip_roll_stance));
    a->Visit(DRAKE_NVP(kp_hip_yaw_stance));
    a->Visit(DRAKE_NVP(kp_hip_pitch_stance));
    a->Visit(DRAKE_NVP(kp_knee_stance));
    a->Visit(DRAKE_NVP(kp_toe_stance));
    a->Visit(DRAKE_NVP(kd_hip_roll_stance));
    a->Visit(DRAKE_NVP(kd_hip_yaw_stance));
    a->Visit(DRAKE_NVP(kd_hip_pitch_stance));
    a->Visit(DRAKE_NVP(kd_knee_stance));
    a->Visit(DRAKE_NVP(kd_toe_stance));
    a->Visit(DRAKE_NVP(kp_hip_roll_swing));
    a->Visit(DRAKE_NVP(kp_hip_yaw_swing));
    a->Visit(DRAKE_NVP(kp_hip_pitch_swing));
    a->Visit(DRAKE_NVP(kp_knee_swing));
    a->Visit(DRAKE_NVP(kp_toe_swing));
    a->Visit(DRAKE_NVP(kd_hip_roll_swing));
    a->Visit(DRAKE_NVP(kd_hip_yaw_swing));
    a->Visit(DRAKE_NVP(kd_hip_pitch_swing));
    a->Visit(DRAKE_NVP(kd_knee_swing));
    a->Visit(DRAKE_NVP(kd_toe_swing));

    W_rom = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->RomW.data(), this->rows, this->cols);
    K_p_rom = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->RomKp.data(), this->rows, this->cols);
    K_d_rom = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->RomKd.data(), this->rows, this->cols);
    W_pelvis_heading = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->PelvisHeadingW.data(), this->rows, this->cols);
    K_p_pelvis_heading = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->PelvisHeadingKp.data(), this->rows, this->cols);
    K_d_pelvis_heading = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->PelvisHeadingKd.data(), this->rows, this->cols);
    W_pelvis_balance = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->PelvisBalanceW.data(), this->rows, this->cols);
    K_p_pelvis_balance = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->PelvisBalanceKp.data(), this->rows, this->cols);
    K_d_pelvis_balance = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->PelvisBalanceKd.data(), this->rows, this->cols);
    W_swing_foot = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->SwingFootW.data(), this->rows, this->cols);
    K_p_swing_foot = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->SwingFootKp.data(), this->rows, this->cols);
    K_d_swing_foot = Eigen::Map<
        Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
        this->SwingFootKd.data(), this->rows, this->cols);
    W_swing_toe = this->w_swing_toe * MatrixXd::Identity(1, 1);
    K_p_swing_toe = this->swing_toe_kp * MatrixXd::Identity(1, 1);
    K_d_swing_toe = this->swing_toe_kd * MatrixXd::Identity(1, 1);
    W_hip_yaw = this->w_hip_yaw * MatrixXd::Identity(1, 1);
    K_p_hip_yaw = this->hip_yaw_kp * MatrixXd::Identity(1, 1);
    K_d_hip_yaw = this->hip_yaw_kd * MatrixXd::Identity(1, 1);
  }
};