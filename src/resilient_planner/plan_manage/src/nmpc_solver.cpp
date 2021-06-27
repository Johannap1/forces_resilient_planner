#include <plan_manage/nmpc_utils.h>

constexpr double PI = 3.1415926;

namespace resilient_planner
{

  NMPCSolver::NMPCSolver()
  {
    //construct model matrices
    At_.setZero();
    Bt_.setZero();
    Dt_.setZero();

    // some pre initializations of the matrix
    At_(0, 3) = 1.0;
    At_(1, 4) = 1.0;
    At_(2, 5) = 1.0;

    Bt_(6, 0) = 1.0;
    Bt_(7, 1) = 1.0;
    Bt_(8, 2) = 1.0;

    Dt_(3, 0) = 1.0;
    Dt_(4, 1) = 1.0;
    Dt_(5, 2) = 1.0;

    Kt_ << -2.0, 5.0, 0.0, -1.0, 4.0, 0.0, -8.0, 0.0, 0.0,
        -5.0, -2.0, 0.0, -4.0, -1.0, 0.0, 0.0, -8.0, 0.0,
        -2.0, -2.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.0, -8.0,
        0.0, 0.0, -8.0, 0.0, 0.0, -6.0, 0.0, 0.0, 0.0;

    external_acc_.setZero();
    end_v_.setZero();
    w_ << ext_noise_bound_, ext_noise_bound_, ext_noise_bound_;

    cmd_status_ = CMD_STATUS::INIT_POSITION;
  }

  void NMPCSolver::initROS(ros::NodeHandle &nh, tgk_planner::OccMap::Ptr &env_ptr_)
  {

    /*  kino a* intial  */
    kino_path_finder_.reset(new resilient_planner::KinodynamicAstar);
    kino_path_finder_->setParam(nh);
    kino_path_finder_->init();
    kino_path_finder_->intialGridMap(env_ptr_);

    ellipsoid_pub_ = nh.advertise<decomp_ros_msgs::EllipsoidArray>("ellipsoid_array", 1, true);
    poly_pub_ = nh.advertise<decomp_ros_msgs::PolyhedronArray>("polyhedron_array", 1, true);
    ref_marker_pub_ = nh.advertise<visualization_msgs::Marker>("ref_traj", 1, true);
    nmpc_marker_pub_ = nh.advertise<visualization_msgs::Marker>("nmpc_traj", 1, true);
    cmd_vis_pub_ = nh.advertise<visualization_msgs::Marker>("position_cmd_vis", 10, true);
    cloud_sub_ = nh.subscribe("/occ_map/local_view_cloud", 1, &NMPCSolver::cloudCallback, this);

    double ego_r, ego_h, drag_coeff;


    /* weight */
    double w_stage_wp , w_stage_input, w_terminal_wp, w_terminal_input ,
          w_input_rate , w_final_terminal_wp , w_final_terminal_input, 
          w_final_stage_wp , w_final_stage_input;



    nh.param("nmpc/w_stage_wp", w_stage_wp, 15.0);
    nh.param("nmpc/w_stage_input", w_stage_input, 3.0);
    nh.param("nmpc/w_terminal_wp", w_terminal_wp, 15.0);
    nh.param("nmpc/w_terminal_input", w_terminal_input, 0.0);
    nh.param("nmpc/w_input_rate", w_input_rate, 80.0);
    nh.param("nmpc/w_final_stage_wp", w_final_stage_wp, 20.0);
    nh.param("nmpc/w_final_stage_input", w_final_stage_input, 5.0);
    nh.param("nmpc/w_final_terminal_wp", w_final_terminal_wp, 20.0);
    nh.param("nmpc/w_final_terminal_input", w_final_terminal_input, 5.0);
    nh.param("nmpc/ego_r", ego_r, 0.27);
    nh.param("nmpc/ego_h", ego_h, 0.0425);
    nh.param("nmpc/g_acc", g, 9.81);
    nh.param("nmpc/mass", mass, 0.74);
    nh.param("nmpc/drag_coefficient", drag_coeff, 0.33);

    nh.param("search/max_tau", max_tau_, 0.5);

    ROS_INFO_STREAM("ego_r_: " << ego_r);
    ROS_INFO_STREAM("ego_h_: " << ego_h);
    ROS_INFO_STREAM("max_tau: " << max_tau_);
    ROS_INFO_STREAM("mass: " << mass);

    // use rotors simulator
    cmd_timer_ = nh.createTimer(ros::Duration(0.01), &NMPCSolver::cmdTrajCallback, this); // 5ms
    traj_cmd_pub_ = nh.advertise<trajectory_msgs::MultiDOFJointTrajectory>("/pos_cmd", 50);

    // ego info
    ego_size_ << ego_r * ego_r, 0.0, 0.0,
        0.0, ego_r * ego_r, 0.0,
        0.0, 0.0, ego_h * ego_h;

    //drag_coefficient_matrix
    drag_coefficient_matrix_ << drag_coeff, 0.0, 0.0,
        0.0, drag_coeff, 0.0,
        0.0, 0.0, 0.0;



    nmpc_forces_solver_normal_.setParasNormal(w_stage_wp,  w_stage_input, w_input_rate,
                                              w_terminal_wp, w_terminal_input);
    nmpc_forces_solver_final_.setParasFinal(w_final_stage_wp,  w_final_stage_input, w_input_rate,
                                              w_final_terminal_wp, w_final_terminal_input);




    ROS_INFO_STREAM("[NMPCSolver]Finish the intialization of the nmpc solver! \n");
  }

  void NMPCSolver::getCurTraj(int index)
  {
    double index_time = index * Ts_ + (mpc_start_time_ - kino_start_time_).toSec();
    unsigned int kino_index = (int)(index_time / Ts_);

    // decide the reference point
    if (kino_index + 1 < kino_size_)
    {
      ref_pos_ = kino_path_[kino_index] + fmod(index_time, Ts_) / Ts_ * (kino_path_[kino_index + 1] - kino_path_[kino_index]);
    }
    else
    {
      ref_pos_ = kino_path_[kino_size_ - 1];
    }

    // decide the forward point
    if (kino_index + 8 < kino_size_)
    {
      forward_pos_ = kino_path_[kino_index + 8];
    }
    else
    {
      forward_pos_ = kino_path_[kino_size_ - 1];
    }

    calculate_yaw(ref_pos_, forward_pos_);

    if (index == 0 && (ref_pos_ - mpc_output_.at(1).segment(8, 3)).norm() > 1.0)
    {
      ROS_INFO_STREAM("Hard to follow the reference ! replan !\n");
      kino_replan_ = true;
    }
  }

  // use kinodynamic a* to generate a path
  bool NMPCSolver::getKinoPath(Eigen::VectorXd &stateOdom, Eigen::Vector3d end_pt,
                               Eigen::Vector3d external_acc, bool replan)
  {
    end_pt_ = end_pt;
    external_acc_ = external_acc;

    start_pt_ = stateOdom.segment(0, 3);
    start_v_ = stateOdom.segment(3, 3);
    start_a_.setZero();

    kino_path_finder_->reset();
    kino_path_finder_->updateExternalAcc(external_acc_);

    int status;

    if (replan && exit_code == 1)
    { // need replan and mpc success

      double t_cur = (ros::Time::now() - pre_mpc_start_time_).toSec();
      int cur_index = (int)(t_cur / Ts_);
      Eigen::VectorXd mpcq;

      if (cur_index < planning_horizon_ - 1 && t_cur >= 0.0)
      {
        mpcq = pre_mpc_output_.at(cur_index) + fmod(t_cur, Ts_) / Ts_ * (pre_mpc_output_.at(cur_index + 1) - pre_mpc_output_.at(cur_index));

        Eigen::Vector3d temp_start_pt, temp_start_v, temp_start_a;

        temp_start_pt << mpcq(8), mpcq(9), mpcq(10);
        temp_start_v << mpcq(11), mpcq(12), mpcq(13);

        Eigen::Vector3d odom_euler(mpcq(14), mpcq(15), mpcq(16));
        Eigen::Vector3d thrust_b(0.0, 0.0, mpcq(3));
        Eigen::Vector3d thrustn_w = eulerToRot(odom_euler) * thrust_b / mass;

        temp_start_a << thrustn_w(0), thrustn_w(1), thrustn_w(2) - g;
        status = kino_path_finder_->search(temp_start_pt, temp_start_v, temp_start_a, end_pt_, end_v_, true);
      }
      else
      {
        status = kino_path_finder_->search(start_pt_, start_v_, start_a_, end_pt_, end_v_, true);
      }
    }
    else
    {
      status = kino_path_finder_->search(start_pt_, start_v_, start_a_, end_pt_, end_v_, true);
    }

    if (status == resilient_planner::KinodynamicAstar::NO_PATH)
    {
      std::cout << "[kino replan]: kinodynamic search fail!" << std::endl;

      // retry searching with discontinuous initial state
      kino_path_finder_->reset();
      status = kino_path_finder_->search(start_pt_, start_v_, start_a_, end_pt, end_v_, false);
      if (status == resilient_planner::KinodynamicAstar::NO_PATH)
      {
        std::cout << "[kino replan]: Can't find path." << std::endl;
        return false;
      }
      else
      {
        std::cout << "[kino replan]: retry search success." << std::endl;
      }
    }
    else
    {
      std::cout << "[kino replan]: kinodynamic search success." << std::endl;
    }

    kino_path_ = kino_path_finder_->getKinoTraj(Ts_);
    kino_size_ = kino_path_.size();

    switch_to_final = false;
    kino_start_time_ = ros::Time::now();
    ROS_INFO_STREAM("kino_start_time_ : \n"
                    << kino_start_time_);
    cmd_status_ = CMD_STATUS::PUB_TRAJ;
    pub_end_ = false;

    return true;
  }

  void NMPCSolver::callInitYaw(Eigen::VectorXd odom, double init_yaw)
  {

    ROS_INFO_STREAM("NMPCSolver::callInitYaw");
    realOdom_ = odom;
    init_yaw_ = init_yaw;
    pub_end_ = false;
    change_yaw_time_ = ros::Time::now();

    init_yaw_dot_ = init_yaw_ - realOdom_(8);

    if (init_yaw_dot_ > PI)
    {
      init_yaw_dot_ = 2 * PI - init_yaw_dot_;
    }
    else if (init_yaw_dot_ < -PI)
    {
      init_yaw_dot_ = init_yaw_dot_ + 2 * PI;
    }

    double max_yaw_dot = 0.4 * PI;

    if (init_yaw_dot_ > max_yaw_dot)
    {
      init_yaw_dot_ = max_yaw_dot;
    }
    else if (init_yaw_dot_ < -max_yaw_dot)
    {
      init_yaw_dot_ = -max_yaw_dot;
    }

    ROS_INFO_STREAM("init_yaw_dot_ : \n"
                    << init_yaw_dot_);
    cmd_status_ = CMD_STATUS::ROTATE_YAW;
  }

  void NMPCSolver::callEmergencyStop(Eigen::VectorXd odom)
  {
    realOdom_ = odom;
    cmd_status_ = CMD_STATUS::EMERGENCY_STOP;
  }


  void NMPCSolver::initMPCOutput()
  {
    mpc_output_.clear();

    Eigen::VectorXd mpc_row(var);
    // control input is
    // state is: position, velocity and euler angle
    mpc_row <<  0.0, 0.0, 0.0, real_thrust_c_,
                0.0, 0.0, 0.0, real_thrust_c_,
                stateMpc_(0), stateMpc_(1), stateMpc_(2), // position
                stateMpc_(3), stateMpc_(4), stateMpc_(5), // velocity
                stateMpc_(6), stateMpc_(7), stateMpc_(8); // euler angle

    for (int i = 0; i < planning_horizon_ + 1; i++)
    {
      mpc_output_.push_back(mpc_row);
    }

    initialized_output_ = true;
    pre_mpc_output_ = mpc_output_;
    ROS_INFO_STREAM("[NMPCSolver::initMPCOutput] Finish the intialization of mpc output! ");
  }

  int NMPCSolver::getSikangConst(Eigen::Matrix3d E)
  {

    Eigen::Vector3d seed_point = ref_pos_.head(3);
    int index = poly_constraints_.size();
    if (index >= 1)
    {
      //directly check the reference ellipsoid shape within the polytope
      auto temp_A = (poly_constraints_.back()).A();
      auto temp_b = (poly_constraints_.back()).b();
      bool flag = true;

      for (unsigned int j = 0; j < temp_b.size(); j++)
      {
        //A << temp_A(j, 0), temp_A(j, 1), temp_A(j, 2);
        Eigen::MatrixXd temp_b_addition = E * (temp_A.row(j)).transpose();

        if (temp_A.row(j) * seed_point - (temp_b(j) - 1.05 * temp_b_addition.norm()) > 0)
        {
          flag = false;
          break;
        }
      }

      if (flag) // the point in the polytope
      {
        return index - 1; // same as last time poly
      }
    }
    //ROS_INFO_STREAM("[NMPCSolver::getSikangConst]: generate new const");

    vec_Vec3f seed_path;
    seed_path.push_back(seed_point);

    Vec3f seed_point2;
    seed_point2 << seed_point[0] + 0.1, seed_point[1] + 0.1, seed_point[2];
    seed_path.push_back(seed_point2);

    EllipsoidDecomp3D decomp_util;
    decomp_util.set_obs(vec_obs_);
    decomp_util.set_local_bbox(Vec3f(2, 2, 1));
    decomp_util.dilate(seed_path);
    auto seed_poly = decomp_util.get_polyhedrons();

    vec_E<LinearConstraint3D> css = decomp_util.get_constraints();
    polys.push_back(seed_poly[0]);
    poly_constraints_.push_back(css[0]);

    return index;
  }

  void NMPCSolver::displayPoly()
  {
    decomp_ros_msgs::PolyhedronArray poly_msg = DecompROS::polyhedron_array_to_ros(polys);
    poly_msg.header.frame_id = "world";
    poly_pub_.publish(poly_msg);
  }

  void NMPCSolver::getKinoTraj(std::vector<Eigen::Vector3d> &kino_path)
  {
    kino_path = kino_path_;
  }

  int NMPCSolver::solveNMPC(Eigen::VectorXd &stateMpc, Eigen::Vector3d external_acc)
  {
    external_acc_ = external_acc;

    if (cmd_status_ == CMD_STATUS::WAIT)
    {
      return 0;
    }
    else if (pub_end_)
    {
      return 2;
    }

    mpc_start_time_ = ros::Time::now();
    pre_mpc_start_time_ = mpc_start_time_;
    stateMpc_ = stateMpc;

    // update the mpc output
    if (!initialized_output_ || exit_code != 1)
    {
      initMPCOutput();
    }

    // for visualization
    poly_constraints_.clear();
    polys.clear();
    ellipsoid_matrices_.clear();
    ref_total_pos_.clear();
    ref_total_yaw_.clear();

    /*** intialization !!!  ***/
    clock_t start, finish1, finish2;
    double totalTime = 0;
    start = clock();

    Eigen::VectorXd poly_indices(planning_horizon_);

    setFORCESParams(poly_indices);
    bool normal_finish = false, update_result = false;

    if (!switch_to_final)
    {
      std::cout << "[NMPC NORMAL] step two : set up the solver ... " << std::endl;

      exit_code = nmpc_forces_solver_normal_.solveNormal(mpc_output_, external_acc_,
                                                         ref_total_pos_, ref_total_yaw_,
                                                         ellipsoid_matrices_, poly_constraints_, poly_indices);
      normal_finish = true;
    }
    else
    {
      std::cout << "[NMPC FINAL] step two : set up the solver ... " << std::endl;

      exit_code = nmpc_forces_solver_final_.solveFinal(mpc_output_, external_acc_,
                                                       ref_total_pos_, ref_total_yaw_,
                                                       ellipsoid_matrices_, poly_constraints_, poly_indices);
    }

    /* check whether or not to use the results*/
    if (exit_code == 1)
    { // success
      // reset the variables//
      fail_count_ = 0;
      replan_count_ = 0;
      update_result = true;
    }
    else
    {
      fail_count_ += 1;
      if (replan_count_ > 3 && exit_code == 0)
      {
        fail_count_ = 0;
        replan_count_ = 0;
        update_result = true;
      }
      else if (fail_count_ > 2)
      {
        fail_count_ = 0;
        replan_count_ += 1;
        ROS_WARN("[resilient_planner] MPC Fails too much time, replan !");
        kino_replan_ = true;
      }
    }

    if (update_result)
    {
      if (normal_finish)
      {
        nmpc_forces_solver_normal_.updateNormal(mpc_output_);
      }
      else
      {
        nmpc_forces_solver_final_.updateFinal(mpc_output_);
      }
      updateFORCESResults();
    }

    finish1 = clock();
    totalTime = (double)(finish1 - start) / CLOCKS_PER_SEC * 1000;
    std::cout << "time is ：" << totalTime << "ms" << std::endl;
    std::cout << "exit_code  ：" << exit_code << std::endl;

    /*    check replan or reach the global end    */
    Eigen::Vector3d ref_end = (mpc_output_.at(19)).segment(8, 3);
    int max_index = (int)((planning_horizon_ * Ts_ + (mpc_start_time_ - kino_start_time_).toSec()) / Ts_);


    if ( max_index > 0.5*kino_size_  && (end_pt_ - kino_path_[kino_size_ - 1]).norm() > 0.7)
    {
      ROS_WARN("[resilient_planner] Reach the local end, replan!");
      kino_replan_ = true;
    }

    /*    check for odom and predict state   */
    if (((mpc_output_.at(1)).segment(8, 3) - stateMpc_.segment(0, 3)).norm() > 1.5)
    {
      ROS_WARN("[resilient_planner] Odom far away from predict state !");
      cmd_status_ = CMD_STATUS::WAIT;
      ROS_INFO_STREAM("stateMpc_ is : \n"
                      << stateMpc_);
      for (unsigned int i = 0; i < mpc_output_.size() - 1; i++)
      {
        for (unsigned int j = 0; j < var; j++)
        {
          std::cout << mpc_output_.at(i)(j) << " ";
        }
        std::cout << " \n"
                  << std::endl;
      }
      return 4;
    }

    /* check whether to switch mode  */
    if (max_index >= kino_size_ || (ref_end - end_pt_).norm() < 1.0)
      switch_to_final = true;

    if ((ref_end - end_pt_).norm() < 0.26)
    {
      std::cout << "NMPCSolver::pubEnd  ：" << std::endl;
      pub_end_ = true;
      cmd_end_pt_ = end_pt_;
      return 2;
    }

    if (kino_replan_)
    {
      kino_replan_ = false;
      return -1;
    }

    return 1; //SUCCESS
  }

  void NMPCSolver::setFORCESParams(Eigen::VectorXd &poly_indices)
  {
    last_yaw_ = mpc_output_.at(1)(16);
    Eigen::MatrixXd Q_init = pow(epsilon, 2) * Eigen::MatrixXd::Identity(nx, nx);
    Eigen::Matrix3d Q, Q1, Q2;

    for (int i = 0; i < planning_horizon_; i++)
    {

      getCurTraj(i);
      ref_total_pos_.push_back(ref_pos_);
      ref_total_yaw_.push_back(ref_yaw_);

      // use predicted euler angle
      Eigen::Vector3d euler_cur(mpc_output_.at(i)(14), mpc_output_.at(i)(15), mpc_output_.at(i)(16));
      Eigen::Vector3d vel_cur(mpc_output_.at(i)(11), mpc_output_.at(i)(12), mpc_output_.at(i)(13));

      Eigen::Matrix3d R_cur = updateMatrix(euler_cur, vel_cur, mpc_output_.at(i)(3));

      Q1 = R_cur * ego_size_ * R_cur.transpose();

      if (i == 0)
      {
        Q = Q1;
      }
      else
      {
        double beta = sqrt(Q1.trace() / Q2.trace());
        Q = (1 + 1 / beta) * Q1 + (1 + beta) * Q2;
      }

      Eigen::EigenSolver<Eigen::Matrix3d> es(Q);
      Eigen::Matrix3d E = (es.eigenvectors() * (es.eigenvalues().cwiseSqrt()).asDiagonal() * es.eigenvectors().inverse()).real();

      poly_indices(i) = getSikangConst(E); // push_back poly_constraints_

      ellipsoid_matrices_.push_back(E);

      // disturbance elliposid
      Q2 = getDistrEllipsoid(Ts_, Q_init); //update Q_init  <1ms
    }
  }

  Eigen::Matrix3d NMPCSolver::eulerToRot(Eigen::Vector3d &odom_euler)
  {

    Eigen::Vector3d rpy(odom_euler(0), odom_euler(1), odom_euler(2));
    Eigen::Quaternion<double> qx(cos(rpy(0) / 2), sin(rpy(0) / 2), 0, 0);
    Eigen::Quaternion<double> qy(cos(rpy(1) / 2), 0, sin(rpy(1) / 2), 0);
    Eigen::Quaternion<double> qz(cos(rpy(2) / 2), 0, 0, sin(rpy(2) / 2));
    Eigen::Matrix3d R = Eigen::Matrix3d(qz * qy * qx);

    return R;
  }

  /// calculate the disturbance ellipsoid
  Eigen::Matrix3d NMPCSolver::getDistrEllipsoid(double t, Eigen::MatrixXd &Q_origin)
  {

    Eigen::MatrixXd Nt(nx, nx);
    Eigen::MatrixXd Array_Q(nx, nx);
    Eigen::MatrixXd temp_Q = Eigen::MatrixXd::Zero(nx, nx);
    double temp;
    Eigen::MatrixXd B = Phi_.transpose();
    //ROS_INFO_STREAM(" B : \n" << B );
    Eigen::ComplexSchur<Eigen::MatrixXd> SchurA(Phi_);
    Eigen::MatrixXcd R = SchurA.matrixT();
    Eigen::MatrixXcd U = SchurA.matrixU();
    Eigen::ComplexSchur<Eigen::MatrixXd> SchurB(B);
    Eigen::MatrixXcd S = SchurB.matrixT();
    Eigen::MatrixXcd V = SchurB.matrixU();
    // refer to this: https://stackoverflow.com/questions/56929966/implementing-the-bartels-stewart-algorithm-in-eigen3
    // ROS_INFO_STREAM(" R : \n" << R);
    // ROS_INFO_STREAM(" U : \n" << U );
    // ROS_INFO_STREAM(" S : \n" << S );
    // ROS_INFO_STREAM(" V : \n" << V );
    Eigen::MatrixXd X = Eigen::MatrixXd::Zero(nx, nx);

    for (int i = 0; i < nw; ++i)
    {

      Nt = t * w_(i) * w_(i) * Dt_.col(i) * (Dt_.col(i)).transpose(); // nx * nx
      //ROS_INFO_STREAM("Nt : \n" << Nt);
      Array_Q = Nt - (-Phi_ * t).exp() * Nt * (-Phi_.transpose() * t).exp(); // nx * nx
      //ROS_INFO_STREAM("Array_Q: \n" << Array_Q);
      // Phi*X + X*Phi' = W
      Eigen::MatrixXcd F = (U.adjoint() * Array_Q) * V;
      Eigen::MatrixXcd Y = Eigen::internal::matrix_function_solve_triangular_sylvester(R, S, F);
      Eigen::MatrixXd X = ((U * Y) * V.adjoint()).real();
      //ROS_INFO_STREAM(" X: \n" <<  X);
      //ROS_INFO_STREAM(" X.trace(): \n" <<  X.trace());
      temp += sqrt(X.trace());
      //ROS_INFO_STREAM("temp: \n" <<  temp);
      temp_Q += X / sqrt(X.trace());
    }

    Eigen::MatrixXd Qd = temp * temp_Q; // nx * nx
    //ROS_INFO_STREAM("Qd: \n" << Qd);

    double beta = sqrt(Q_origin.trace() / Qd.trace());

    Eigen::MatrixXd Q_update = (1 + 1 / beta) * Q_origin + (1 + beta) * Qd; // nx * nx
    Eigen::MatrixXd position_Q = (Phi_ * t).exp() * Q_update * (Phi_.transpose() * t).exp();
    //ROS_INFO_STREAM("position_Q: \n" << position_Q);

    // update Q_origin
    Q_origin = Q_update;

    return position_Q.block(0, 0, 3, 3);
  }

  void NMPCSolver::updateFORCESResults()
  {

    /* cmd update */
    pre_mpc_output_ = mpc_output_;
    //cmd_status_ = CMD_STATUS::PUB_TRAJ;
    have_mpc_traj_ = true;

    for (int i = 0; i < planning_horizon_; i++)
    {
      if (mpc_output_.at(i)(16) < -PI)
      {
        mpc_output_.at(i)(16) = mpc_output_.at(i)(16) + 2 * PI;
      }
      else if (mpc_output_.at(i)(16) > PI)
      {
        mpc_output_.at(i)(16) = mpc_output_.at(i)(16) - 2 * PI;
      }
    }

    mpc_output_.at(20) = mpc_output_.at(19);

    /* visulizations  */
    displayNMPCPoints();
    //displayNMPCAll();
    displayRefPoints();
    //displayRefAll();
    displayEllipsoids();
    displayPoly();
  }

  void NMPCSolver::cmdTrajCallback(const ros::TimerEvent &e)
  {

    switch (cmd_status_)
    {
    case INIT_POSITION:
    {
      break;
    }

    case ROTATE_YAW:
    {

      trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;
      trajectory_msg.header.stamp = ros::Time::now();
      trajectory_msg.header.frame_id = "world";
      trajectory_msgs::MultiDOFJointTrajectoryPoint point_msg;
      //std::cout << "The init_yaw_ is " << init_yaw_ <<  std::endl;
      double desired_yaw;

      if (init_yaw_ - realOdom_(8) >= 0)
      {
        //std::cout << "The (ros::Time::now()- change_yaw_time_).toSec()*init_yaw_dot_ is " <<  (ros::Time::now()- change_yaw_time_).toSec()*init_yaw_dot_<<  std::endl;
        desired_yaw = min(realOdom_(8) + (ros::Time::now() - change_yaw_time_).toSec() * init_yaw_dot_, init_yaw_);
      }
      else
      {
        //std::cout << "The (ros::Time::now()- change_yaw_time_).toSec()*init_yaw_dot_ is " <<  (ros::Time::now()- change_yaw_time_).toSec()*init_yaw_dot_<<  std::endl;
        //std::cout << "============================================="<<  std::endl;
        desired_yaw = max(realOdom_(8) + (ros::Time::now() - change_yaw_time_).toSec() * init_yaw_dot_, init_yaw_);
      }
      //std::cout << "The desired_yaw is " << desired_yaw<<  std::endl;

      Eigen::Vector3d desired_position(realOdom_(0), realOdom_(1), realOdom_(2));

      mav_msgs::msgMultiDofJointTrajectoryFromPositionYaw(
          desired_position, desired_yaw, &trajectory_msg);
      traj_cmd_pub_.publish(trajectory_msg);

      break;
    }

    case WAIT:
    {

      break;
    }

    case EMERGENCY_STOP:
    {
      break;
    }

    case PUB_END:
    { // pub_end_ && finish_mpc_cmd_

      trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;

      trajectory_msg.header.stamp = ros::Time::now();
      trajectory_msg.header.frame_id = "world";
      trajectory_msgs::MultiDOFJointTrajectoryPoint point_msg;

      point_msg.transforms.resize(1);
      point_msg.velocities.resize(1);
      point_msg.accelerations.resize(1);

      point_msg.transforms[0].translation.x = cmd_end_pt_(0);
      point_msg.transforms[0].translation.y = cmd_end_pt_(1);
      point_msg.transforms[0].translation.z = cmd_end_pt_(2);

      point_msg.velocities[0].linear.x = 0;
      point_msg.velocities[0].linear.y = 0;
      point_msg.velocities[0].linear.z = 0;

      //ROS_INFO_STREAM( " =========== mpcq end_pt_ " << end_pt_);

      double roll = pre_mpc_output_.at(19)(14);
      double pitch = pre_mpc_output_.at(19)(15);
      double yaw = pre_mpc_output_.at(19)(16);

      Eigen::Quaterniond q = Eigen::AngleAxisd(roll, Eigen::Vector3d::UnitX()) * Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitY()) * Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ());

      //Eigen::Quaterniond q_yaw = mav_msgs::quaternionFromYaw(yaw);

      point_msg.transforms[0].rotation.x = q.x();
      point_msg.transforms[0].rotation.y = q.y();
      point_msg.transforms[0].rotation.z = q.z();
      point_msg.transforms[0].rotation.w = q.w();

      //point_msg.time_from_start = ros::Duration(Ts_);
      trajectory_msg.points.push_back(point_msg);

      traj_cmd_pub_.publish(trajectory_msg);
      initialized_output_ = false;

      ROS_INFO_STREAM("!!!!!!!!!!!!  end time is " << ros::Time::now().toSec());

      cmd_status_ = CMD_STATUS::WAIT;
      break;
    }

    case PUB_TRAJ:
    { // pub_end_ && finish_mpc_cmd_

      if (!have_mpc_traj_)
        break;

      ros::Time time_now = ros::Time::now();
      double t_cur = (time_now - pre_mpc_start_time_).toSec();
      int cur_index = (int)(t_cur / Ts_);

      trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;
      trajectory_msg.header.stamp = ros::Time::now();
      trajectory_msg.header.frame_id = "world";
      trajectory_msgs::MultiDOFJointTrajectoryPoint point_msg;

      point_msg.transforms.resize(1);
      point_msg.velocities.resize(1);
      point_msg.accelerations.resize(1);

      if (cur_index < planning_horizon_ - 1 && t_cur >= 0.0)
      {
        Eigen::VectorXd mpcq = pre_mpc_output_.at(cur_index) + fmod(t_cur, Ts_) / Ts_ * (pre_mpc_output_.at(cur_index + 1) - pre_mpc_output_.at(cur_index));
        //ROS_INFO_STREAM( " =========== mpcq " << mpcq);

        point_msg.transforms[0].translation.x = mpcq(8);
        point_msg.transforms[0].translation.y = mpcq(9);
        point_msg.transforms[0].translation.z = mpcq(10);

        point_msg.velocities[0].linear.x = mpcq(11);
        point_msg.velocities[0].linear.y = mpcq(12);
        point_msg.velocities[0].linear.z = mpcq(13);

        point_msg.velocities[0].angular.x = mpcq(0);
        point_msg.velocities[0].angular.y = mpcq(1);
        point_msg.velocities[0].angular.z = mpcq(2);

        Eigen::Vector3d odom_euler(mpcq(14), mpcq(15), mpcq(16));
        Eigen::Vector3d thrust_b(0.0, 0.0, mpcq(3));
        Eigen::Vector3d thrust_w = eulerToRot(odom_euler) * thrust_b;

        point_msg.accelerations[0].linear.x = thrust_w(0) / mass;
        point_msg.accelerations[0].linear.y = thrust_w(1) / mass;
        point_msg.accelerations[0].linear.z = thrust_w(2) / mass - g;

        //double yaw = mpcq(16);
        //std::cout << "yaw is :"  << yaw << std::endl;

        Eigen::Quaterniond q = Eigen::AngleAxisd(mpcq(14), Eigen::Vector3d::UnitX()) * Eigen::AngleAxisd(mpcq(15), Eigen::Vector3d::UnitY()) * Eigen::AngleAxisd(mpcq(16), Eigen::Vector3d::UnitZ());

        //Eigen::Quaterniond q_yaw = mav_msgs::quaternionFromYaw(yaw);

        point_msg.transforms[0].rotation.x = q.x();
        point_msg.transforms[0].rotation.y = q.y();
        point_msg.transforms[0].rotation.z = q.z();
        point_msg.transforms[0].rotation.w = q.w();
        //std::cout << "q.z() is :"  << q.z() << std::endl;

        Eigen::Vector3d pos(mpcq(8), mpcq(9), mpcq(10));
        Eigen::Vector3d dir(cos(mpcq(16)), sin(mpcq(16)), 0.0);
        drawCmd(pos, 2 * dir, 2, Eigen::Vector4d(1, 1, 0, 0.7));

        //point_msg.time_from_start = ros::Duration(Ts_);
        trajectory_msg.points.push_back(point_msg);
        traj_cmd_pub_.publish(trajectory_msg);
        finish_mpc_cmd_ = false;
      }
      else
      {

        finish_mpc_cmd_ = true;
        if (pub_end_)
          cmd_status_ = CMD_STATUS::PUB_END;
      }
      break;
    }
    }
  }


  // odom_euler:  roll pitch yaw
  Eigen::Matrix3d NMPCSolver::updateMatrix(Eigen::Vector3d &odom_euler, Eigen::Vector3d& odom_vel, double thrust_c)
  {
    // update At matrix
    double roll = odom_euler(0), pitch = odom_euler(1), yaw = odom_euler(2); 
    double v1 = odom_vel(0), v2 = odom_vel(1), v3 = odom_vel(2);

    // roll
    At_(3, 6) = (thrust_c * 1.0 / mass) * (-cos(yaw) * sin(pitch) * sin(roll) + sin(yaw) * cos(roll));
    At_(4, 6) = (thrust_c * 1.0 / mass) * (-sin(yaw) * sin(pitch) * sin(roll) - cos(yaw) * cos(roll));
    At_(5, 6) = (thrust_c * 1.0 / mass) * (-sin(roll)) * cos(pitch);
    // pitch
    At_(3, 7) = (thrust_c * 1.0 / mass) * (cos(yaw) * cos(pitch) * cos(roll));
    At_(4, 7) = (thrust_c * 1.0 / mass) * (sin(yaw) * cos(pitch) * cos(roll));
    At_(5, 7) = (thrust_c * 1.0 / mass) * cos(roll) * (-sin(pitch));
    // yaw
    At_(3, 8) = (thrust_c * 1.0 / mass) * (-sin(yaw) * sin(pitch) * cos(roll) + cos(yaw) * sin(roll));
    At_(4, 8) = (thrust_c * 1.0 / mass) * (cos(yaw) * sin(pitch) * cos(roll) + sin(yaw) * sin(roll));

    // add gradients with drag coefficient term (in some case we can neglect this term)
    Eigen::Matrix3d R_cur = eulerToRot(odom_euler);

    At_.block(3, 3, 3, 3) = R_cur * drag_coefficient_matrix_ * R_cur.transpose(); //transpose does not change
    double drag = drag_coefficient_matrix_(0,0);
    // roll

    // leave for further revision (not add the grad of drag coeff)
    // update Bt matrix                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      
    Bt_(3, 3) = 1.0 / mass * (cos(yaw) * sin(pitch) * cos(roll) + sin(yaw) * sin(roll));
    Bt_(4, 3) = 1.0 / mass * (sin(yaw) * sin(pitch) * cos(roll) - cos(yaw) * sin(roll));
    Bt_(5, 3) = 1.0 / mass * cos(roll) * cos(pitch);

    Phi_ = At_ + Bt_ * Kt_;

    return R_cur;
  }


  /*visulizations*/
  void NMPCSolver::drawCmd(const Eigen::Vector3d &pos, const Eigen::Vector3d &vec, const int &id,
                           const Eigen::Vector4d &color)
  {
    visualization_msgs::Marker mk_state;
    mk_state.header.frame_id = "world";
    mk_state.header.stamp = ros::Time::now();
    mk_state.id = id;
    mk_state.type = visualization_msgs::Marker::ARROW;
    mk_state.action = visualization_msgs::Marker::ADD;

    mk_state.pose.orientation.w = 1.0;
    mk_state.scale.x = 0.1;
    mk_state.scale.y = 0.2;
    mk_state.scale.z = 0.3;

    geometry_msgs::Point pt;
    pt.x = pos(0);
    pt.y = pos(1);
    pt.z = pos(2);
    mk_state.points.push_back(pt);

    pt.x = pos(0) + vec(0);
    pt.y = pos(1) + vec(1);
    pt.z = pos(2) + vec(2);
    mk_state.points.push_back(pt);

    mk_state.color.r = color(0);
    mk_state.color.g = color(1);
    mk_state.color.b = color(2);
    mk_state.color.a = color(3);

    cmd_vis_pub_.publish(mk_state);
  }

  void NMPCSolver::displayNMPCAll()
  {
    visualization_msgs::Marker mk_state;
    mk_state.header.frame_id = "world";
    mk_state.header.stamp = ros::Time::now();
    mk_state.id = 2;
    mk_state.type = visualization_msgs::Marker::LINE_LIST;
    mk_state.action = visualization_msgs::Marker::ADD;

    mk_state.pose.orientation.w = 1.0;
    mk_state.scale.x = 0.1;
    mk_state.scale.y = 0.2;
    mk_state.scale.z = 0.3;

    geometry_msgs::Point pt;

    mk_state.color.r = 0.5;
    mk_state.color.g = 0;
    mk_state.color.b = 0;
    mk_state.color.a = 0.5;

    //std::cout << " the refer yaw is "<< std::endl;

    for (int i = 0; i < planning_horizon_; i++)
    {
      pt.x = pre_mpc_output_.at(i)(8);
      pt.y = pre_mpc_output_.at(i)(9);
      pt.z = pre_mpc_output_.at(i)(10);
      mk_state.points.push_back(pt);

      double yaw = pre_mpc_output_.at(i)(16);

      Eigen::Vector3d dir(cos(yaw), sin(yaw), 0.0);

      pt.x = ref_total_pos_.at(i)(0) + 2 * dir(0);
      pt.y = ref_total_pos_.at(i)(1) + 2 * dir(1);
      pt.z = ref_total_pos_.at(i)(2) + 2 * dir(2);
      //std::cout <<  yaw  << std::endl;
      mk_state.points.push_back(pt);
    }

    nmpc_marker_pub_.publish(mk_state);
  }

  void NMPCSolver::displayNMPCPoints()
  {
    //std::cout<<"[Disturbance Ellipsoid with python] display the points ："<<std::endl;
    visualization_msgs::Marker mk;
    mk.header.frame_id = "world";
    mk.header.stamp = ros::Time::now();
    mk.type = visualization_msgs::Marker::LINE_STRIP;
    mk.action = visualization_msgs::Marker::DELETE;

    nmpc_marker_pub_.publish(mk);
    geometry_msgs::Point pt;
    std_msgs::ColorRGBA pc;

    mk.action = visualization_msgs::Marker::ADD;
    mk.pose.orientation.x = 0.0;
    mk.pose.orientation.y = 0.0;
    mk.pose.orientation.z = 0.0;
    mk.pose.orientation.w = 1.0;

    pc.r = 0.5;
    pc.g = 0;
    pc.b = 0;
    pc.a = 0.6;

    for (int i = 0; i < planning_horizon_; i++)
    {

      mk.scale.x = 0.1;

      pt.x = pre_mpc_output_.at(i)(8);
      pt.y = pre_mpc_output_.at(i)(9);
      pt.z = pre_mpc_output_.at(i)(10);

      mk.points.push_back(pt);
      mk.colors.push_back(pc);
    }

    nmpc_marker_pub_.publish(mk);
  }

  void NMPCSolver::displayRefAll()
  {
    visualization_msgs::Marker mk_state;
    mk_state.header.frame_id = "world";
    mk_state.header.stamp = ros::Time::now();
    mk_state.id = 2;
    mk_state.type = visualization_msgs::Marker::LINE_LIST;
    mk_state.action = visualization_msgs::Marker::ADD;

    mk_state.pose.orientation.w = 1.0;
    mk_state.scale.x = 0.1;
    mk_state.scale.y = 0.2;
    mk_state.scale.z = 0.3;

    geometry_msgs::Point pt;

    mk_state.color.r = 0;
    mk_state.color.g = 0;
    mk_state.color.b = 0.5;
    mk_state.color.a = 0.5;

    //std::cout << " the refer yaw is "<< std::endl;

    for (int i = 0; i < planning_horizon_; i++)
    {
      pt.x = ref_total_pos_.at(i)(0);
      pt.y = ref_total_pos_.at(i)(1);
      pt.z = ref_total_pos_.at(i)(2);
      mk_state.points.push_back(pt);

      double yaw = ref_total_yaw_.at(i);

      Eigen::Vector3d dir(cos(yaw), sin(yaw), 0.0);

      pt.x = ref_total_pos_.at(i)(0) + 2 * dir(0);
      pt.y = ref_total_pos_.at(i)(1) + 2 * dir(1);
      pt.z = ref_total_pos_.at(i)(2) + 2 * dir(2);
      //std::cout <<  yaw  << std::endl;
      mk_state.points.push_back(pt);
    }

    ref_marker_pub_.publish(mk_state);
  }

  void NMPCSolver::displayRefPoints()
  {
    //std::cout<<"[Disturbance Ellipsoid with python] display the points ："<<std::endl;
    visualization_msgs::Marker mk;
    mk.header.frame_id = "world";
    mk.header.stamp = ros::Time::now();
    mk.type = visualization_msgs::Marker::SPHERE_LIST;
    mk.action = visualization_msgs::Marker::DELETE;

    ref_marker_pub_.publish(mk);
    geometry_msgs::Point pt;
    std_msgs::ColorRGBA pc;

    mk.action = visualization_msgs::Marker::ADD;
    mk.pose.orientation.x = 0.0;
    mk.pose.orientation.y = 0.0;
    mk.pose.orientation.z = 0.0;
    mk.pose.orientation.w = 1.0;

    pc.r = 0;
    pc.g = 0;
    pc.b = 0.5;
    pc.a = 0.6;

    //std::cout << " the refer path is "<< std::endl;

    for (int i = 0; i < planning_horizon_; i++)
    {

      mk.scale.x = 0.2;
      mk.scale.y = 0.2;
      mk.scale.z = 0.2;

      pt.x = ref_total_pos_.at(i)(0);
      pt.y = ref_total_pos_.at(i)(1);
      pt.z = ref_total_pos_.at(i)(2);

      mk.points.push_back(pt);
      mk.colors.push_back(pc);
      //std::cout <<  ref_total_pos_.at(i)(0) << " " <<  ref_total_pos_.at(i)(1) << " " << ref_total_pos_.at(i)(2) << " " << std::endl;
    }
    ref_marker_pub_.publish(mk);
  }

  void NMPCSolver::displayEllipsoids()
  {
    decomp_ros_msgs::EllipsoidArray ellipsoids;
    for (int i = 0; i < planning_horizon_; i++)
    {
      decomp_ros_msgs::Ellipsoid ellipsoid;
      ellipsoid.d[0] = mpc_output_.at(i)(8);
      ellipsoid.d[1] = mpc_output_.at(i)(9);
      ellipsoid.d[2] = mpc_output_.at(i)(10);

      auto C = ellipsoid_matrices_[i];
      //std::cout <<  "ellipsoid_matrices_[i];" << std::endl;
      for (int x = 0; x < 3; x++)
      {
        for (int y = 0; y < 3; y++)
        {
          ellipsoid.E[3 * x + y] = C(x, y);
          //std::cout <<  C(x, y) << std::endl;
        }
      }
      ellipsoids.ellipsoids.push_back(ellipsoid);
    }

    ellipsoids.header.frame_id = "world";
    ellipsoid_pub_.publish(ellipsoids);
  }

  void NMPCSolver::calculate_yaw(Eigen::Vector3d pos, Eigen::Vector3d pos_next)
  {
    constexpr double YAW_DOT_MAX_PER_SEC = PI;
    double yaw = 0;
    double yawdot = 0;

    Eigen::Vector3d dir = pos_next - pos;

    double yaw_temp = dir.norm() > 0.1 ? atan2(dir(1), dir(0)) : last_yaw_; // if dir is some or not change


    if (fabs(yaw_temp - last_yaw_) > PI)
    {
      if (yaw_temp > 0)
      { // -3.14 to 3.1
        yaw = yaw_temp - 2 * PI;
      }
      else
      { // 3.14 to -3.1
        yaw = yaw_temp + 2 * PI;
      }
    }
    else
    {
      yaw = yaw_temp;
    }

    yaw = 0.2 * last_yaw_ + 0.8 * yaw; // nieve LPF

    last_yaw_ = yaw;
    ref_yaw_ = yaw;
  }

  /*** callbacks  ***/ //ok
  void NMPCSolver::cloudCallback(const sensor_msgs::PointCloud2 &msg)
  {
    //std::cout << "[resilient_planner] the cloud msg" << std::endl;

    sensor_msgs::PointCloud out_cloud;
    sensor_msgs::convertPointCloud2ToPointCloud(msg, out_cloud);
    vec_obs_ = DecompROS::cloud_to_vec(out_cloud);
  }

}