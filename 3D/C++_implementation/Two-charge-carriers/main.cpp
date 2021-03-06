/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%  Solving 2D Poisson + Drift Diffusion semiconductor eqns for a solar cell using
%                      Scharfetter-Gummel discretization
%
%                         Written by Timofey Golubev
%
%     This includes the 2D poisson equation and 2D continuity/drift-diffusion
%     equations using Scharfetter-Gummel discretization. The Poisson equation
%     is solved first, and the solution of potential is used to calculate the
%     Bernoulli functions and solve the continuity eqn's.
%
%   Boundary conditions for Poisson equation are:
%
%     -a fixed voltage at (x,0) and (x, Nz) defined by V_bottomBC
%      and V_topBC which are defining the  electrodes
%
%    -insulating boundary conditions: V(0,z) = V(1,z) and
%     V(0,N+1) = V(1,N) (N is the last INTERIOR mesh point).
%     so the potential at the boundary is assumed to be the same as just inside
%     the boundary. Gradient of potential normal to these boundaries is 0.
%
%   Matrix equations are AV*V = bV, Ap*p = bp, and An*n = bn where AV, Ap, and An are sparse matrices
%   (generated using spdiag), for the Poisson and continuity equations.
%   V is the solution for electric potential, p is the solution for hole
%   density, n is solution for electron density
%   bV is the rhs of Poisson eqn which contains the charge densities and boundary conditions
%   bp is the rhs of hole continuity eqn which contains net generation rate
%   and BCs
%
%     The code as is will calculate data for a JV curve
%     as well as carrier densities, current densities, and electric field
%     distributions of a generic solar cell made of an active layer and electrodes.
%     More equations for carrier recombination can be easily added.f
%
%     Photogeneration rate will be inputed from gen_rate.inp file
%     (i.e. the output of an optical model can be used) or an analytic expression
%     for photogeneration rate can be added to photogeneration.cpp. Generation rate file
%     should contain num_cell-2 number of entries in a single column, corresponding to
%     the the generation rate at each mesh point (except the endpoints).
%
%     The code can also be applied to non-illuminated devices by
%     setting photogeneration rate to 0.
%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#include <iostream>
#include <vector>
#include <iomanip>
#include <algorithm>   //allows to use fill and min
#include <fstream>
#include <chrono>
#include <string>
#include <time.h>
#include <fstream>
#include <string>

#include <Eigen/Sparse>
#include <Eigen/Dense>
#include<Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCholesky>
#include<Eigen/SparseQR>
#include <Eigen/OrderingMethods>
#include<Eigen/SparseLU>
#include <unsupported/Eigen/CXX11/Tensor>  //allows for 3D matrices (Tensors)

#include "constants.h"        //these contain physics constants only
#include "parameters.h"
#include "poisson.h"
#include "continuity_p.h"
#include "continuity_n.h"
#include "recombination.h"
#include "photogeneration.h"
#include "Utilities.h"


int main()
{
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();  //start clock timer
    Parameters params;    //params is struct storing all parameters
    params.Initialize();  //reads parameters from file

    const int num_cell = params.num_cell;   //create a local num_cell so don't have to type params.num_cell everywhere

    const int num_V = static_cast<int>(floor((params.Va_max-params.Va_min)/params.increment))+1;  //floor returns double, explicitely cast to int
    params.tolerance_eq = 100.*params.tolerance_i;
    const int N = params.num_cell -1;
    const int num_rows = N*N*N;  //number of rows in the solution vectors (V, n, p)
    //NOTE: num_rows is the same as num_elements

    std::ofstream JV;
    JV.open("JV.txt");  //note: file will be created inside the build directory

    //-------------------------------------------------------------------------------------------------------
    //Initialize other vectors
    //Will use indicies for n and p... starting from 1 --> since is more natural--> corresponds to 1st node inside the device...
    //NOTE: ALL THESE INCLUDE THE INTERIOR ELEMENTS ONLY
    std::vector<double> n(num_rows+ 1), p(num_rows+ 1), oldp(num_rows+ 1), newp(num_rows+ 1), oldn(num_rows+ 1), newn(num_rows+ 1);
    std::vector<double> oldV(num_rows+ 1), newV(num_rows+ 1), V(num_rows+ 1);

    //create matrices to hold the V, n, and p values (including those at the boundaries) according to the (x,z) coordinates.
    //allows to write formulas in terms of coordinates
    Eigen::VectorXd soln_Xd(num_rows);  //vector for storing solutions to the  sparse solver (indexed from 0, so only num_rows size)

    //For the following, only need gen rate on insides, so N+1 size is enough
    std::vector<double> Un(num_rows+1); //will store generation rate as vector, for easy use in rhs
    std::vector<double> Up = Un;
    Eigen::Tensor<double, 3> R_Langevin(N+1,N+1,N+1), PhotogenRate(N+1,N+1,N+1);
    Eigen::Tensor<double, 3> J_total_Z(num_cell+1, num_cell+1, num_cell+1), J_total_X(num_cell+1, num_cell+1, num_cell+1), J_total_Y(num_cell+1, num_cell+1, num_cell+1);                  //matrices for spacially dependent current

    Eigen::SparseMatrix<double> input; //for feeding input matrix into BiCGSTAB, b/c it crashes if try to call get matrix from the solve call.

    //std::cout << Eigen::nbThreads( ) << std::endl;  //displays the # of threads that will be used by Eigen--> mine displays 8, but doesn't seem like it's using 8.

//------------------------------------------------------------------------------------
    //Construct objects
    Poisson poisson(params);
    Recombo recombo(params);
    Continuity_p continuity_p(params);  //note this also sets up the constant top and bottom electrode BC's
    Continuity_n continuity_n(params);  //note this also sets up the constant top and bottom electrode BC's
    Photogeneration photogen(params, params.Photogen_scaling, params.GenRateFileName);
    Utilities utils;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::UpLoType::Lower, Eigen::AMDOrdering<int>> SCholesky; //Note using NaturalOrdering is much much slower

    Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> SQR;
    Eigen::SparseLU<Eigen::SparseMatrix<double> >  poisson_LU, cont_n_LU, cont_p_LU;
    Eigen::BiCGSTAB<Eigen::SparseMatrix<double>, Eigen::IncompleteLUT<double>> BiCGStab_solver;  //BiCGStab solver object

    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::UpLoType::Lower|Eigen::UpLoType::Upper > cg;


//--------------------------------------------------------------------------------------------
    //Define boundary conditions and initial conditions. Note: electrodes are at the top and bottom.
    double Va = 0;
    poisson.set_V_bottomBC(params, Va);
    poisson.set_V_topBC(params, Va);

    //Initial conditions
    //std::vector<double> diff;
    //for (int x = 0; x <= num_cell; x++)
       //diff[x] = (poisson.get_V_topBC()[x] - poisson.get_V_bottomBC()[x])/num_cell;    //note, the difference can be different at different x values..., diff is in Z directiont

    //for now assume diff is constant everywhere...
    double diff = (poisson.get_V_topBC(0,0) - poisson.get_V_bottomBC(0,0))/num_cell;  //this is  calculated correctly

    int index = 0;
    for (int k = 1; k <= N; k++) {
        index++;
        V[index] = poisson.get_V_bottomBC(0,0) + diff*k;   //for now just  use 1 pt on bottom BC, since is uniform anyway
        for (int i = 2; i <= N*N; i++) {//  %elements along the x and y directions assumed to have same V
            index++;
            V[index] = V[index-1];
        }
    }

    //side BCs, insulating BC's
    poisson.set_V_leftBC_X(V);
    poisson.set_V_rightBC_X(V);
    poisson.set_V_leftBC_Y(V);
    poisson.set_V_rightBC_Y(V);

    //Fill n and p with initial conditions (need for error calculation)
    double min_dense = continuity_n.get_n_bottomBC(1,1) < continuity_p.get_p_topBC(1,1) ? continuity_n.get_n_bottomBC(1,1):continuity_p.get_p_topBC(1,1);  //this should be same as std::min  fnc which doesn't work for some reason
    //double min_dense = std::min (continuity_n.get_n_bottomBC(1,1), continuity_p.get_p_topBC(1,1));  //Note: I defined the get fnc to take as arguments the i,j values... //Note: the bc's along bottom and top are currently uniform, so index, doesn't really matter.
    for (int i = 1; i<= num_rows; i++) {
        n[i] = min_dense;
        p[i] = min_dense;
    }

    //Convert the n and p to n_matrix and p_matrix
    continuity_n.to_matrix(n);
    continuity_p.to_matrix(p);

    poisson.setup_matrix();  //outside of loop since matrix never changes

    //////////////////////MAIN LOOP////////////////////////////////////////////////////////////////////////////////////////////////////////

    int iter, not_cnv_cnt, Va_cnt;
    bool not_converged;
    double error_np, old_error;  //this stores max value of the error and the value of max error from previous iteration
    std::vector<double> error_np_vector(num_rows+1);  //note: since n and p solutions are in vector form, can use vector form here also

    for (Va_cnt = 0; Va_cnt <= num_V +1; Va_cnt++) {  //+1 b/c 1st Va is the equil run
        not_converged = false;
        not_cnv_cnt = 0;
        if (params.tolerance > 1e-5)
            std::cerr<<"ERROR: Tolerance has been increased to > 1e-5" <<std::endl;

        if (Va_cnt==0) {
            params.use_tolerance_eq();  //relaxed tolerance for equil. run
            params.use_w_eq();
            Va = 0;
        }
        else {
            Va = params.Va_min+params.increment*(Va_cnt-1);
        }
        if (Va_cnt == 1) {
            params.use_tolerance_i();  //reset tolerance back
            params.use_w_i();
            PhotogenRate = photogen.getPhotogenRate();    //otherwise PhotogenRate is pre-initialized to 0 in this main.cpp when declared
        }
        std::cout << "Va = " << Va <<std::endl;

        //Reset top and bottom BCs (outside of loop b/c don't change iter to iter)
        poisson.set_V_bottomBC(params, Va);
        poisson.set_V_topBC(params, Va);

        //-----------------------------------------------------------
        error_np = 1.0;
        iter = 0;

        while (error_np > params.tolerance) {
            //std::cout << "Va " << Va <<std::endl;

            //-----------------Solve Poisson Equation------------------------------------------------------------------     
            poisson.set_rhs(n, p);  //this finds netcharge and sets rhs
            //std::cout << poisson.get_sp_matrix() << std::endl;
            oldV = V;



            if (iter == 0) { //INSTEAD OF HAVING IF here, can move these 2 lines, outside of the loop
                poisson_LU.analyzePattern(poisson.get_sp_matrix());  //by doing only on first iter, since pattern never changes, save a bit cpu
                poisson_LU.factorize(poisson.get_sp_matrix());
            }
            soln_Xd = poisson_LU.solve(poisson.get_rhs());


/*
            if (iter == 0) {  //This is slower than  LU
                SCholesky.analyzePattern(poisson.get_sp_matrix());
                SCholesky.factorize(poisson.get_sp_matrix());         //since numerical values of Poisson matrix don't change for 1 set of BC's, can factorize, just on 1st iter
            }
            soln_Xd = SCholesky.solve(poisson.get_rhs());
            */
            //std::cout << soln_Xd << std::endl;
             //std::cout << "Poisson solver error " << poisson.get_sp_matrix() * soln_Xd - poisson.get_rhs() << std::endl;

            //RECALL, I am starting my V vector from index of 1, corresponds to interior pts...
            for (int i = 1; i<=num_rows; i++) {
                newV[i] = soln_Xd(i-1);   //fill VectorXd  rhs of the equation
            }

            //Mix old and new solutions for V
            if (iter > 0)
                V  = utils.linear_mix(params, newV, oldV);
            else
                V = newV;

            //update side BC's and V_matrix
            poisson.set_V_leftBC_X(V);
            poisson.set_V_rightBC_X(V);
            poisson.set_V_leftBC_Y(V);
            poisson.set_V_rightBC_Y(V);
            poisson.to_matrix(V);

            //------------------------------Calculate Net Generation Rate----------------------------------------------------------

            //R_Langevin = recombo.ComputeR_Langevin(params,n,p);
            //FOR NOW CAN USE 0 FOR R_Langevin

            if (Va_cnt > 0) {
                for (int i = 1; i <= num_rows; i++) {
                    Un[i] = params.Photogen_scaling;  //This is what was used in Matlab version for testing.   photogen.getPhotogenRate()(i,j); //- R_Langevin(i,j);
                }
                Up = Un;
            }

            //--------------------------------Solve equations for n and p------------------------------------------------------------ 

            continuity_n.setup_eqn(poisson.get_V_matrix(), Un, n);
            oldn = n;

            //std::chrono::high_resolution_clock::time_point start2 = std::chrono::high_resolution_clock::now();  //start clock timer

            if (iter == 0 ) //can move this outside of the loop, instead of using if here...
                cont_n_LU.analyzePattern(continuity_n.get_sp_matrix());  //by doing only on first iter, since pattern never changes, save a bit cpu
            cont_n_LU.factorize(continuity_n.get_sp_matrix());  //need to do on each iter, b/c matrix elements change
            soln_Xd = cont_n_LU.solve(continuity_n.get_rhs());

            //std::chrono::high_resolution_clock::time_point finish2 = std::chrono::high_resolution_clock::now();
            //std::chrono::duration<double> time2 = std::chrono::duration_cast<std::chrono::duration<double>>(finish2-start2);
            //std::cout << "CPU time = " << time2.count() << std::endl;

            //std::cout << "solver error " << continuity_n.get_sp_matrix() * soln_Xd - continuity_n.get_rhs() << std::endl;
/*
            input = continuity_n.get_sp_matrix();
            if (iter == 0)
                BiCGStab_solver.analyzePattern(input);
            BiCGStab_solver.factorize(input);  //this computes preconditioner, if use along with analyzePattern (for 1st iter)
            //BiCGStab_solver.compute(input);  //this computes the preconditioner.
            soln_Xd = BiCGStab_solver.solve(continuity_n.get_rhs());
            //std::cout << soln_Xd << std::endl;
            */

            //std::cout << "#iterations:     " << solver.iterations() << std::endl;
            //std::cout << "estimated error: " << BiCGStab_solver.error()      << std::endl;

            //save results back into n std::vector. RECALL, I am starting my V vector from index of 1, corresponds to interior pts...
            for (int i = 1; i<=num_rows; i++) {
                newn[i] = soln_Xd(i-1);   //fill VectorXd  rhs of the equation
            }

            //-------------------------------------------------------
            continuity_p.setup_eqn(poisson.get_V_matrix(), Up, p);
            //std::cout << continuity_p.get_sp_matrix() << std::endl;   //Note: get rhs, returns an Eigen VectorXd
            oldp = p;
/*
            input = continuity_p.get_sp_matrix();
            if (iter == 0)
                BiCGStab_solver.analyzePattern(input);
            BiCGStab_solver.factorize(input);  //this computes preconditioner, if use along with analyzePattern (for 1st iter)
            //BiCGStab_solver.compute(input);  //this computes the preconditioner..compute(input);
            soln_Xd = BiCGStab_solver.solve(continuity_p.get_rhs());
*/

            if (iter == 0 )
                cont_p_LU.analyzePattern(continuity_p.get_sp_matrix());
            cont_p_LU.factorize(continuity_p.get_sp_matrix());
            soln_Xd = cont_p_LU.solve(continuity_p.get_rhs());


            //save results back into n std::vector. RECALL, I am starting my V vector from index of 1, corresponds to interior pts...
            for (int i = 1; i<=num_rows; i++) {
                newp[i] = soln_Xd(i-1);   //fill VectorXd  rhs of the equation
            }

            //------------------------------------------------

            //if get negative p's or n's set them = 0
            for (int i = 1; i <= num_rows; i++) {
                if (newp[i] < 0.0) newp[i] = 0;
                if (newn[i] < 0.0) newn[i] = 0;
            }

            //calculate the error
            old_error = error_np;

            //THIS CAN BE MOVED TO A FUNCTION IN UTILS
            for (int i = 1; i <= num_rows; i++) {
                if (newp[i]!=0 && newn[i] !=0) {
                    error_np_vector[i] = (abs(newp[i]-oldp[i]) + abs(newn[i]-oldn[i]))/abs(oldp[i]+oldn[i]);
                }
            }
            error_np = *std::max_element(error_np_vector.begin()+1,error_np_vector.end());  //+1 b/c we are not using the 0th element
            std::fill(error_np_vector.begin(), error_np_vector.end(),0.0);  //refill with 0's so have fresh one for next iter

            //auto decrease w if not converging
            if (error_np >= old_error)
                not_cnv_cnt = not_cnv_cnt+1;
            if (not_cnv_cnt > 2000) {
                params.reduce_w();
                params.relax_tolerance();
                not_cnv_cnt = 0;
            }

            p = utils.linear_mix(params, newp, oldp);
            n = utils.linear_mix(params, newn, oldn);

            //Apply side continuity equation  BC's
            //WE ARE UPDATING BC'S here b/c we need them for setting up the n and p matrices below
            //Bc's are also updated when setup continuity eqn.
            continuity_n.set_n_leftBC_X(n);  //this sets both x and y left BC's
            continuity_n.set_n_rightBC_X(n);
            continuity_n.set_n_leftBC_Y(n);  //this sets both x and y left BC's
            continuity_n.set_n_rightBC_Y(n);

            continuity_p.set_p_leftBC_X(p);
            continuity_p.set_p_rightBC_X(p);
            continuity_p.set_p_leftBC_Y(p);
            continuity_p.set_p_rightBC_Y(p);
            //note: top and bottom BC's don't need to be changed for now, since assumed to be constant... (they are set when initialize continuity objects)

            //Convert the n and p to n_matrix and p_matrix
            continuity_n.to_matrix(n);
            continuity_p.to_matrix(p);

            //std::cout << error_np << std::endl;
            //std::cout << "weighting factor = " << params.w << std::endl << std::endl;

            iter = iter+1;
        }

        //-------------------Calculate Currents using Scharfetter-Gummel definition--------------------------

        continuity_n.calculate_currents();
        continuity_p.calculate_currents();

        J_total_Z = continuity_p.get_Jp_Z() + continuity_n.get_Jn_Z();
        J_total_X = continuity_p.get_Jp_X() + continuity_n.get_Jn_X();
        J_total_Y = continuity_p.get_Jp_Y() + continuity_n.get_Jn_Y();

        //---------------------Write to file----------------------------------------------------------------
        utils.write_details(params, Va, poisson.get_V_matrix(), p, n, J_total_Z, Un);
        if(Va_cnt >0) utils.write_JV(params, JV, iter, Va, J_total_Z);


    }//end of main loop

    JV.close();

    std::chrono::high_resolution_clock::time_point finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time = std::chrono::duration_cast<std::chrono::duration<double>>(finish-start);
    std::cout << "CPU time = " << time.count() << std::endl;

    return 0;
}
