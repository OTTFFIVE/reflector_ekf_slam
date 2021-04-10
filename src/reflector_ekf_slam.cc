#include <reflector_ekf_slam/reflector_ekf_slam.h>

ReflectorEKFSLAM::ReflectorEKFSLAM (const double &time, const double &x0, const double &y0, const double &yaw0): time_(time)
{
    init_pose_.x() = x0;
    init_pose_.y() = y0;
    init_pose_.z() = yaw0;
    vt_ = 0.0;
    wt_ = 0.0;
    /* 初始时刻机器人位姿为0，绝对准确, 协方差为0 */
    mu_ = Eigen::Vector3d::Zero();
    mu_.topRows(3) = init_pose_;
    sigma_.resize(3, 3);
    sigma_.setZero();
    Qu_ << 0.05 * 0.05, 0.f, 0.f, 0.068 * 0.068;
    Qt_ << 0.05 * 0.05, 0.f, 0.f, 0.05 * 0.05;
    lidar_to_base_link_ << 0.13686,0.,0;
}

ReflectorEKFSLAM::~ReflectorEKFSLAM()
{
    if(!map_path_.empty() && !map_.reflector_map_.empty())
    {
        // write data
        std::ofstream out(map_path_.c_str(), std::ios::in);
        if(!map_.reflector_map_.empty())
        {
            for(int i = 0; i < map_.reflector_map_.size(); ++i)
            {
                if( i != map_.reflector_map_.size()- 1)
                    out << map_.reflector_map_[i].x() << "," << map_.reflector_map_[i].y() << ",";
                else
                    out << map_.reflector_map_[i].x() << "," << map_.reflector_map_[i].y();
            }
        }
        if(mu_.rows() > 3)
        {
            out << ",";
            const int N = (mu_.rows() - 3) / 2;
            for(int i = 0; i < N; ++i)
            {
                if(i != N - 1)
                    out << mu_(3 + 2 * i) << "," << mu_(3 + 2 * i + 1) << ",";
                else
                    out << mu_(3 + 2 * i) << "," << mu_(3 + 2 * i + 1);
            }
        }
        out << std::endl;
        if(!map_.reflector_map_.empty())
        {
            for(int i = 0; i < map_.reflector_map_.size(); ++i)
            {
                if( i != map_.reflector_map_.size()- 1)
                    out << map_.reflector_map_coviarance_[i](0,0) << "," << map_.reflector_map_coviarance_[i](0,1) << ","
                        << map_.reflector_map_coviarance_[i](1,0) << "," << map_.reflector_map_coviarance_[i](1,1) << ",";
                else
                    out << map_.reflector_map_coviarance_[i](0,0) << "," << map_.reflector_map_coviarance_[i](0,1) << ","
                        << map_.reflector_map_coviarance_[i](1,0) << "," << map_.reflector_map_coviarance_[i](1,1);
            }
        }
        if(mu_.rows() > 3)
        {
            out << ",";
            const int N = (mu_.rows() - 3) / 2;
            for(int i = 0; i < N; ++i)
            {
                if(i != N - 1)
                    out << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(0,0) << "," << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(0,1) << ","
                        << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(1,0) << "," << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(1,1) << ",";
                else
                    out << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(0,0) << "," << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(0,1) << ","
                        << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(1,0) << "," << sigma_.block(3 + 2 * i, 3 + 2 * i, 2, 2)(1,1);
            }
        }
        out << std::endl;
        out.close();
    }
}

void ReflectorEKFSLAM::predict(const double& dt)
{
    const double delta_theta = wt_ * dt;
    const double delta_x = vt_ * dt * std::cos(mu_(2) + delta_theta / 2);
    const double delta_y = vt_ * dt * std::sin(mu_(2) + delta_theta / 2);

    const int N = mu_.rows();
    /***** 更新协方差 *****/
    /* 构造 Gt */
    const double angular_half_delta =  mu_(2) + delta_theta / 2;
    Eigen::MatrixXd G_xi = Eigen::MatrixXd::Identity(N, N);

    Eigen::Matrix3d G_xi_2 = Eigen::Matrix3d::Identity();
    G_xi_2(0, 2) = -vt_ * dt * std::sin(angular_half_delta);
    G_xi_2(1, 2) = vt_ * dt * std::cos(angular_half_delta);
    G_xi.block(0,0,3,3) = G_xi_2;

    /* 构造 Gu' */
    Eigen::MatrixXd G_u = Eigen::MatrixXd::Zero(N, 2);
    Eigen::MatrixXd G_u_2(3, 2);
    G_u_2 << dt * std::cos(angular_half_delta), -vt_ * dt * dt * std::sin(angular_half_delta) / 2,
        dt * std::sin(angular_half_delta), vt_ * dt * dt * std::cos(angular_half_delta) / 2,
        0, dt;
    G_u.block(0,0,3,2) = G_u_2;
    /* 更新协方差 */
    sigma_ = G_xi * sigma_ * G_xi.transpose() + G_u * Qu_ * G_u.transpose();
    /***** 更新均值 *****/
    mu_.topRows(3) += Eigen::Vector3d(delta_x, delta_y, delta_theta);
    mu_(2) = std::atan2(std::sin(mu_(2)), std::cos(mu_(2))); //norm
}

void ReflectorEKFSLAM::addEncoder (const nav_msgs::Odometry::ConstPtr &odometry)
{
    if(odometry->header.stamp.toSec() <= time_)
        return;
    const double now_time = odometry->header.stamp.toSec();
    /***** 保存上一帧编码器数据 *****/
    wt_ = odometry->twist.twist.angular.z;
    vt_ = odometry->twist.twist.linear.x;
    const double dt = now_time - time_;
    predict(dt);
    // const double delta_theta = wt_ * dt;
    // const double delta_x = vt_ * dt * std::cos(mu_(2) + delta_theta / 2);
    // const double delta_y = vt_ * dt * std::sin(mu_(2) + delta_theta / 2);

    // const int N = mu_.rows();
    // /***** 更新协方差 *****/
    // /* 构造 Gt */
    // const double angular_half_delta =  mu_(2) + delta_theta / 2;
    // Eigen::MatrixXd G_xi = Eigen::MatrixXd::Zero(N, 3);

    // Eigen::Matrix3d G_xi_2 = Eigen::Matrix3d::Identity();
    // G_xi_2(0, 2) = -vt_ * dt * std::sin(angular_half_delta);
    // G_xi_2(1, 2) = vt_ * dt * std::cos(angular_half_delta);
    // G_xi.block(0,0,3,3) = G_xi_2;

    // /* 构造 Gu' */
    // Eigen::MatrixXd G_u = Eigen::MatrixXd::Zero(N, 2);
    // Eigen::MatrixXd G_u_2(3, 2);
    // G_u_2 << dt * std::cos(angular_half_delta), -vt_ * dt * dt * std::sin(angular_half_delta) / 2,
    //     dt * std::sin(angular_half_delta), vt_ * dt * dt * std::cos(angular_half_delta) / 2,
    //     0, dt;
    // G_u.block(0,0,3,2) = G_u_2;
    // /* 更新协方差 */
    // sigma_ = G_xi * sigma_ * G_xi.transpose() + G_u * Qu_ * G_u.transpose();
    // /***** 更新均值 *****/
    // mu_.topRows(3) += Eigen::Vector3d(delta_x, delta_y, delta_theta);
    // mu_(2) = std::atan2(std::sin(mu_(2)), std::cos(mu_(2))); //norm
    // std::cout << "Predict covariance is: \n" << sigma_ << std::endl;
    time_ = now_time;
}

void ReflectorEKFSLAM::addPoints(const sensor_msgs::PointCloud2ConstPtr& points)
{
    const double now_time = points->header.stamp.toSec();
    const double dt = now_time - time_;

    // Predict now pose
    const int N = mu_.rows();
    predict(dt);
    time_ = now_time;
    std::cout << "  ↓↓↓-----------------------------------\n" <<
                 "Predict now pose is: " << mu_(0) << "," << mu_(1) << "," << mu_(2) << std::endl;
    std::cout << "Predict now covariance is: \n" << sigma_ << std::endl;

    // 完成基于3D点云的反光板提取
    Observation observation;
    if(!getObservations(*points, observation))
    {
        std::cout << "do not have detect any reflector" << std::endl;
        return;
    }

    matched_ids result = detectMatchedIds(observation);
    const int M_ = result.map_obs_match_ids.size();
    const int M = result.state_obs_match_ids.size();
    std::cout << "Match with old map size is: " << M_ << std::endl;
    std::cout << "Match with state vector size is: " << M << std::endl;
    const int MM = M + M_;
    if(MM > 0)
    {
        Eigen::MatrixXd H_t = Eigen::MatrixXd::Zero(2 * MM, N);
        Eigen::VectorXd zt = Eigen::VectorXd::Zero(2 * MM);
        Eigen::VectorXd zt_hat = Eigen::VectorXd::Zero(2 * MM);
        Eigen::MatrixXd Q = Eigen::MatrixXd::Zero(2 * MM, 2 * MM);
        const double cos_theta = std::cos(mu_(2));
        const double sin_theta = std::sin(mu_(2));
        Eigen::Matrix2d B;
        B << cos_theta, sin_theta, -sin_theta, cos_theta;
        if(M > 0)
        {
            const auto xy = [&](const int &id) -> Eigen::Vector2d {
                return Eigen::Vector2d(mu_(3 + 2 * id), mu_(3 + 2 * id + 1));
            };
            for(int i = 0; i < M; ++i)
            {
                const int local_id = result.state_obs_match_ids[i].first;
                const int global_id = result.state_obs_match_ids[i].second;
                zt(2 * i) = observation.cloud_[local_id].x();
                zt(2 * i + 1) = observation.cloud_[local_id].y();
                const double delta_x = xy(global_id).x() - mu_(0);
                const double delta_y = xy(global_id).y() - mu_(1);
                zt_hat(2 * i) = delta_x * cos_theta + delta_y * sin_theta;
                zt_hat(2 * i + 1) = -delta_x * sin_theta + delta_y * cos_theta;
                Eigen::MatrixXd A_i(2, 3);
                A_i << -cos_theta, -sin_theta, -delta_x * sin_theta + delta_y * cos_theta,
                    sin_theta, -cos_theta, -delta_x * cos_theta - delta_y * sin_theta;
                H_t.block(2 * i, 0, 2, 3) = A_i;
                H_t.block(2 * i, 3 + 2 * global_id, 2, 2) = B;
                Q.block(2 * i, 2 * i, 2, 2) = Qt_;
            }
        }
        if(M_ > 0)
        {
            const auto xy = [&](const int &id) -> Eigen::Vector2f {
                return map_.reflector_map_[id];
            };

            for(int i = 0; i < M_; ++i)
            {
                const int local_id = result.map_obs_match_ids[i].first;
                const int global_id =result.map_obs_match_ids[i].second;
                zt(2 * (M + i)) = observation.cloud_[local_id].x();
                zt(2 * (M + i) + 1) = observation.cloud_[local_id].y();
                const double delta_x = xy(global_id).x() - mu_(0);
                const double delta_y = xy(global_id).y() - mu_(1);

                zt_hat(2 * (M + i)) = delta_x * cos_theta + delta_y * sin_theta;
                zt_hat(2 * (M + i) + 1) = -delta_x * sin_theta + delta_y * cos_theta;

                Eigen::MatrixXd A_i(2, 3);
                A_i << -cos_theta, -sin_theta, -delta_x * sin_theta + delta_y * cos_theta,
                    sin_theta, -cos_theta, -delta_x * cos_theta - delta_y * sin_theta;
                H_t.block(2 * (M + i), 0, 2, 3) = A_i;

                Q.block(2 * (M + i), 2 * (M + i), 2, 2) = Qt_;
            }
        }
        const auto K_t = sigma_ * H_t.transpose() * (H_t * sigma_ * H_t.transpose() + Q).inverse();
        mu_ += K_t * (zt - zt_hat);
        mu_(2) = std::atan2(std::sin(mu_(2)), std::cos(mu_(2)));
        sigma_ = sigma_ - K_t * H_t * sigma_;
    }

    const int N2 = result.new_ids.size();
    if(N2 > 0)
    {
        std::cout << "Add " << N2 << " reflectors" << std::endl;
        // increase X_estimate and coviarance size
        const int M_e = N + 2 * N2;
        Eigen::VectorXd tmp_xe = Eigen::VectorXd::Zero(M_e);
        tmp_xe.topRows(N) = mu_.topRows(N);

        Eigen::MatrixXd tmp_sigma = Eigen::MatrixXd::Zero(M_e, M_e);
        tmp_sigma.block(0, 0, N, N) = sigma_;
        const Eigen::Matrix3d sigma_xi = sigma_.block(0, 0, 3, 3);
        const double sin_theta = std::sin(mu_(2));
        const double cos_theta = std::cos(mu_(2));
        Eigen::Matrix2d G_zi;
        G_zi << cos_theta, -sin_theta, sin_theta, cos_theta;
        auto point_transformed_to_global_frame = [&](const Eigen::Vector2f& p) -> Eigen::Vector2f{
            const float x = p.x() * std::cos(mu_(2)) - p.y() * std::sin(mu_(2)) + mu_(0);
            const float y = p.x() * std::sin(mu_(2)) + p.y() * std::cos(mu_(2)) + mu_(1);
            return Eigen::Vector2f(x,y);
        };
        Eigen::MatrixXd G_p(2 * N2, 3);
        Eigen::MatrixXd G_z(2 * N2, 2);
        Eigen::MatrixXd G_fx = Eigen::MatrixXd::Zero(2 * N2, N);

        for (int i = 0; i < N2; i++)
        {
            const int local_id = result.new_ids[i];
            const auto point = point_transformed_to_global_frame(observation.cloud_[local_id]);
            // update state vector
            tmp_xe(N + 2 * i) =  point.x();
            tmp_xe(N + 2 * i + 1) = point.y();
            // for update cov
            const double rx = observation.cloud_[local_id].x();
            const double ry = observation.cloud_[local_id].y();
            Eigen::MatrixXd Gp_i(2, 3);
            Gp_i << 1., 0., -rx * sin_theta - ry * cos_theta, 0., 1., rx * cos_theta - ry * sin_theta;
            G_p.block(2 * i, 0, 2, 3) = Gp_i;
            G_z.block(2 * i, 0, 2, 2) = G_zi;
            Eigen::MatrixXd G_fx_i = Eigen::MatrixXd::Zero(2, N);
            G_fx_i.topLeftCorner(2,3) = Gp_i;
            G_fx.block(2 * i, 0, 2, N) = G_fx_i;
        }
        const auto sigma_mm = G_p * sigma_xi * G_p.transpose() + G_z * Qt_ * G_z.transpose();
        const auto sigma_mx = G_fx * sigma_;
        tmp_sigma.block(N , 0, 2 * N2, N) = sigma_mx;
        tmp_sigma.block(0, N , N, 2 * N2) = sigma_mx.transpose();
        tmp_sigma.block(N, N, 2 * N2, 2 * N2) = sigma_mm;

        sigma_.resize(M_e, M_e);
        sigma_ = tmp_sigma;
        mu_.resize(M_e);
        mu_ = tmp_xe;
    }
    std::cout << "Update now pose is: " << mu_(0) << "," << mu_(1) << "," << mu_(2) << std::endl;
    std::cout << "state vector:  \n" << mu_ << std::endl;
    // std::cout << "covariance is:  \n" << sigma_ << std::endl;
    ekf_path_.header.stamp = points->header.stamp;
    ekf_path_.header.frame_id = "world";
    geometry_msgs::PoseStamped pose;
    pose.header.frame_id = "world";
    pose.header.stamp =  points->header.stamp;
    pose.pose.position.x = mu_(0);
    pose.pose.position.y = mu_(1);
    const double theta = mu_(2);
    pose.pose.orientation.x = 0.;
    pose.pose.orientation.y = 0.;
    pose.pose.orientation.z = std::sin(theta / 2);
    pose.pose.orientation.w = std::cos(theta / 2);
    ekf_path_.poses.push_back(pose);
}

void ReflectorEKFSLAM::addScan(const sensor_msgs::LaserScan::ConstPtr& scan)
{
    const double now_time = scan->header.stamp.toSec();
    const double dt = now_time - time_;

    // Predict now pose
    const int N = mu_.rows();
    predict(dt);
    // const double delta_theta = wt_ * dt;
    // const double delta_x = vt_ * dt * std::cos(mu_(2) + delta_theta / 2);
    // const double delta_y = vt_ * dt * std::sin(mu_(2) + delta_theta / 2);
    // const int N = mu_.rows();
    // /***** 更新协方差 *****/
    // /* 构造 Gt */
    // const double angular_half_delta =  mu_(2) + delta_theta / 2;
    // Eigen::MatrixXd G_xi = Eigen::MatrixXd::Zero(N, 3);

    // Eigen::Matrix3d G_xi_2 = Eigen::Matrix3d::Identity();
    // G_xi_2(0, 2) = -vt_ * dt * std::sin(angular_half_delta);
    // G_xi_2(1, 2) = vt_ * dt * std::cos(angular_half_delta);
    // G_xi.block(0,0,3,3) = G_xi_2;

    // /* 构造 Gu' */
    // Eigen::MatrixXd G_u = Eigen::MatrixXd::Zero(N, 2);
    // Eigen::MatrixXd G_u_2(3, 2);
    // G_u_2 << dt * std::cos(angular_half_delta), -vt_ * dt * dt * std::sin(angular_half_delta) / 2,
    //     dt * std::sin(angular_half_delta), vt_ * dt * dt * std::cos(angular_half_delta) / 2,
    //     0, dt;
    // G_u.block(0,0,3,2) = G_u_2;
    // /* 更新协方差 */
    // sigma_ = G_xi * sigma_ * G_xi.transpose() + G_u * Qu_ * G_u.transpose();
    // /***** 更新均值 *****/
    // mu_.topRows(3) += Eigen::Vector3d(delta_x, delta_y, delta_theta);
    // mu_(2) = std::atan2(std::sin(mu_(2)), std::cos(mu_(2))); //norm
    time_ = now_time;
    std::cout << "  ↓↓↓-----------------------------------\n" <<
                 "Predict now 2d pose is: " << mu_(0) << "," << mu_(1) << "," << mu_(2) << std::endl;
    // std::cout << "Predict now covariance is: \n" << sigma_ << std::endl;

    // 完成反光板点云提取
    Observation observation;
    if(!getObservations(*scan, observation))
    {
        std::cout << "\n do not have detect any reflector" << std::endl;
        return;
    }

    matched_ids result = detectMatchedIds(observation);
    const int M_ = result.map_obs_match_ids.size();
    const int M = result.state_obs_match_ids.size();
    std::cout << "Match with old map size is: " << M_ << std::endl;
    std::cout << "Match with state vector size is: " << M << std::endl;
    const int MM = M + M_;
    if(MM > 0)
    {
        Eigen::MatrixXd H_t = Eigen::MatrixXd::Zero(2 * MM, N);
        Eigen::VectorXd zt = Eigen::VectorXd::Zero(2 * MM);
        Eigen::VectorXd zt_hat = Eigen::VectorXd::Zero(2 * MM);
        Eigen::MatrixXd Q = Eigen::MatrixXd::Zero(2 * MM, 2 * MM);
        const double cos_theta = std::cos(mu_(2));
        const double sin_theta = std::sin(mu_(2));
        Eigen::Matrix2d B;
        B << cos_theta, sin_theta, -sin_theta, cos_theta;
        if(M > 0)
        {
            const auto xy = [&](const int &id) -> Eigen::Vector2d {
                return Eigen::Vector2d(mu_(3 + 2 * id), mu_(3 + 2 * id + 1));
            };
            for(int i = 0; i < M; ++i)
            {
                const int local_id = result.state_obs_match_ids[i].first;
                const int global_id = result.state_obs_match_ids[i].second;
                zt(2 * i) = observation.cloud_[local_id].x();
                zt(2 * i + 1) = observation.cloud_[local_id].y();
                const double delta_x = xy(global_id).x() - mu_(0);
                const double delta_y = xy(global_id).y() - mu_(1);
                zt_hat(2 * i) = delta_x * cos_theta + delta_y * sin_theta;
                zt_hat(2 * i + 1) = -delta_x * sin_theta + delta_y * cos_theta;
                Eigen::MatrixXd A_i(2, 3);
                A_i << -cos_theta, -sin_theta, -delta_x * sin_theta + delta_y * cos_theta,
                    sin_theta, -cos_theta, -delta_x * cos_theta - delta_y * sin_theta;
                H_t.block(2 * i, 0, 2, 3) = A_i;
                H_t.block(2 * i, 3 + 2 * global_id, 2, 2) = B;
                Q.block(2 * i, 2 * i, 2, 2) = Qt_;
            }
        }
        if(M_ > 0)
        {
            const auto xy = [&](const int &id) -> Eigen::Vector2f {
                return map_.reflector_map_[id];
            };

            for(int i = 0; i < M_; ++i)
            {
                const int local_id = result.map_obs_match_ids[i].first;
                const int global_id =result.map_obs_match_ids[i].second;
                zt(2 * (M + i)) = observation.cloud_[local_id].x();
                zt(2 * (M + i) + 1) = observation.cloud_[local_id].y();
                const double delta_x = xy(global_id).x() - mu_(0);
                const double delta_y = xy(global_id).y() - mu_(1);

                zt_hat(2 * (M + i)) = delta_x * cos_theta + delta_y * sin_theta;
                zt_hat(2 * (M + i) + 1) = -delta_x * sin_theta + delta_y * cos_theta;

                Eigen::MatrixXd A_i(2, 3);
                A_i << -cos_theta, -sin_theta, -delta_x * sin_theta + delta_y * cos_theta,
                    sin_theta, -cos_theta, -delta_x * cos_theta - delta_y * sin_theta;
                H_t.block(2 * (M + i), 0, 2, 3) = A_i;

                Q.block(2 * (M + i), 2 * (M + i), 2, 2) = Qt_;
            }
        }
        const auto K_t = sigma_ * H_t.transpose() * (H_t * sigma_ * H_t.transpose() + Q).inverse();
        mu_ += K_t * (zt - zt_hat);
        mu_(2) = std::atan2(std::sin(mu_(2)), std::cos(mu_(2)));
        sigma_ = sigma_ - K_t * H_t * sigma_;
    }

    const int N2 = result.new_ids.size();
    if(N2 > 0)
    {
        std::cout << "Add " << N2 << " reflectors" << std::endl;
        // increase X_estimate and coviarance size
        const int M_e = N + 2 * N2;
        Eigen::VectorXd tmp_xe = Eigen::VectorXd::Zero(M_e);
        tmp_xe.topRows(N) = mu_.topRows(N);

        Eigen::MatrixXd tmp_sigma = Eigen::MatrixXd::Zero(M_e, M_e);
        tmp_sigma.block(0, 0, N, N) = sigma_;
        const Eigen::Matrix3d sigma_xi = sigma_.block(0, 0, 3, 3);
        const double sin_theta = std::sin(mu_(2));
        const double cos_theta = std::cos(mu_(2));
        Eigen::Matrix2d G_zi;
        G_zi << cos_theta, -sin_theta, sin_theta, cos_theta;
        auto point_transformed_to_global_frame = [&](const Eigen::Vector2f& p) -> Eigen::Vector2f{
            const float x = p.x() * std::cos(mu_(2)) - p.y() * std::sin(mu_(2)) + mu_(0);
            const float y = p.x() * std::sin(mu_(2)) + p.y() * std::cos(mu_(2)) + mu_(1);
            return Eigen::Vector2f(x,y);
        };
        Eigen::MatrixXd G_p(2 * N2, 3);
        Eigen::MatrixXd G_z(2 * N2, 2);
        Eigen::MatrixXd G_fx = Eigen::MatrixXd::Zero(2 * N2, N);

        for (int i = 0; i < N2; i++)
        {
            const int local_id = result.new_ids[i];
            const auto point = point_transformed_to_global_frame(observation.cloud_[local_id]);
            // update state vector
            tmp_xe(N + 2 * i) =  point.x();
            tmp_xe(N + 2 * i + 1) = point.y();
            // for update cov
            const double rx = observation.cloud_[local_id].x();
            const double ry = observation.cloud_[local_id].y();
            Eigen::MatrixXd Gp_i(2, 3);
            Gp_i << 1., 0., -rx * sin_theta - ry * cos_theta, 0., 1., rx * cos_theta - ry * sin_theta;
            G_p.block(2 * i, 0, 2, 3) = Gp_i;
            G_z.block(2 * i, 0, 2, 2) = G_zi;
            Eigen::MatrixXd G_fx_i = Eigen::MatrixXd::Zero(2, N);
            G_fx_i.topLeftCorner(2,3) = Gp_i;
            G_fx.block(2 * i, 0, 2, N) = G_fx_i;
        }
        const auto sigma_mm = G_p * sigma_xi * G_p.transpose() + G_z * Qt_ * G_z.transpose();
        const auto sigma_mx = G_fx * sigma_;
        tmp_sigma.block(N , 0, 2 * N2, N) = sigma_mx;
        tmp_sigma.block(0, N , N, 2 * N2) = sigma_mx.transpose();
        tmp_sigma.block(N, N, 2 * N2, 2 * N2) = sigma_mm;

        sigma_.resize(M_e, M_e);
        sigma_ = tmp_sigma;
        mu_.resize(M_e);
        mu_ = tmp_xe;
    }
    std::cout << "Update now pose is: " << mu_(0) << "," << mu_(1) << "," << mu_(2) << std::endl;
    std::cout << "state vector:  \n" << mu_ << std::endl;
    // std::cout << "covariance is:  \n" << sigma_ << std::endl;
    ekf_path_.header.stamp = scan->header.stamp;
    ekf_path_.header.frame_id = "world";
    geometry_msgs::PoseStamped pose;
    pose.header.frame_id = "world";
    pose.header.stamp =  scan->header.stamp;
    pose.pose.position.x = mu_(0);
    pose.pose.position.y = mu_(1);
    const double theta = mu_(2);
    pose.pose.orientation.x = 0.;
    pose.pose.orientation.y = 0.;
    pose.pose.orientation.z = std::sin(theta / 2);
    pose.pose.orientation.w = std::cos(theta / 2);
    ekf_path_.poses.push_back(pose);
}

bool ReflectorEKFSLAM::getObservations(const sensor_msgs::PointCloud2& msg, Observation& obs)
{
    CloudPtr points_raw(new Cloud);
    CloudPtr reflector_points_i(new Cloud);
    pcl::PointCloud<pcl::PointXYZ>::Ptr reflector_points(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr reflector_points_filtered(new pcl::PointCloud<pcl::PointXYZ>);
    PointCloud centers;

    auto point_transformed_to_base_link = [&](const Eigen::Vector2f& p) -> Eigen::Vector2f{
        const float x = p.x() * std::cos(lidar_to_base_link_.z()) - p.y() * std::sin(lidar_to_base_link_.z()) + lidar_to_base_link_.x();
        const float y = p.x() * std::sin(lidar_to_base_link_.z()) + p.y() * std::cos(lidar_to_base_link_.z()) + lidar_to_base_link_.y();
        return Eigen::Vector2f(x,y);
    };

    pcl::fromROSMsg(msg,*points_raw);
    for(int i = 0; i < points_raw->points.size(); i++)
    {
        if(points_raw->at(i).intensity > intensity_min_)
        {
            reflector_points_i->points.push_back(points_raw->at(i));
        }
    }

    pcl::copyPointCloud(*reflector_points_i , *reflector_points);

    // 离群点剔除
    // 1)统计滤波法
    pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;
    sor.setInputCloud(reflector_points);
    sor.setMeanK(30);//临近点
    sor.setStddevMulThresh(0.5);//距离大于1倍标准方差，值越大，丢掉的点越少
    sor.filter(*reflector_points_filtered);

    pcl::toROSMsg(*reflector_points_filtered,msg_reflector);
    msg_reflector.header.frame_id = "world";

    // 2)半径滤波法
    // pcl::RadiusOutlierRemoval<pcl::PointXYZ> outrem;
    // outrem.setInputCloud(reflector_points);
    // outrem.setRadiusSearch(0.3);
    // outrem.setMinNeighborsInRadius(4);
    // outrem.filter(*reflector_points_filtered);

    // 取反，获取被剔除的离群点
    // sor.setNegative(true);
    // sor.filter(*cloud_filtered);

    // 通过聚类，提取出反光板点云块
    // 创建一个Kd树对象作为提取点云时所用的方法，
    pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);
    tree->setInputCloud (reflector_points_filtered);//创建点云索引向量，用于存储实际的点云信息
    std::vector<pcl::PointIndices> cluster_indices;
    pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;//欧式聚类对象
    ec.setClusterTolerance(0.2); //设置近邻搜索的搜索半径(m)
    ec.setMinClusterSize(4);//设置一个聚类需要的最少点数目
    ec.setMaxClusterSize(160); //设置一个聚类需要的最大点数目
    ec.setSearchMethod(tree);//设置点云的搜索机制
    ec.setInputCloud(reflector_points_filtered);
    ec.extract(cluster_indices);//从点云中提取聚类，并将 聚类后的点云块索引 保存在cluster_indices中

    //迭代访问点云索引cluster_indices，直到分割出所有聚类出的反光板点云
    for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it)
    {
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_cluster (new pcl::PointCloud<pcl::PointXYZ>);
        for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); pit++)
            cloud_cluster->points.push_back(reflector_points_filtered->points[*pit]);

        cloud_cluster->width = cloud_cluster->points.size ();
        cloud_cluster->height = 1;
        cloud_cluster->is_dense = true;
        cloud_cluster->header = reflector_points_filtered->header;
        cloud_cluster->sensor_orientation_ = reflector_points_filtered->sensor_orientation_;
        cloud_cluster->sensor_origin_ = reflector_points_filtered->sensor_origin_;

        std::cout << "PointCloud representing the Cluster: " << cloud_cluster->points.size () << " data points." << std::endl;

        // PCL函数计算质心
        Eigen::Vector4f centroid;// 质量m默认为1 (x,y,z,1)
        pcl::compute3DCentroid(*cloud_cluster, centroid);	// 计算当前质心
        Eigen::Vector2f now_point{centroid(0),centroid(1)};
        centers.push_back(point_transformed_to_base_link(now_point));
    }

    if(centers.empty())
        return false;
    obs.time_ = msg.header.stamp.toSec();
    obs.cloud_ = centers;
    // 输出检测到的反光板个数
    std::cout << "\n detected " << obs.cloud_.size() << " reflectors" << std::endl;
    return true;
}

bool ReflectorEKFSLAM::getObservations(const sensor_msgs::LaserScan& msg, Observation& obs)
{
    if (msg.range_min < 0 || msg.range_max <= msg.range_min)
    {
        std::cout << "Scan message range min and max is wrong" << std::endl;
        exit(-1);
    }
    if (msg.angle_increment < 0.f && msg.angle_max <= msg.angle_min)
    {
        std::cout << "Scan message angle min and max and angle increment is wrong" << std::endl;
        exit(-1);
    }
    // All reflector points
    std::vector<PointCloud> reflector_points;
    // All reflector centers
    PointCloud centers;
    // Now reflector points, ids and center
    PointCloud reflector;
    std::vector<int> reflector_id;
    Eigen::Vector2f now_center(0.f, 0.f);
    // All laser scan points in base_link frame
    PointCloud point_cloud;

    float angle = msg.angle_min;

    auto point_transformed_to_base_link = [&](const Eigen::Vector2f& p) -> Eigen::Vector2f{
        const float x = p.x() * std::cos(lidar_to_base_link_.z()) - p.y() * std::sin(lidar_to_base_link_.z()) + lidar_to_base_link_.x();
        const float y = p.x() * std::sin(lidar_to_base_link_.z()) + p.y() * std::cos(lidar_to_base_link_.z()) + lidar_to_base_link_.y();
        return Eigen::Vector2f(x,y);
    };

    // Detect reflectors and motion distortion correction hear
    // 反光板点云提取部分
    int points_number = msg.ranges.size();
    for (int i = 0; i < points_number; ++i)
    {
        // Get range data
        const float range = msg.ranges[i];
        // 间隙点云是否存在的标志位
        char detected_gap = false;
        // 只处理距离在[range_min_,range_max_]范围用内的点云
        // 因为当反光板距离激光较远时,激光能扫到的反光板点云数量很少,极不稳定。所以只考虑近距离内的反光板点云
        if (range_min_ <= range && range <= range_max_)
        {
            // Get now point xy value in sensor frame
            Eigen::Vector2f now_point(range * std::cos(angle), range * std::sin(angle));
            // Transform sensor point to odom frame and then transform to global frame
            point_cloud.push_back(point_transformed_to_base_link(now_point));

            // Detect reflector
            const double intensity = msg.intensities[i];
            // 通过强度阈值来提取来自反光板的点云
            if (intensity > intensity_min_)
            {
                // Add the first point
                if (reflector.empty())
                {
                    reflector.push_back(point_cloud.back());
                    reflector_id.push_back(i);
                    now_center += point_cloud.back();
                }
                else
                {
                    const int last_id = reflector_id.back();// 取得上一个点云的id
                    // Add connected points
                    // 若点云强度是连续很高,则认为是来自同一块反光板的点云(这里假定反光板反射回的点云总是大于强度阈值且连续成片)
                    if (i - last_id == 1)
                    {
                        reflector.push_back(point_cloud.back());
                        reflector_id.push_back(i);
                        now_center += point_cloud.back();// 累加点云,用于求和取平均
                    }
                    // 反光板间隙点云检测部分
                    // 因为点云的强度值影响因素很多,有可能一条反光板点云的中间会有某几个点强度较低
                    // 这样会造成1块反光板检测为多块 或者 1块反光板检测出来缺失一部分,从而影响同一反光板的中心点计算
                    else
                    {
                        // 若与上一个反光板点云相差最多3个点 且 当前点与之前的点在同一平面 且 当前点的下一个点也是高强度点云
                        // 则认为是反光板中间部分的弱强度点云,即间隙点云
                        if (i - last_id < 4 && fabs(msg.ranges[i] - msg.ranges[last_id]) < 0.3
                            && msg.intensities[i+1 < points_number ? i+1:i] > intensity_min_)
                        {
                            // 存储反光板中的间隙点云
                            int j = last_id+1;
                            for(; j < i; ++j)
                            {
                                const float range_gap = msg.ranges[j];
                                const float angle_gap = angle - msg.angle_increment*(i - j);
                                if(std::isinf(range_gap))// scan中很可能存在inf值
                                    continue;
                                // std::cout << "\n!!!!!!!!!!!!\n"<< "points_number=" << points_number << " i=" << i << " j=" << j << "\n";
                                // std::cout << range_gap << " -- " << angle_gap;
                                Eigen::Vector2f gap_point(range_gap * std::cos(angle_gap), range_gap * std::sin(angle_gap));
                                gap_point = point_transformed_to_base_link(gap_point);
                                reflector.push_back(gap_point);
                                reflector_id.push_back(j);
                                now_center += gap_point;
                            }
                            // 存储当前反光板中正常的高强度点云
                            detected_gap = true;
                            std::cout << "\n>>>gap_points of reflector detected! now add it back.";
                            reflector.push_back(point_cloud.back());
                            reflector_id.push_back(i);
                            now_center += point_cloud.back();
                        }

                        if(!detected_gap)
                        {
                            // Calculate reflector length
                            // 当下一帧高强度点云不是连续的,表明一块反光板上的点云已经检索完毕
                            // 此时,front和back的点云代表这块反光板的第一个点和最后一个点
                            // 求取位移差=检测到的反光板宽度
                            const float reflector_length = std::hypotf(reflector.front().x() - reflector.back().x(),
                                                                    reflector.front().y() - reflector.back().y());
                            // Add good reflector and its center
                            // 允许检测出的反光板宽度误差在 reflector_length_error 设定的误差范围内
                            if (fabs(reflector_length - reflector_min_length_) < reflector_length_error_)
                            {
                                reflector_points.push_back(reflector);// 存入反光板点云
                                centers.push_back(now_center / reflector.size());// 求平均 获得反光板点云的中心点位置
                            }
                            // Update now reflector
                            // 清除缓存,准备下一个反光板点云的存储
                            reflector.clear();
                            reflector_id.clear();
                            now_center.setZero(2);
                            // 当前反光板点云就是 下一个反光板的第一个点
                            reflector_id.push_back(i);
                            reflector.push_back(point_cloud.back());
                            now_center += point_cloud.back();
                            point_cloud.clear();// 防止内存占用一直增加
                            std::cout << "\n reflector +1";
                        }
                    }
                }
            }
        }
        // 一个点云判断完毕,下一个点云角度按照角度分辨率增加
        angle += msg.angle_increment;
    }
    // 处理可能刚好在scan最后的 反光板点云
    if (!reflector.empty())
    {
        const float reflector_length = std::hypotf(reflector.front().x() - reflector.back().x(),
                                                   reflector.front().y() - reflector.back().y());
        if (fabs(reflector_length - reflector_min_length_) < reflector_length_error_)
        {
            reflector_points.push_back(reflector);
            centers.push_back(now_center / reflector.size());
        }
    }

    if(centers.empty())
        return false;
    obs.time_ = msg.header.stamp.toSec();
    obs.cloud_ = centers;
    // 输出检测到的反光板个数
    std::cout << "\n detected " << obs.cloud_.size() << " reflectors" << std::endl;
    return true;
}

visualization_msgs::MarkerArray ReflectorEKFSLAM::toRosMarkers(double scale)
{

    visualization_msgs::MarkerArray markers;
    const int N = mu_.rows();
    if(N == 3)
    {
        std::cout << "no reflector " << std::endl;
        return markers;
    }

    const int M = (N - 3) / 2;
    std::cout << "now reflector size is : " << M << std::endl;
    for(int i = 0; i < M; i++)
    {
        const int id = 3 + 2 * i;
        double mx = mu_(id);
        double my = mu_(id + 1);
        // std::cout << "real xy: " << mx << "," << my << std::endl;


        /* 计算地图点的协方差椭圆角度以及轴长 */
        Eigen::Matrix2d sigma_m = sigma_.block(id, id, 2, 2); //协方差
        // std::cout << "cov: \n" << sigma_m <<std::endl;
        // Calculate Eigen Value(D) and Vectors(V), simga_m = V * D * V^-1
        // D = | D1 0  |  V = |cos  -sin|
        //     | 0  D2 |      |sin  cos |
        Eigen::EigenSolver<Eigen::Matrix2d> eigen_solver(sigma_m);
        const auto eigen_value = eigen_solver.pseudoEigenvalueMatrix();
        const auto eigen_vector = eigen_solver.pseudoEigenvectors();
        // Calculate angle and x y
        const double angle = std::atan2(eigen_vector(1, 0), eigen_vector(0, 0));
        const double x_len = 2 * std::sqrt(eigen_value(0, 0) * 5.991);
        const double y_len = 2 * std::sqrt(eigen_value(1, 1) * 5.991);
        // std::cout << "x_len: " << x_len << std::endl;
        // std::cout << "y_len: " << y_len << std::endl;

        /* 构造marker */
        visualization_msgs::Marker marker;
        marker.header.frame_id = "world";
        marker.header.stamp = ros::Time(time_);
        marker.ns = "ekf_slam";
        marker.id = i;
        marker.type = visualization_msgs::Marker::SPHERE;
        marker.action = visualization_msgs::Marker::ADD;
        marker.pose.position.x = mx;
        marker.pose.position.y = my;
        marker.pose.position.z = 0.;
        marker.pose.orientation.x = 0.;
        marker.pose.orientation.y = 0.;
        marker.pose.orientation.z = std::sin(angle / 2);
        marker.pose.orientation.w = std::cos(angle / 2);
        marker.scale.x = scale * x_len;
        marker.scale.y = scale * y_len;
        marker.scale.z = 0.1 * scale * (x_len + y_len);
        marker.color.a = 0.8; // Don't forget to set the alpha!
        marker.color.r = 0.0;
        marker.color.g = 1.0;
        marker.color.b = 0.0;

        markers.markers.push_back(marker);
    }// for all mpts

    return markers;
}

geometry_msgs::PoseWithCovarianceStamped ReflectorEKFSLAM::toRosPose()
{
    /* 转换带协方差的机器人位姿 */
    geometry_msgs::PoseWithCovarianceStamped rpose;
    rpose.header.frame_id = "world";

    rpose.pose.pose.position.x = mu_(0);
    rpose.pose.pose.position.y = mu_(1);
    rpose.pose.pose.orientation.x = 0.0;
    rpose.pose.pose.orientation.y = 0.0;
    rpose.pose.pose.orientation.z = std::sin(mu_(2) / 2);
    rpose.pose.pose.orientation.w = std::cos(mu_(2) / 2);

    rpose.pose.covariance.at(0) = sigma_(0, 0);
    rpose.pose.covariance.at(1) = sigma_(0, 1);
    rpose.pose.covariance.at(5) = sigma_(0, 2);
    rpose.pose.covariance.at(6) = sigma_(1, 0);
    rpose.pose.covariance.at(7) = sigma_(1, 1);
    rpose.pose.covariance.at(11) = sigma_(1, 2);
    rpose.pose.covariance.at(30) = sigma_(2, 0);
    rpose.pose.covariance.at(31) = sigma_(2, 1);
    rpose.pose.covariance.at(35) = sigma_(2, 2);
    return rpose;
}

void ReflectorEKFSLAM::normAngle ( double& angle )
{
    const static double PI = 3.1415926;
    static double Two_PI = 2.0 * PI;
    if( angle >= PI)
        angle -= Two_PI;
    if( angle < -PI)
        angle += Two_PI;
}

matched_ids ReflectorEKFSLAM::detectMatchedIds(const Observation& obs)
{
    matched_ids ids;
    if(obs.cloud_.empty())
    {
        std::cout << "should never reach here" << std::endl;
        exit(-1);
    }
    if (mu_.rows() == 3 && map_.reflector_map_.empty())
    {
        for (int i = 0; i < obs.cloud_.size(); ++i)
            ids.new_ids.push_back(i);

        std::cout << "\n >>>Reflector map is empty.\n";
        return ids;
    }

    auto point_transformed_to_global_frame = [&](const Eigen::Vector2f& p) -> Eigen::Vector2f{
        const float x = p.x() * std::cos(mu_(2)) - p.y() * std::sin(mu_(2)) + mu_(0);
        const float y = p.x() * std::sin(mu_(2)) + p.y() * std::cos(mu_(2)) + mu_(1);
        return Eigen::Vector2f(x,y);
    };

    const int M = (mu_.rows() - 3) / 2;
    const int M_ = map_.reflector_map_.size();
    for(int i = 0; i < obs.cloud_.size(); ++i)
    {
        const auto reflector = point_transformed_to_global_frame(obs.cloud_[i]);
        // Match with global map
        if (M_ > 0)
        {
            std::vector<std::pair<double, int>> distance_id;
            for (int j = 0; j < M_; ++j)
            {
                // Get global reflector covariance
                const Eigen::Matrix2d sigma = map_.reflector_map_coviarance_[j];
                const Eigen::Vector2f delta_state = map_.reflector_map_[j] - reflector;
                const auto delta_double_state = delta_state.cast<double>().transpose();
                // Calculate Ma distance
                const double dist = std::sqrt(delta_double_state * sigma * delta_double_state.transpose());
                distance_id.push_back({dist, j});
            }
            std::sort(distance_id.begin(), distance_id.end(),
                    [](const std::pair<double, int> &left,
                        const std::pair<double, int> &right) {
                        return left.first <= right.first;
                    });
            const auto best_match = distance_id.front();
            if (best_match.first < 0.05)
            {
              ids.map_obs_match_ids.push_back({i, best_match.second});
              continue;
            }
        }
        if (M > 0)
        {
            std::vector<std::pair<double, int>> distance_id;
            for (int j = 0; j < M; ++j)
            {
                Eigen::Vector2f global_reflector(mu_(3 + 2 * j),mu_(3 + 2 * j + 1));
                const Eigen::Matrix2d sigma = sigma_.block(3 + 2 * j, 3 + 2 * j, 2, 2);
                const Eigen::Vector2f delta_state = reflector - global_reflector;
                const auto delta_double_state = delta_state.cast<double>().transpose();
                // Calculate Ma distance
                // const double dist = std::sqrt(delta_double_state * sigma * delta_double_state.transpose());
                const double dist = std::sqrt(delta_double_state * delta_double_state.transpose());
                distance_id.push_back({dist, j});
            }
            std::sort(distance_id.begin(), distance_id.end(),
                    [](const std::pair<double, int> &left,
                        const std::pair<double, int> &right) {
                        return left.first <= right.first;
                    });
            const auto best_match = distance_id.front();
            if (best_match.first < 0.6)
            {
                ids.state_obs_match_ids.push_back({i, best_match.second});
                continue;
            }
        }
        ids.new_ids.push_back(i);
    }
    return ids;
}

void ReflectorEKFSLAM::loadFromVector(const std::vector<std::vector<double>>& vecs)
{
    PointCloud reflector_map;
    PointCloudCoviarance reflector_map_coviarance;
    if(vecs.empty())
        return;
    if(vecs[0].size() % 2 == 1)
        return;
    for(int i = 0; i < vecs[0].size() / 2; ++i)
    {
        reflector_map.push_back(Eigen::Vector2f(vecs[0][2 * i], vecs[0][2 * i + 1]));
    }
    for(int i = 0; i < vecs[1].size() / 4; ++i)
    {
        Eigen::Matrix2d p;
        p << vecs[0][4 * i], vecs[0][4 * i + 1], vecs[0][4 * i + 2], vecs[0][4 * i + 3];
        reflector_map_coviarance.push_back(p);
    }
    map_.reflector_map_ = reflector_map;
    map_.reflector_map_coviarance_ = reflector_map_coviarance;
}


