#include <eigen3/Eigen/Dense>
#include <iostream>
#include <opencv2/core.hpp>
#include <random>
#include <vector>

#include <Eigen/Core>
#include <cmath>
#include <iostream>
#include <opencv2/core/core.hpp>

// g2o相关
#include <core/block_solver.h>
#include <core/eigen_types.h>
#include <core/optimization_algorithm_levenberg.h>
#include <solvers/dense/linear_solver_dense.h>
#include <types/slam3d/edge_se3.h>
#include "type_p2_line.h"

//然后引入核心控件中的求解器

//引入各种优化算法的头文件，这里有好多可以引进，用啥引用啥就是了
//#include <core/optimization_algorithm_dogleg.h>        //include进核心构件中的DL优化算法头文件
//#include <core/optimization_algorithm_gauss_newton.h>  //include进核心构件中的GN优化算法头文件
//#include <core/optimization_algorithm_levenberg.h>     //include进核心构件中的LM优化算法头文件

//引入求解器的求解方法,注意这里不是core文件中的，而是solvers中的稠密中的线性稠密求解器
//#include <solvers/dense/linear_solver_dense.h>

using namespace std;
using namespace Eigen;

vector<Matrix<double, 1, 6>> jacobians;
vector<double> fs;

vector<vector<double>> coeffs;
Sophus::SE3 trans;

vector<Vector3d> pts;

float LM_param_ = 1;
const float minGradient = 1e-4;
double lr = 1;
bool add_s = false;

// 1,double是误差的维度和类型, VertexSE3是连接的顶点的类型

bool LMOptimization() {
  int dimension = pts.size();
  cv::Mat matA(dimension, 6, CV_32F, cv::Scalar::all(0));
  cv::Mat matAt(6, dimension, CV_32F, cv::Scalar::all(0));
  cv::Mat matAtA(6, 6, CV_32F, cv::Scalar::all(0));
  cv::Mat matC = cv::Mat::eye(6, 6, CV_32F);
  matC = LM_param_ * matC;
  cv::Mat matB(dimension, 1, CV_32F, cv::Scalar::all(0));
  cv::Mat matAtB(6, 1, CV_32F, cv::Scalar::all(0));
  cv::Mat matX(6, 1, CV_32F, cv::Scalar::all(0));

  Eigen::Vector3d point_ori;
  vector<double> coeff;
  float totle_dis = 0;
  for (int i = 0; i < dimension; ++i) {
    coeff = coeffs[i];
    Eigen::RowVector4d c_coeff(coeff[0], coeff[1], coeff[2], 1.0);
    float d2 = coeff[3];
    totle_dis += d2;

    point_ori.x() = pts[i](0);
    point_ori.y() = pts[i](1);
    point_ori.z() = pts[i](2);

    Eigen::Matrix<double, 4, 6> lie_algebra_jacobin = Eigen::MatrixXd::Zero(4, 6);
    lie_algebra_jacobin.block<3, 3>(0, 0) = Eigen::MatrixXd::Identity(3, 3);
    lie_algebra_jacobin.block<3, 3>(0, 3) = -Sophus::SO3::hat(point_ori);
    Eigen::Matrix<double, 1, 6> line_jacobin = c_coeff * lie_algebra_jacobin;

    matA.at<float>(i, 0) = line_jacobin(0, 0);
    matA.at<float>(i, 1) = line_jacobin(0, 1);
    matA.at<float>(i, 2) = line_jacobin(0, 2);
    matA.at<float>(i, 3) = line_jacobin(0, 3);
    matA.at<float>(i, 4) = line_jacobin(0, 4);
    matA.at<float>(i, 5) = line_jacobin(0, 5);
    std::cout << "jacobian " << i << " is " << line_jacobin << std::endl;
    matB.at<float>(i, 0) = -lr * d2;
    //            std::cout<<"jacobin : "<<arx<<", "<<ary<<", "<<arz<<", "<<atx<<",
    //            "<<aty<<", "<<atz<<", "<<d2<<std::endl;
  }
  std::cout << "[1] totle dis : " << totle_dis << std::endl;
  //    if (totle_dis < totole_loss_) totole_loss_ = totle_dis;
  //    else return true;

  cv::transpose(matA, matAt);
  matAtA = matAt * matA;
  matAtB = matAt * matB;
  auto matLM = matAtA + matC;
  cv::solve(matLM, matAtB, matX, cv::DECOMP_QR);

  // 计算rho

  Sophus::SE3 update(Sophus::SO3(matX.at<float>(3, 0), matX.at<float>(4, 0), matX.at<float>(5, 0)),
                     Eigen::Vector3d(matX.at<float>(0, 0), matX.at<float>(1, 0), matX.at<float>(2, 0)));
  trans = update * trans;

  //    std::cout<<"revise : "<<matX.at<float>(0, 0)
  //            <<", "<<matX.at<float>(1, 0)
  //           <<", "<<matX.at<float>(2, 0)
  //          <<", "<<matX.at<float>(3, 0)
  //         <<", "<<matX.at<float>(4, 0)
  //        <<", "<<matX.at<float>(5, 0)<<std::endl;
  //  std::cout << "current trans is " << trans << "total dis" << totle_dis << std::endl;

  if (fabs(matX.at<float>(0, 0)) < minGradient && fabs(matX.at<float>(1, 0)) < minGradient && fabs(matX.at<float>(2, 0)) < minGradient &&
      fabs(matX.at<float>(3, 0)) < minGradient && fabs(matX.at<float>(4, 0)) < minGradient && fabs(matX.at<float>(5, 0)) < minGradient)
    return true;

  return false;
}

bool AddCornerConstraint(Vector3d &p0, Vector3d &p1, Vector3d &p2, int itorCount) {
  Eigen::Vector3d v12 = p2 - p1;
  Eigen::Vector3d v10 = p0 - p1;
  // 向量的2范数
  double v12_sq_norm = v12.squaredNorm();

  // 计算出距离的平方
  Eigen::Vector3d v12_10 = v12.cross(v10);
  double distance = v12_10.squaredNorm() / v12_sq_norm;

  double c = 2 / v12_sq_norm;
  Eigen::RowVector3d coeff_matrix = v12_10.transpose() * Sophus::SO3::hat(v12);
  coeff_matrix *= c;

  float s = 1;
  //  if (itorCount >= 5) {
  //    s = 1 - 1.8 * fabs(distance);
  //  }
  //  if (s <= 0.1) return false;
  //
  //  if (!add_s) {
  //    s = 1;
  //  }

  vector<double> coeff;
  coeff.push_back(coeff_matrix(0, 0));
  coeff.push_back(coeff_matrix(0, 1));
  coeff.push_back(coeff_matrix(0, 2));
  coeff.push_back(s * distance);
  coeff.push_back(distance);
  coeffs.push_back(coeff);

  pts.push_back(p0);
  return true;
}

bool NewAddCornerConstraint(Vector3d &p0, Vector3d &p1, Vector3d &p2, int itorCount) {
  Eigen::Vector3d current_p0 = trans * p0;
  Eigen::Vector3d v12 = p2 - p1;
  Eigen::Vector3d v10 = current_p0 - p1;
  //  std::cout << " current p0" << current_p0 << std::endl;
  // 向量的2范数
  double v12_sq_norm = v12.squaredNorm();

  // 计算出距离的平方
  Eigen::Vector3d v12_10 = v12.cross(v10);
  double distance = v12_10.squaredNorm() / v12_sq_norm;

  // 计算关于旋转量的雅各比

  Eigen::Matrix3d R = trans.rotation_matrix();
  Eigen::Matrix3d B = Sophus::SO3::hat(v12) * Sophus::SO3::hat(v12);
  Vector3d C = v10;
  double A = v12_sq_norm;
  Eigen::Matrix3d p_hat = Sophus::SO3::hat(p0);
  Eigen::RowVector3d j_r = (C.transpose() * B * R * p_hat + C.transpose() * B.transpose() * R * p_hat) / A;
  //  j_r(0, 0) = 0;
  //  j_r(0, 1) = 0;
  //  j_r(0, 2) = 0;

  float s = 1;
  //  if (itorCount >= 5) {
  //    s = 1 - 1.8 * fabs(distance);
  //  }
  //  if (s <= 0.1) return false;
  //
  //  if (!add_s) {
  //    s = 1;
  //  }

  // 计算关于平移量的雅各比
  Eigen::Vector3d D = (B * v10).transpose();
  Eigen::Vector3d E = v10.transpose() * B;
  Eigen::RowVector3d j_t = -(D + E) / v12_sq_norm;

  Matrix<double, 1, 6> jaco;
  jaco.block<1, 3>(0, 0) = j_t;
  jaco.block<1, 3>(0, 3) = j_r;
  static int cnt = 0;
  std::cout << "my jacobian" << cnt++ << " is" << jaco << std::endl;
  jacobians.push_back(jaco);
  fs.push_back(s * distance);
  pts.push_back(p0);
  //  float s = 1;
  //  if (itorCount >= 5) {
  //    s = 1 - 1.8 * fabs(distance);
  //  }
  //  if (s <= 0.1) return false;
  //
  //  vector<double> coeff;
  //  coeff.push_back(coeff_matrix(0, 0));
  //  coeff.push_back(coeff_matrix(0, 1));
  //  coeff.push_back(coeff_matrix(0, 2));
  //  coeff.push_back(s * distance);
  //  coeffs.push_back(coeff);

  return true;
}
bool NewLMOptimization() {
  int dimension = pts.size();
  cv::Mat matA(dimension, 6, CV_32F, cv::Scalar::all(0));
  cv::Mat matAt(6, dimension, CV_32F, cv::Scalar::all(0));
  cv::Mat matAtA(6, 6, CV_32F, cv::Scalar::all(0));
  cv::Mat matC = cv::Mat::eye(6, 6, CV_32F);
  matC = LM_param_ * matC;
  cv::Mat matB(dimension, 1, CV_32F, cv::Scalar::all(0));
  cv::Mat matAtB(6, 1, CV_32F, cv::Scalar::all(0));
  cv::Mat matX(6, 1, CV_32F, cv::Scalar::all(0));

  Eigen::Vector3d point_ori;
  vector<double> coeff;
  float totle_dis = 0;
  for (int i = 0; i < dimension; ++i) {
    //    coeff = coeffs[i];
    //    Eigen::RowVector4d c_coeff(coeff[0], coeff[1], coeff[2], 1.0);
    //    float d2 = coeff[3];
    totle_dis += fs[i];
    //
    //    point_ori.x() = pts[i](0);
    //    point_ori.y() = pts[i](1);
    //    point_ori.z() = pts[i](2);
    //
    //    Eigen::Matrix<double, 4, 6> lie_algebra_jacobin = Eigen::MatrixXd::Zero(4, 6);
    //    lie_algebra_jacobin.block<3, 3>(0, 0) = Eigen::MatrixXd::Identity(3, 3);
    //    lie_algebra_jacobin.block<3, 3>(0, 3) = -Sophus::SO3::hat(point_ori);
    //    Eigen::Matrix<double, 1, 6> line_jacobin = c_coeff * lie_algebra_jacobin;

    matA.at<float>(i, 0) = jacobians[i](0, 0);
    matA.at<float>(i, 1) = jacobians[i](0, 1);
    matA.at<float>(i, 2) = jacobians[i](0, 2);
    matA.at<float>(i, 3) = jacobians[i](0, 3);
    matA.at<float>(i, 4) = jacobians[i](0, 4);
    matA.at<float>(i, 5) = jacobians[i](0, 5);
    matB.at<float>(i, 0) = -lr * fs[i];
    //        std::cout<<"jacobin : "<<arx<<", "<<ary<<", "<<arz<<", "<<atx<<",
    //        "<<aty<<", "<<atz<<", "<<d2<<std::endl;
  }
  totle_dis /= dimension;
  std::cout << "[2] totle dis : " << totle_dis << std::endl;
  //    if (totle_dis < totole_loss_) totole_loss_ = totle_dis;
  //    else return true;

  cv::transpose(matA, matAt);
  matAtA = matAt * matA;
  matAtB = matAt * matB;
  auto matLM = matAtA + matC;
  cv::solve(matLM, matAtB, matX, cv::DECOMP_QR);

  Matrix<double, 6, 1> update;
  update(0, 0) = matX.at<float>(0, 0);
  update(1, 0) = matX.at<float>(1, 0);
  update(2, 0) = matX.at<float>(2, 0);
  update(3, 0) = matX.at<float>(3, 0);
  update(4, 0) = matX.at<float>(4, 0);
  update(5, 0) = matX.at<float>(5, 0);

  Vector3d update_v_r = update.block<3, 1>(3, 0);
  Vector3d update_v_t = update.block<3, 1>(0, 0);

  Sophus::SE3 update_rt(Sophus::SO3::exp(update_v_r), update_v_t);
  //  Sophus::SO3 update_r = ;
  //  trans.setRotationMatrix(trans.rotation_matrix() * update_r.matrix());
  //  trans.translation() = trans.translation() + update_v_t;
  trans = trans * update_rt;

  //    std::cout<<"revise : "<<matX.at<float>(0, 0)
  //            <<", "<<matX.at<float>(1, 0)
  //           <<", "<<matX.at<float>(2, 0)
  //          <<", "<<matX.at<float>(3, 0)
  //         <<", "<<matX.at<float>(4, 0)
  //        <<", "<<matX.at<float>(5, 0)<<std::endl;
  if (fabs(matX.at<float>(0, 0)) < minGradient && fabs(matX.at<float>(1, 0)) < minGradient && fabs(matX.at<float>(2, 0)) < minGradient &&
      fabs(matX.at<float>(3, 0)) < minGradient && fabs(matX.at<float>(4, 0)) < minGradient && fabs(matX.at<float>(5, 0)) < minGradient)
    return true;

  return false;
}

void sophus_se3_2_isometry3(Sophus::SE3 &tran_in) {
  Eigen::Isometry3d iso = Eigen::Isometry3d::Identity();
  iso.matrix() = tran_in.matrix();
}

int main(int argc, char *argv[]) {
  Eigen::Vector3d p1(0, 0, 0);
  Eigen::Vector3d p2(0, 1, 0);

  Eigen::Matrix3d real_r;
  //  real_r = Eigen::Matrix3d::Identity();
  real_r = Eigen::AngleAxisd(0, Eigen::Vector3d::UnitZ());
  Eigen::Vector3d real_t(0, 0, 0.1);

  Sophus::SE3 real_trans(real_r, real_t);

  Eigen::Matrix3d init_r;
  init_r = Eigen::AngleAxisd(0, Eigen::Vector3d::UnitZ());
  //  init_r = Eigen::Matrix3d::Identity();
  Eigen::Vector3d init_t(0, 0, 0.2);
  trans = Sophus::SE3(init_r, init_t);
  trans = trans.inverse();
  std::cout << "init trans is " << trans << std::endl;
  // 首先在p1,p2的线上面构造一些点
  uniform_real_distribution<double> u(-5, 5);  //随机数分布对象
  normal_distribution<double> n(0, 0);
  default_random_engine e;
  vector<Vector3d> pxs;

  // 定义优化问题
  typedef g2o::BlockSolver<g2o::BlockSolverTraits<6, 1>> Block;
  // 线性方程求解器
  Block::LinearSolverType *linearSolver = new g2o::LinearSolverDense<Block::PoseMatrixType>();
  Block *solver_ptr = new Block(linearSolver);

  // 梯度下降方法
  g2o::OptimizationAlgorithmLevenberg *solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
  g2o::SparseOptimizer optimizer;
  optimizer.setAlgorithm(solver);
  optimizer.setVerbose(true);

  //添加顶点
  VertexSE3 *v = new VertexSE3();
  v->setId(0);
  v->setEstimate(Eigen::Isometry3d::Identity());
  v->setFixed(false);
  optimizer.addVertex(v);

  for (int i = 0; i < 10; i++) {
    Eigen::Vector3d px = p1 + (p2 - p1) * u(e);
    Eigen::Vector3d px_(px(0), px(1), px(2));
    px_ = real_trans * px_;
    px(0) = px_(0) + n(e);
    px(1) = px_(1) + n(e);
    px(2) = px_(2) + n(e);
    //    std::cout << "insert pt" << px << std::endl;
    pxs.push_back(px);
    // 添加边
    Point2LineEdge *edge = new Point2LineEdge(px, p1, p2);
    edge->setId(i);
    edge->setVertex(0, v);
    // information必须设置
    edge->setInformation(Eigen::Matrix<double, 1, 1>(1));
    //    edge->setMeasurement(0);
    // 添加鲁棒核
    optimizer.addEdge(edge);
  }
  // 执行优化
  cout << "start optimization" << endl;

  Vector3d test_pt(0, 0.5, 0);
  test_pt = real_trans * test_pt;
  optimizer.initializeOptimization();
  optimizer.optimize(30);
  //  for (int i = 0; i < 30; i++) {
  //    optimizer.optimize(1);
  //    Sophus::SE3 estimate = v->estimate();
  //    Vector3d current_test = estimate * test_pt;
  //    std::cout << "current test at " << current_test << endl;
  //  }
  Eigen::Isometry3d estimate = v->estimate();
  Sophus::SE3 se3_estimate(estimate.rotation(), estimate.translation());
  test_pt = se3_estimate * test_pt;
  cout << "final estimate " << se3_estimate << endl;
  cout << "testpt at " << test_pt << endl;
  cout << "real trans is " << real_trans.inverse() << endl;
}