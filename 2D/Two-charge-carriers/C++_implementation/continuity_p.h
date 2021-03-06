#ifndef CONTINUITY_P_H
#define CONTINUITY_P_H

#include <vector>
#include <Eigen/Dense>
#include<Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCholesky>
#include<Eigen/SparseQR>
#include <Eigen/OrderingMethods>

#include "parameters.h"  //needs this to know what parameters is
#include "constants.h"

class Continuity_p
{

typedef Eigen::Triplet<double> Trp;  //allows to use Trp to refer to the Eigen::Triplet<double> type

public:   
    Continuity_p(const Parameters &params);

    //!Sets up the matrix equation Ap*p = bp for continuity equation for holes.
    //!\param V stores the voltage and is needed to calculate Bernoulli fnc.'s.
    //!\param Up stores the net generation rate, needed for the right hand side.
    //!\param p the hole density is needed to setup the boundary conditions.
    void setup_eqn(const Eigen::MatrixXd &V_matrix, const Eigen::MatrixXd &Up_matrix, const std::vector<double> &p);

    void calculate_currents();

    void Continuity_p::to_matrix(const std::vector<double> &p);

    //setters for BC's:
    //for left and right BC's, will use input from the n matrix to determine
    void set_p_topBC();
    void set_p_bottomBC();
    void set_p_leftBC(const std::vector<double> &p);
    void set_p_rightBC(const std::vector<double> &p);

    //getters
    Eigen::VectorXd get_rhs() const {return VecXd_rhs;}  //returns the Eigen object
    Eigen::SparseMatrix<double> get_sp_matrix() const {return sp_matrix;}
    Eigen::MatrixXd get_p_matrix() const {return p_matrix;}
    std::vector<double> get_p_topBC() const {return p_topBC;} //bottom and top are needed to set initial conditions
    std::vector<double> get_p_bottomBC() const {return p_bottomBC;}

    Eigen::MatrixXd get_Jp_X() const {return Jp_X;}
    Eigen::MatrixXd get_Jp_Z() const {return Jp_Z;}

    //The below getters can be useful for testing and debugging
    //std::vector<double> get_main_diag() const {return main_diag;}
    //std::vector<double> get_upper_diag() const {return upper_diag;}
    //std::vector<double> get_lower_diag() const {return lower_diag;}
    //std::vector<double> get_far_upper_diag() const {return far_upper_diag;}
    //std::vector<double> get_far_lower_diag() const {return far_lower_diag;}
    //Eigen::MatrixXd  get_Bp_posX() const {return Bp_posX;}
    //Eigen::MatrixXd  get_Bp_negX() const {return Bp_negX;}
    //Eigen::MatrixXd  get_Bp_posZ() const {return Bp_posZ;}
    //Eigen::MatrixXd  get_Bp_negZ() const {return Bp_negZ;}
    //Eigen::MatrixXd get_p_mob() const {return p_mob;}
    //std::vector<double> get_p_leftBC() const {return p_leftBC;}
    //std::vector<double> get_p_rightBC() const {return p_rightBC;}


private:
    std::vector<double> far_lower_diag;
    std::vector<double> far_upper_diag;
    std::vector<double> main_diag;
    std::vector<double> upper_diag;
    std::vector<double> lower_diag;
    std::vector<double> rhs;
    Eigen::MatrixXd p_mob;  //!Matrix storing the position dependent holeelectron mobility
    Eigen::VectorXd VecXd_rhs;  //rhs in Eigen object vector form, for sparse matrix solver
    Eigen::SparseMatrix<double> sp_matrix;
    Eigen::MatrixXd p_matrix;
    Eigen::MatrixXd Jp_Z;
    Eigen::MatrixXd Jp_X;

    std::vector<Trp> triplet_list;
    int trp_cnt;  //for counting the triplets
    double J_coeff;  //coefficient for curents eqn

    //Boundary conditions
    std::vector<double> p_leftBC, p_rightBC, p_bottomBC, p_topBC;

    //Bernoulli functions
    Eigen::MatrixXd Bp_posX;  //bernoulli (+dV_x)
    Eigen::MatrixXd Bp_negX;  //bernoulli (-dV_x)
    Eigen::MatrixXd Bp_posZ;  //bernoulli (+dV_z)
    Eigen::MatrixXd Bp_negZ;  //bernoulli (-dV_z)

    double Cp;
    int num_cell, num_elements; //so don't have to keep typing params.
    int N;

    //!Calculates the Bernoulli functions for dV in x direction and updates member arrays
    void Bernoulli_p_X(const Eigen::MatrixXd &V_matrix);

    //!Calculates the Bernoulli functions for dV in z direction and updates member arrays
    void Bernoulli_p_Z(const Eigen::MatrixXd &V_matrix);

    //matrix setup functions
    void set_far_lower_diag();
    void set_lower_diag();
    void set_main_diag();
    void set_upper_diag();
    void set_far_upper_diag();
    void Continuity_p::set_rhs(const Eigen::MatrixXd &Up_matrix);
};

#endif // CONTINUITY_P_H
