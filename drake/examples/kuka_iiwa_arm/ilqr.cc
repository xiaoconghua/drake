/// @file
///
/// Description!

#include <lcm/lcm-cpp.hpp>
#include <math.h>

#include "drake/common/drake_assert.h"
#include "drake/common/drake_path.h"
#include "drake/examples/kuka_iiwa_arm/iiwa_common.h"
#include "drake/multibody/parsers/urdf_parser.h"
#include "drake/multibody/rigid_body_tree.h"
#include "drake/gps_run_controller.hpp"
#include "drake/gps_controller_gains.hpp"


using Eigen::MatrixXd;
using Eigen::VectorXd;
using Eigen::VectorXi;
using drake::Vector1d;
using Eigen::Vector2d;
using Eigen::Vector3d;

const char* const kLcmRunControllerChannel = "GPS_RUN_CONTROLLER";


namespace drake {
namespace examples {
namespace kuka_iiwa_arm {
namespace {

const int kDof = 7;
const int T = 25;
const int numStates = 14;
const double dt = 0.001;
const double finite_differences_epsilon = 1e-4;
const int maxIterations = 10;
const double converganceThreshold = 0.000000000000000000000001;
const double terminalPosWeight = 0.01;
const double terminalVelWeight = 0.01;
const double MIN_VALUE = 0.000000000000001;
class iLQR {
 public:
  Eigen::MatrixXd k_;
  Eigen::MatrixXd K_;
  Eigen::VectorXd x0_;
  Eigen::MatrixXd X_;
  Eigen::VectorXd xt_;
  Eigen::MatrixXd U_;
  Eigen::MatrixXd Fm_;
  Eigen::MatrixXd fv_;
  double optimal_cost_;
  double current_cost_;


  bool update_rollout_ = true;
  double lambda_ = 1.0;
  double lambda_factor_ = 10.0;

  std::vector<MatrixXd> fx;
  std::vector<MatrixXd> fu;
  std::vector<MatrixXd> lxx;
  std::vector<MatrixXd> luu;
  std::vector<MatrixXd> lux;
  MatrixXd l;
  MatrixXd lx;
  MatrixXd lu;

  explicit iLQR(const RigidBodyTree<double>& tree)
      : tree_(tree)  {
    VerifyIiwaTree(tree);

    // traj_mat.resize(kNumJoints, T);
    l.resize(T,1);
    lx.resize(T,numStates);
    lu.resize(T,kDof);

    fx.clear();
    fu.clear();
    lxx.clear();
    luu.clear();
    lux.clear();

    k_ = MatrixXd(T,numStates);
    K_ = MatrixXd(T,numStates);

    for(int i = 0; i < T; i++) {
      fx.push_back(MatrixXd(numStates,numStates));
      fu.push_back(MatrixXd(numStates,numStates));
      lxx.push_back(MatrixXd(numStates,numStates));
      luu.push_back(MatrixXd(kDof,kDof));
      lux.push_back(MatrixXd(kDof,numStates));
    }
  }

  void update(){
    optimal_cost_ = trajectoryCost(X_,U_);

    //TODO optimal_cost_sum_

    for(int i = 0; i < maxIterations; i++) {
      if(update_rollout_ == true){
        forwardPass(x0_,U_);
      }

      update_rollout_ = false;

      backwardsPass();

      MatrixXd newU = computeNewU(x0_);
      current_cost_ = computeControlSequence(X_.row(0) , newU, X_);
      std::cout << "Current Cost: " << current_cost_ << " Optimal Cost Sum: " <<  optimal_cost_ << std::endl;

      //TODO current_cost_sum_
      if (current_cost_ < optimal_cost_) {
        lambda_ /= lambda_factor_;
        update_rollout_ = true;
        //TODO what do I reuse / save?
        U_ = newU;
        optimal_cost_ = current_cost_;

        if ((fabs((current_cost_ - optimal_cost_))/fabs(optimal_cost_)) < converganceThreshold) {
          break;
        }

      } else {
        lambda_ *= lambda_factor_;
      }
    }
  }

  MatrixXd computeNewU(MatrixXd x0) {
    MatrixXd U = MatrixXd::Zero(T,kDof);
    MatrixXd x = x0;

    for(int i =0; i < T-1;i++ ){
      U.row(i) = U_.row(i) + k_.row(i) + K_.row(i) * (x- X_.row(i));
      // std::cout << U.row(i) << std::endl;
    }
    return U;
  }

  double computeControlSequence(VectorXd x0, MatrixXd U, MatrixXd &newX) {
      newX = MatrixXd(T,numStates);
      newX.row(0) = x0;

      double cost = 0.0;

      for (int i = 0; i < (T - 1) ; i++) {
        newX.row(i+1) = forwardDynamics(newX.row(i) , U.row(i), i);
        cost += computeL(newX.row(i+1), U.row(i)) * dt;
        std::cout << "newX " << newX.row(i)  << std::endl;
        std::cout << "newX+1 " << newX.row(i+1)  << std::endl;
        std::cout << "U " << U.row(i)  << std::endl;
        std::cout << "c0: " << x0 << std::endl;
        std::cout << "c0st: " << cost << std::endl;

        std::cout << "-------------------------" << std::endl;

      }
      std::cout << "-------------------------" << std::endl;
      std::cout << "-------------------------" << std::endl;
      std::cout << "-------------------------" << std::endl;
      std::cout << "-------------------------" << std::endl;

      return cost;
  }

  void finiteDifferences(MatrixXd &A, MatrixXd &B, VectorXd x, VectorXd u, int t) {
      A = MatrixXd(numStates,numStates);
      B = MatrixXd(numStates, kDof);

      for(int i =0; i < numStates;i++) {
          MatrixXd xPlus = x + Eigen::VectorXd::Ones(x.size()) *  finite_differences_epsilon;
          MatrixXd xMinus = x -  Eigen::VectorXd::Ones(x.size()) *  finite_differences_epsilon;
          MatrixXd xPlusNew = forwardDynamics(xPlus, u, t);
          // std::cout << "OLD X " << xPlus << " NEW X " << xPlusNew << " u " << u << std::endl;
          MatrixXd xMinusNew = forwardDynamics(xMinus, u, t);
          MatrixXd diff = (xPlusNew - xMinusNew) / (2 * finite_differences_epsilon);
          A.col(i) = diff;
      }
      for(int i =0; i < kDof;i++) {
          MatrixXd uPlus = u + Eigen::VectorXd::Ones(u.size()) *  finite_differences_epsilon;
          MatrixXd uMinus = u -  Eigen::VectorXd::Ones(u.size()) *  finite_differences_epsilon;
          MatrixXd xPlusNew = forwardDynamics(x, uPlus, t);
          MatrixXd xMinusNew = forwardDynamics(x, uMinus, t);
          MatrixXd diff = (xPlusNew - xMinusNew) / (2 * finite_differences_epsilon);
          B.col(i) = diff;
      }
  }


  void forwardPass(VectorXd x0, MatrixXd U) {
    update_rollout_ = false;

    MatrixXd X(T, kDof);
    MatrixXd newX(T, kDof);
    VectorXd cost(kDof);
    computeControlSequence(x0 , U , newX);

    X_ = newX;

    fx.clear();
    fu.clear();
    lxx.clear();
    luu.clear();
    lux.clear();

    for(int i = 0; i < T; i++) {
      fx.push_back(MatrixXd(numStates,numStates));
      fu.push_back(MatrixXd(numStates,numStates));
      lxx.push_back(MatrixXd(numStates,numStates));
      luu.push_back(MatrixXd(kDof,kDof));
      lux.push_back(MatrixXd(kDof,numStates));
    }

    for (int i = 0; i < T-1; i++) {

      l(i) = computeCost( newX.row(i) , U.row(i), lx, lxx, lu, luu, lux, i);

      MatrixXd A(numStates,numStates);
      MatrixXd B(numStates,kDof);

      // TODO Why multiply bt dt
      l(i) *= dt;
      lx.row(i) *= dt;
      lxx.at(i) *= dt;
      lu.row(i) *= dt;
      luu.at(i) *= dt;
      lux.at(i) *= dt;

      finiteDifferences(A, B, newX.row(i), U.row(i), i);
      fx.at(i) = Eigen::MatrixXd::Identity(numStates,numStates) + A * dt;
      fu.at(i) = B * dt;

      // std::cout << i << " lx" << lx.row(i) << std::endl;
      // std::cout << i << " lxx" << lxx.at(i) << std::endl;
      // std::cout << i << " lu" << lu.row(i) << std::endl;
      // std::cout << i << " luu" << luu.at(i) << std::endl;
      // std::cout << i << " lux" << lux.at(i) << std::endl;
      // std::cout << i << " fx" << fx.at(i) << std::endl;
      // std::cout << i << " fu" << fu.at(i) << std::endl;

    }


    lu.row(T-1)  = 0.0 * U.row(T-1);
    luu.at(T-1) = 0.0 * Eigen::MatrixXd::Identity(kDof,kDof);
    lux.at(T-1) = Eigen::MatrixXd::Identity(kDof, numStates);

    l(T - 1) = computeFinalCost(newX.row(T-1), U.row(T-1), lx, lxx, T-1);
    // std::cout << "lxx" << lxx.at(T-1) << " , " << l(T-1)  << std::endl;
    lxx.at(T-1) = Eigen::MatrixXd::Identity(numStates,numStates);
    lx.row(T-1) = Eigen::VectorXd::Zero(numStates);
    fx.at(T-1) = Eigen::MatrixXd::Zero(numStates,numStates);
    fu.at(T-1) = Eigen::MatrixXd::Zero(numStates, kDof);

    // exit(0);
  }

   VectorXd forwardDynamics(VectorXd x, MatrixXd u, int t) {
     MatrixXd M;
     MatrixXd C;
     MatrixXd G;
     VectorXd qd_0(kDof);
     VectorXd q_0(kDof);
     double q_in[kDof];
     double qd_in[kDof];

     for(int i = 0; i < kDof; i++) {
       q_in[i] = x(i);
       qd_in[i] = x(i+kDof);
       qd_0(i) = qd_in[i];
       q_0(i) = q_in[i];
      //  std::cout << q_in[i] << " , " << q_0(i) << ": " << qd_in[i] << " , " << qd_0(i) << std::endl;
     }

     computeMCG(q_in, qd_in, M, C, G);

    //  std::cout << "---------------- C " << C << " G " << G << " M " << M << std::endl;

     MatrixXd qdd = M.inverse() * (u - C + G);
     VectorXd qd = qd_0 + qdd * dt;
     VectorXd q = q_0 + qd * dt + 0.5 * qdd * pow(dt,2);
    //  std::cout << " Minv " << M.inverse() << " ,u " << u << " ,C " << C << std::endl;
    //  std::cout << " qd_0 " << qd_0 << " q_0 " << q_0 << "  x, " << x << std::endl;
    //  std::cout << " qdd " << qdd << " ,qd " << qd << " ,q " << q <<  "--------------" <<std::endl;

     VectorXd returnVec(2*kDof);
     returnVec << q,qd;
    //  std::cout << returnVec << std::endl;
     return returnVec;
   }

  double computeL(VectorXd x, VectorXd u) {
    return u.squaredNorm();
  }


  double computeCost(VectorXd x, VectorXd u, MatrixXd &lx, std::vector<MatrixXd> &lxx, MatrixXd &lu, std::vector<MatrixXd> &luu, std::vector<MatrixXd> &lux, int i ) {
    double l = u.squaredNorm();
    lx.row(i) = Eigen::VectorXd::Zero(numStates);
    lxx.at(i) = Eigen::MatrixXd::Zero(numStates,numStates);
    lu.row(i)  = 2.0 * u;
    luu.at(i) = 2.0 * Eigen::MatrixXd::Identity(kDof,kDof);
    lux.at(i) = Eigen::MatrixXd::Zero(kDof, numStates);
    return l;
  }

  double computeFinalCost(MatrixXd x, MatrixXd u, MatrixXd &lx, std::vector<MatrixXd> &lxx, int t) {
    VectorXd q = x.block<1,kDof>(0,0);
    VectorXd qd = x.block<1,kDof>(0,kDof);
    VectorXd dis = q - xt_;

    for (int i =0 ; i < dis.size(); i++) {
      dis(i) = dis(i) + 180.0;
      dis(i) = fmod((double)dis(i) ,360.0);
      dis(i) = dis(i) - 180.0;
    }

    lxx.at(t) = Eigen::MatrixXd::Ones(numStates,numStates);
    double l = terminalPosWeight * dis.squaredNorm() + terminalVelWeight * qd.squaredNorm();
    // std::cout << l;
    // exit(0);
    return l;
  }


  void computeMCG(double * q_in, double * qd_in, MatrixXd &M, MatrixXd &C, MatrixXd &G) {
    // for (int i = 0; i < kDof; i++){
    //   std::cout << " qin " << q_in[i] << " qd_in " << qd_in[i] << std::endl;
    //
    // }
    double *qptr = &q_in[0];
    Eigen::Map<Eigen::VectorXd> q(qptr, kDof);
    double *qdptr = &qd_in[0];
    Eigen::Map<Eigen::VectorXd> qd(qdptr, kDof);
    KinematicsCache<double> cache = tree_.doKinematics(q, qd);
    const RigidBodyTree<double>::BodyToWrenchMap no_external_wrenches;


    /* Note that the 'dynamics bias term' \f$ C(q, v, f_\text{ext}) \f$ can be
    *computed by simply setting \f$ \dot{v} = 0\f$.
    * Note also that if only the gravitational terms contained in \f$ C(q, v,
    *f_\text{ext}) \f$ are required, one can set \a include_velocity_terms to
    *false.
    * Alternatively, one can pass in a KinematicsCache created with \f$ v = 0\f$
    *or without specifying the velocity vector.
    */
    C = tree_.dynamicsBiasTerm(cache, no_external_wrenches, true);
    G = tree_.dynamicsBiasTerm(cache, no_external_wrenches, false);
    M = tree_.massMatrix(cache);
    // std::cout << "+++++++++++++++++++ C " << C << " G " << G << " M " << M << " Minv: " << M.inverse() << "------------------" << std::endl;
  }


  void backwardsPass(){
    MatrixXd V = l.row(T-1);
    MatrixXd Vx = lx.row(T-1);
    MatrixXd Vxx = lxx.at(T-1);
    // std::cout << "Vxx!" << lxx.at(T-1) << std::endl;

    MatrixXd Qx(numStates , 1);
    MatrixXd Qu(kDof , 1);
    MatrixXd Qxx(numStates,numStates);
    MatrixXd Qux(kDof,numStates);
    MatrixXd Quu(kDof,kDof);
    MatrixXd Quu_inv;

    for (int i = 0; i < T - 1; i++) {
        Qx = lx.row(i) + fx.at(i).transpose() * Vx;
        Qu = lu.row(i) + fu.at(i).transpose() * Vx;
        Qxx = lxx.at(i) + fx.at(i).transpose() * (Vxx * fx.at(i));
        // std::cout <<i << "," << lux.size() << "," << Vxx.size() << " , " << fu.size() << "," << fx.size() << ":" << lux.at(i) << std::endl << Vxx<< std::endl;
        Qux = lux.at(i) + fu.at(i).transpose() * (Vxx * fx.at(i));
        Quu = luu.at(i) + fu.at(i).transpose() * (Vxx * fu.at(i));

        // Eigen::EigenSolver<MatrixXd> solver(Quu);
        // MatrixXd values = solver.eigenvalues();
        // MatrixXd vectors = solver.eigenvectors();

        Eigen::JacobiSVD<MatrixXd> svd(Quu, Eigen::ComputeThinU | Eigen::ComputeThinV);
        MatrixXd U = svd.matrixU();
        MatrixXd V = svd.matrixV();
        MatrixXd S = svd.singularValues();

        for(int j = 0; j < S.rows(); j++) {
          for(int k = 0; k < S.cols();k++ ){
            if(S(j,k) < 0.0 ) {
              S(j,k) = 0.0;
            }
            S(j,k) += lambda_;
            S(j,k) = 1.0 / S(j,k);
          }
        }

        //
        //
        // Quu_inv = vectors * (values.asDiagonal() * vectors.transpose());
        // std::cout << "quu inv"  << Quu_inv << std::endl;

        // S += lambda_ * MatrixXd::Ones(S.rows(),S.cols());
        // std::cout << "S2"  << S << std::endl;
        //
        // S = S.inverse();
        // std::cout << "S3"  << S << std::endl;
        //
      //   if(i < 5) {
      //   std::cout << "-------------" << i << "---------------" << std::endl;
      //   std::cout << "Quu "  << Quu << std::endl;
      //   std::cout << "Qu "  << Qu << std::endl;
      //   std::cout << "lu "  << lu.row(i) << std::endl;
      //
      //   std::cout << "U "  << U << std::endl;
      //   std::cout << "V "  << V << std::endl;
      //   std::cout << "S "  << S << std::endl;
      //   std::cout << "Quu_inv "  << Quu_inv << std::endl;
      //   std::cout << "k_ "  << k_.row(i) << std::endl;
      //   std::cout << "K_ "  << K_.row(i) << std::endl;
      //   std::cout << "Vx "  << Vx << std::endl;
      //   std::cout << "Vxx "  << Vxx << std::endl;
      //   std::cout << "fu  "  << fu.at(i) << std::endl;
      //   std::cout << "luu  "  << luu.at(i) << std::endl;
      //   std::cout << "fx  "  << fx.at(i) << std::endl;
      //
      // }




        Quu_inv = U * (S.asDiagonal() * V.transpose());
        k_.row(i) = - Quu_inv * Qu;
        K_.row(i) = - Quu_inv * Qux;

        Vx = Qx - K_.row(i).transpose() * (Quu * k_.row(i));
        Vxx = Qxx - K_.row(i).transpose() * (Quu * K_.row(i));
    }

  }

 private:

   double trajectoryCost(MatrixXd X, MatrixXd U) {
     double cost = 0.0;

     for(int i =0; i < T;i++) {
       cost += computeL(X.row(i), U.row(i)) * dt;
     }
     return cost;
   }


   lcm::LCM lcm_;
   const RigidBodyTree<double>& tree_;
};

int do_main(int argc, const char* argv[]) {

  auto tree = std::make_unique<RigidBodyTree<double>>();
  parsers::urdf::AddModelInstanceFromUrdfFileToWorld(
      GetDrakePath() + "/examples/kuka_iiwa_arm/urdf/iiwa14_estimated_params_fixed_gripper.urdf",
      multibody::joints::kFixed, tree.get());


  iLQR ilqr(*tree);
  ilqr.X_ = Eigen::MatrixXd::Zero(50,14);
  ilqr.U_ = Eigen::MatrixXd::Zero(50,7);
  ilqr.X_ += 0.3 * Eigen::MatrixXd::Ones(50,14);
  ilqr.U_ += 0.01 * Eigen::MatrixXd::Ones(50,7);

  ilqr.x0_ = Eigen::VectorXd(14) + 0.1 * Eigen::VectorXd::Ones(14);
  for(int i = kDof; i < 2* kDof; i ++) {
    ilqr.x0_(i) = 0.0;
  }
  ilqr.xt_.resize(7);
  ilqr.xt_ << 0.5,0.5,0.5,0.5,0.5,0.5,0.5;
  ilqr.update();


  lcm::LCM lcm_;
  gps_run_controller cmd;
  cmd.numTimeSteps = 25;
  cmd.numStates = 14;

  for(int i =0; i <cmd.numTimeSteps; i++ ){
    gps_controller_gains K;
    gps_controller_gains k;

    K.numStates = cmd.numStates;
    k.numStates = cmd.numStates;
    for(int j = 0; j < cmd.numStates; j++){
      K.values[j] = ilqr.K_(i,j);
      k.values[j] = ilqr.k_(i,j);

    }
    cmd.K[i] = K;
    cmd.k[i] = k;

  }
  std::cout << "sdfsdfsdf";
  lcm_.publish(kLcmRunControllerChannel, &cmd);
  std::cout << "sddfdfdfdfddddddddddddddddddddddddddddddfsdfsdf";


  // double q_in[7] = {0,-0.05,0,0,0,0,0};
  // double qd_in[7] = {0,0,0,0,0,0,0};
  // MatrixXd M;
  // MatrixXd C;
  // MatrixXd G;
  // MatrixXd G_old;
  //
  // for (int i = 0; i < 10; i ++) {
  //   ilqr.computeMCG(q_in,qd_in,M,C,G);
  //   q_in[1] += 0.01;
  //   if(i>=1){
  //     std::cout << "G " << G(1) - G_old(1) << std::endl;
  //   }
  //   G_old = G;
  // }
  // ilqr.computeMCG(q_in,qd_in,M,C,G);
  //
  // std::cout << "C " << C << std::endl;
  // std::cout << "G " << G << std::endl;
  // std::cout << "M " << M << std::endl;
  //
  // ilqr.update();
  // RobotPlanRunner runner(*tree);
  // runner.Run();

  return 0;
}

}  // namespace
}  // namespace kuka_iiwa_arm
}  // namespace examples
}  // namespace drake

int main(int argc, const char* argv[]) {
  return drake::examples::kuka_iiwa_arm::do_main(argc, argv);
}
