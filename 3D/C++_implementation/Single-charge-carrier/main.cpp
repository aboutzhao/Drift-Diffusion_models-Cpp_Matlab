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

#include <omp.h>

#define EIGEN_USE_MKL_ALL  //is for Intel MKL

//#define EIGEN_NO_DEBUG   //this should turn off eigen asserts, //MAKES NO PERFORMANCE DIFFERENCE!, is I think auto turned off in release mode
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
//#include "continuity_n.h"
//#include "recombination.h"
//#include "photogeneration.h"
#include "Utilities.h"

#include "mkl.h"


int main()
{

    //trivial MKL function call for testing
    vcAbs(0, 0, 0);  //MY MKL linking works!!, b/c otherwise it wouldn't recognize this function!

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();  //start clock timer
    Parameters params;    //params is struct storing all parameters
    params.Initialize();  //reads parameters from file

    const int num_cell_x = params.num_cell_x;   //create a local num_cell so don't have to type params.num_cell everywhere
    const int num_cell_y = params.num_cell_y;
    const int num_cell_z = params.num_cell_z;

    const int num_V = static_cast<int>(floor((params.Va_max-params.Va_min)/params.increment))+1;  //floor returns double, explicitely cast to int
    params.tolerance_eq = params.tolerance_i;
    const int Nx = params.Nx;
    const int Ny = params.Ny;
    const int Nz = params.Nz;
    const int num_rows = (Nx+1)*(Ny+1)*(Nz+1);  //number of rows in the solution vectors (V, n, p)
    //NOTE: num_rows is the same as num_elements.
    //NOTE: we include the top BC inside the matrix and solution vectors to allow in future to use mixed BC's there.

    std::ofstream JV;
    JV.open("JV.txt");  //note: file will be created inside the build directory

    //-------------------------------------------------------------------------------------------------------
    //Initialize other vectors
    //WILL INDEX FROM 0, b/c that's what Eigen library does.
    //Note: these are Tensor type, so can use reshape on them, but these all are just a single column (i.e. the soln column from the matrix eqn).
    Eigen::Tensor<double, 3> p(num_rows, 1, 1), oldp(num_rows, 1, 1), newp(num_rows, 1, 1);
    Eigen::Tensor<double, 3> oldV(num_rows, 1, 1), newV(num_rows, 1, 1), V(num_rows, 1, 1);

    //create matrices to hold the V, n, and p values (including those at the boundaries) according to the (x,z) coordinates.
    //allows to write formulas in terms of coordinates
    Eigen::VectorXd soln_V(num_rows), soln_p(num_rows);  //vector for storing solutions to the  sparse solver (indexed from 0)
    Eigen::VectorXd p_Xd(num_rows), V_Xd(num_rows);  //Eigen Vector_Xds for the initial buesses to bicgstab method
    //For the following, only need gen rate on insides, so N+1 size is enough
    std::vector<double> Up(num_rows); //will store generation rate as vector, for easy use in rhs

    //Eigen::Tensor<double, 3> R_Langevin(N+1,N+1,N+1), PhotogenRate(N+1,N+1,N+1);
    Eigen::Tensor<double, 3> J_total_Z(num_cell_x+1, num_cell_y+1, num_cell_z+1), J_total_X(num_cell_x+1, num_cell_y+1, num_cell_z+1), J_total_Y(num_cell_x+1, num_cell_y+1, num_cell_z+1);  //we want the indices of J to correspond to the real x,y,z values..., for convinience                //matrices for spacially dependent current
    Eigen::Tensor<double, 3> V_matrix(num_cell_x, num_cell_y, num_cell_z), temp_permuted; //indexed from 0, so just num_cell....//temp_permuted is needed b/c it can't do in place permutations...., need to save to another tensor!  //Note: is actually a Tensor in Eigen
    Eigen::Tensor<double, 3> fullV(num_cell_x+1, num_cell_y+1, num_cell_z+1), fullp(num_cell_x+1, num_cell_y+1, num_cell_z+1);  //for storing all the values in device, including bndrys
    Eigen::SparseMatrix<double> input; //for feeding input matrix into BiCGSTAB, b/c it crashes if try to call get matrix from the solve call.

    //test if openmp is working
    //omp_set_num_threads(8);   //this can allow to set the number of threads that will be used

    std::cout << Eigen::nbThreads() << std::endl;  //displays the # of threads that will be used by Eigen--> mine displays 8, but doesn't seem like it's using 8.

    //this will only make sense if within a region of code which is multithreaded
//    int nthreads = omp_get_num_threads();  //SAYS ONLY 1 THREAD IS AVAILABLE--> MIGHT HAVE AN ISSUE HERE!
//    std::cout << nthreads << " treads available " << std::endl;
//------------------------------------------------------------------------------------
    //Construct objects
    Poisson poisson(params);  //so it can't construct the poisson object
    //Recombo recombo(params);
    Continuity_p continuity_p(params);  //note this also sets up the constant top and bottom electrode BC's

    //Continuity_n continuity_n(params);  //note this also sets up the constant top and bottom electrode BC's
    //Photogeneration photogen(params, params.Photogen_scaling, params.GenRateFileName);
    Utilities utils;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::UpLoType::Lower, Eigen::AMDOrdering<int>> SCholesky; //Note using NaturalOrdering is much much slower

    Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> SQR;
    Eigen::SparseLU<Eigen::SparseMatrix<double> >  poisson_LU, cont_n_LU, cont_p_LU;
    //Eigen::BiCGSTAB<Eigen::SparseMatrix<double>, Eigen::IncompleteLUT<double>> BiCGStab_solver;  //BiCGStab solver object/ /USING THIS PRECONDITIONER IS WAY TOO SLOW FOR LARGE SYSTEMS!
    Eigen::BiCGSTAB<Eigen::SparseMatrix<double>, Eigen::DiagonalPreconditioner<double>> BiCGStab_solver;   //NOTE: WORKS MUCH FASTER WITH DIAGONAL PRECONDITIONER, THAN the IncompleteLUT preconditioner!!--> probably b/c
    //Eigen::BiCGSTAB<Eigen::SparseMatrix<double>, Eigen::IdentityPreconditioner> BiCGStab_solver;  //try with Identity preconditioner, the simplest trivial one
    BiCGStab_solver.setTolerance(1e-14); //set the tolerance explicitely, so matches Matlab's tolerance

    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::UpLoType::Lower|Eigen::UpLoType::Upper > cg;


//--------------------------------------------------------------------------------------------
    //Define boundary conditions and initial conditions. Note: electrodes are at the top and bottom.
    double Va = params.Va_min;
    poisson.set_V_bottomBC(params, Va);
    poisson.set_V_topBC(params, Va);  //THESE CAN BE MOVED TO BE DONE WITHIN constructor of Poisson object

    //Initial conditions
    //for now assume diff is constant everywhere...
    double diff = (poisson.get_V_topBC(0,0) - poisson.get_V_bottomBC(0,0))/num_cell_z;  //this is  calculated correctly

    //Note: B/C I must fill from 0, and don't want to include bottomBC in V matrix, b/c is not part of V
    for (int k = 1; k <= Nz+1; k++) {
        for (int i = 1; i <= Nx+1; i++) {
            for (int j = 1; j <= Ny+1; j++) {
                V_matrix(i-1, j-1, k-1) = poisson.get_V_bottomBC(i,j) +  diff*(k);  //-1's b/c fill from 0
            }
        }
    }

    //need to permute the matrix, to be consistent with the z,y,x ordering which I use for the matrices when solving.
    //NOTE: for Tensor shuffle to work, NEED TO EXPLICTELY CREATE AN ARRAY--> this isn't clear from the documentation
    Eigen::array<ptrdiff_t, 3> permutation = {{2,1,0}}; //array should HAVE THE SPECIFIC type:  ptrdiff_t  ==> used for pointer arithmetic and array indexing.
    //ptrdiff_t is the signed integer type of the result of subtracting 2 pointers.

    temp_permuted = V_matrix.shuffle(permutation);  //shuffle permuts the tensor. Note: dimensions are indexed from 0. This is supposed to swap x and z values...
    V_matrix = temp_permuted;
    //Returns a copy of the input tensor whose dimensions have been reordered according to the specified permutation. The argument shuffle is an array of Index values. Its size is the rank of the input tensor. It must contain a permutation of 0, 1, ..., rank - 1.
    //works to here
    //NOTE: IT CAN'T DO AN INPLACE PERMUTATION!!!--> need to rename the variable!!!

    //reshape the matrix to a single column. //Note: even though reshaping to a column, V still must be a TENSOR type for this to work!
     Eigen::array<ptrdiff_t, 3> reshape_sizes = {{num_rows, 1, 1}};  //need to only go to num_rows..., b/c it fills from 0 !!
     V = V_matrix.reshape(reshape_sizes);  //Note: even though reshaping to a column, V still must be a TENSOR type for this to work!

     //prepare initial guess, for 1st iteration of bicgstab
     for (int i = 0; i < num_rows; i++) {
         V_Xd(i) = V(i,0,0);
         soln_V(i) = V(i,0,0); // do this since we are using soln_V for the inital guess
     }

    //Fill p with initial conditions (need for error calculation)
    double min_dense = continuity_p.get_p_bottomBC(1,1) < continuity_p.get_p_topBC(1,1) ? continuity_p.get_p_bottomBC(1,1):continuity_p.get_p_topBC(1,1);  //this should be same as std::min  fnc which doesn't work for some reason
    //double min_dense = std::min (continuity_n.get_n_bottomBC(1,1), continuity_p.get_p_topBC(1,1));  //Note: I defined the get fnc to take as arguments the i,j values... //Note: the bc's along bottom and top are currently uniform, so index, doesn't really matter.
    for (int i = 0; i < num_rows; i++) {
        p(i,0,0) = min_dense;  //NOTE: p is tensor now, so need () to access elements
    }

    //prepare initial guess, for 1st iteration of bicgstab
    for (int i = 0; i < num_rows; i++) {
        p_Xd(i) = p(i,0,0);  //this is working correctly
        soln_p(i) = p(i,0,0); // do this since we are using soln_p for the inital guess
    }

    //Convert the p to p_matrix
    Eigen::array<ptrdiff_t, 3> to_matrix_sizes{{Nz+1, Ny+1, Nx+1}};  //use reshape according to the  ordering of the matrix, so: z, y, x
    Eigen::Tensor<double, 3> p_matrix = p.reshape(to_matrix_sizes);
    //continuity_p.set_p_matrix(p_matrix);  //THIS FOR SOME REASON FAILS..., so don't use it...//save p_matrix to continuity_p member variable--> THIS MIGHT BE INEFFIIENT, BUT DO IT FOR NOW

    //////////////////////MAIN LOOP////////////////////////////////////////////////////////////////////////////////////////////////////////

    int iter, not_cnv_cnt, Va_cnt;
    bool not_converged;
    double error_np, old_error;  //this stores max value of the error and the value of max error from previous iteration
    std::vector<double> error_np_vector(num_rows);  //note: since n and p solutions are in vector form, can use vector form here also

    poisson.setup_matrix();  //I VERIFIED that size of sparse matrix is correct


    for (Va_cnt = 1; Va_cnt <= num_V; Va_cnt++) {  //+1 b/c 1st Va is the equil run
        not_converged = false;
        not_cnv_cnt = 0;

        Va = params.Va_min+params.increment*(Va_cnt-1);

        if (Va_cnt == 1) {
            params.use_tolerance_i();  //reset tolerance back
            params.use_w_i();
            //PhotogenRate = photogen.getPhotogenRate();    //otherwise PhotogenRate is pre-initialized to 0 in this main.cpp when declared
        }

        if (params.tolerance > 1e-5)
            std::cerr<<"ERROR: Tolerance has been increased to > 1e-5" <<std::endl;

        std::cout << "Va = " << Va <<std::endl;

        //Reset top and bottom BCs (outside of loop b/c don't change iter to iter)
        poisson.set_V_bottomBC(params, Va);
        poisson.set_V_topBC(params, Va);

       //correct through  here

        //-----------------------------------------------------------
        error_np = 1.0;
        iter = 0;

        //get's through here
        while (error_np > params.tolerance) {
            //std::cout << "Va " << Va <<std::endl;

            //-----------------Solve Poisson Equation------------------------------------------------------------------
            poisson.set_rhs(p);  //this finds netcharge and sets rhs
            //std::cout << poisson.get_sp_matrix() << std::endl;
            oldV = V;

            input = poisson.get_sp_matrix();

            //as expected, LU, is way too slow for a 3D matrix!!

            if (iter == 0) {
                BiCGStab_solver.analyzePattern(input);
                BiCGStab_solver.factorize(input);  //this computes preconditioner, Poisson matrix doesn't change, so can factorize just once
            }
            //BiCGStab_solver.compute(input);  //this computes the preconditioner..compute(input);
            //soln_V = BiCGStab_solver.solve(poisson.get_rhs());
            soln_V = BiCGStab_solver.solveWithGuess(poisson.get_rhs(), soln_V); //note: using soln_V for initial guess is faster than using V_Xd/NOTE: use solve with Guess...., b/c need initial guess
            //std::cout << "#iterations:     " << BiCGStab_solver.iterations() << std::endl;
             //std::cout << BiCGStab_solver.info() << std::endl;
            //std::cout << soln_V << std::endl;

           //CHOLESKY is not accurate!! for 3D solve

            //std::cout << poisson.get_sp_matrix() << std::endl;
             //std::cout << "Poisson solver error " << poisson.get_sp_matrix() * soln_Xd - poisson.get_rhs() << std::endl;

            //RECALL, I am starting my V vector from index of 1, corresponds to interior pts...
            for (int i = 0; i < num_rows; i++) {
                newV(i,0,0) = soln_V(i);   //fill VectorXd  rhs of the equation
                //NOTE: newV is a Tensor now, so use () to access
            }

            //Mix old and new solutions for V
            if (iter > 0)
                V  = utils.linear_mix(params, newV, oldV);
            else
                V = newV;

            //update the Eigen Vector_Xd for the initial guess--> LATER DO ALL THIS MORE EFFICIENTLY
            for (int i = 0; i < num_rows; i++) {
                V_Xd(i) = V(i,0,0);
            }

            //reshape solution to a V_matrix
            V_matrix = V.reshape(to_matrix_sizes); // reshapes using z, y, x ordering
            //need to permute before using for fullV
            temp_permuted = V_matrix.shuffle(permutation);  //need to use a temp variable for this to work

            //fill the fullV
            //SHOULD MOVE THIS TO A FUNCTION!
            for (int k = 1; k <= num_cell_z; k++)
               for (int j = 1; j <= num_cell_y; j++)
                   for (int i = 1; i <= num_cell_x; i++)
                       fullV(i,j,k) = temp_permuted(i-1,j-1,k-1);  //-1 b/c temp_permuted from V_matrix was filled from 0

            for (int j = 0; j <= num_cell_y; j++)
                for (int i = 0; i <= num_cell_x; i++)
                    fullV(i,j,0) = poisson.get_V_bottomBC(i,j);

            for (int k = 1; k <= num_cell_z; k++)
               for (int j = 1; j <= num_cell_y; j++)
                   fullV(0,j,k) = temp_permuted(num_cell_x-1,j-1,k-1);  //x BC's

            for (int k = 1; k <= num_cell_z; k++)
               for (int i = 1; i <= num_cell_x; i++)
                   fullV(i,0,k) = temp_permuted(i-1,num_cell_y-1,k-1);  //y BC's

            //fill edges
            for (int k = 1; k <= num_cell_z; k++)
                fullV(0,0,k) = temp_permuted(num_cell_x-1,0,k-1);




            //------------------------------Calculate Net Generation Rate----------------------------------------------------------

            //R_Langevin = recombo.ComputeR_Langevin(params,n,p);
            //FOR NOW CAN USE 0 FOR R_Langevin

                for (int i = 0; i < num_rows; i++) {
                    Up[i] = 0; //params.Photogen_scaling;  //This is what was used in Matlab version for testing.   photogen.getPhotogenRate()(i,j); //- R_Langevin(i,j);
                }

            //--------------------------------Solve equation for p------------------------------------------------------------

            continuity_p.setup_eqn(fullV, Up, p);  //pass it fullV...
            //std::cout << continuity_p.get_sp_matrix() << std::endl;   //Note: get rhs, returns an Eigen VectorXd

            for (int i = 0; i < num_rows; i++)
                oldp(i,0,0) = p(i,0,0);          //explicitely  copy, just in case

            input = continuity_p.get_sp_matrix();
            if (iter == 0) {
                BiCGStab_solver.analyzePattern(input);
                BiCGStab_solver.factorize(input);  //factorize only for the 1st iteration!!, since matrix doesn't change too much, this still will work!-> means it only  computes preconditioner once per Va!! //this computes preconditioner, if use along with analyzePattern (for 1st iter)
            }
            //BiCGStab_solver.compute(input);  //this computes the preconditioner..compute(input);
            soln_p = BiCGStab_solver.solveWithGuess(continuity_p.get_rhs(), soln_p);  //NOTE: if for initial guess use soln_p, INSTEAD OF p_Xd (which is the linearly mixed solution, then does't blow up!!!!!, even with 0.2 = w.
            //soln_p = BiCGStab_solver.solve(continuity_p.get_rhs());

//         std::cout << soln_p << std::endl;
//         exit(1);

            //save results back into n std::vector. RECALL, I am starting my V vector from index of 1, corresponds to interior pts...
            for (int i = 0; i < num_rows; i++) {
                newp(i,0,0) = soln_p(i);   //newp is now a tensor....
            }


            //------------------------------------------------

            //if get negative p's or n's set them = 0
            for (int i = 0; i < num_rows; i++) {
                if (newp(i,0,0) < 0.0) newp(i,0,0) = 0;
                //if (newn[i] < 0.0) newn[i] = 0;
            }

            //calculate the error
            old_error = error_np;
            int count = 0;  //for counting the error_np_vector_index

            //THIS CAN BE MOVED TO A FUNCTION IN UTILS
            std::fill(error_np_vector.begin(), error_np_vector.end(),0.0);  //refill with 0's so have fresh one
            for (int i = 0; i < num_rows; i++) {
                if (newp(i,0,0)!= 0) {
                    error_np_vector[count] = (std::abs(newp(i,0,0)-oldp(i,0,0)))/std::abs(oldp(i,0,0));
                    count++;
//                   if(iter == 2)
//                       std::cout << error_np_vector[i] << std::endl;
               }
            }

//            if(iter == 2) {
//                for (int i = 0; i < num_rows; i++ ) {
//                    std::cout <<  newp(i,0,0) << " " << oldp(i,0,0) <<  std::endl;   //FOR LARGE SYSTEMS SEEMS NEWP AND OLDP ARE EXACT SAME--> GETTING 0 ERRORS, BUT THEN BLOWS UP!!  //compare newp and oldp //newp are not 0's --> have numbers...
//                }
//                exit(1);
//            }

            error_np = *std::max_element(error_np_vector.begin(),error_np_vector.end());

            std::cout << error_np << std::endl;

            //auto decrease w if not converging
            if (error_np >= old_error)
                not_cnv_cnt = not_cnv_cnt+1;
            if (not_cnv_cnt > 1000) {  //Note: 100 is too small for C++, sometimes w is reduced when not necessary!!
                params.reduce_w();
                params.relax_tolerance();
                not_cnv_cnt = 0;
            }

            p = utils.linear_mix(params, newp, oldp);

            //update the Eig vector for initial guess --> LATER SHOULD DO ALL OF THIS MORE EFFICIENTLY
            for (int i = 0; i < num_rows; i++) {
                p_Xd(i) = p(i,0,0);
            }

            //Convert p to p_matrix
            p_matrix = p.reshape(to_matrix_sizes);  // this reshapes based on z,y,x ordering

            //need to permute before using for fullV
            temp_permuted = p_matrix.shuffle(permutation);  //need to use a temp variable for this to work

            //fill the fullp
            //SHOULD MOVE THIS TO A FUNCTION!
            for (int k = 1; k <= num_cell_z; k++)
               for (int j = 1; j <= num_cell_y; j++)
                   for (int i = 1; i <= num_cell_x; i++)
                       fullp(i,j,k) = temp_permuted(i-1,j-1,k-1);  //-1 b/c temp_permuted from V_matrix was filled from 0

            for (int j = 0; j <= num_cell_y; j++)
                for (int i = 0; i <= num_cell_x; i++)
                    fullp(i,j,0) = continuity_p.get_p_bottomBC(i,j);

            for (int k = 1; k <= num_cell_z; k++)
               for (int j = 1; j <= num_cell_y; j++)
                   fullp(0,j,k) = temp_permuted(num_cell_x-1,j-1,k-1);  //x BC's

            for (int k = 1; k <= num_cell_z; k++)
               for (int i = 1; i <= num_cell_x; i++)
                   fullp(i,0,k) = temp_permuted(i-1,num_cell_y-1,k-1);  //y BC's

            //fill edges
            for (int k = 1; k <= num_cell_z; k++)
                fullp(0,0,k) = temp_permuted(num_cell_x-1,0,k-1);

            //continuity_p.set_p_matrix(p_matrix);  //update member variable  //DON'T USE THIS B/C CAUSES ISSUES, just use the p matrix form main

           // std::cout << error_np << std::endl;
            //std::cout << "weighting factor = " << params.w << std::endl << std::endl;

            iter = iter+1;
        }

        //-------------------Calculate Currents using Scharfetter-Gummel definition--------------------------

        //continuity_n.calculate_currents();
        continuity_p.calculate_currents(fullp); //send it fullp, since we find it here anyway

        J_total_Z = continuity_p.get_Jp_Z();// + continuity_n.get_Jn_Z();
        J_total_X = continuity_p.get_Jp_X();// + continuity_n.get_Jn_X();
        J_total_Y = continuity_p.get_Jp_Y();// + continuity_n.get_Jn_Y();

//        for (int k = 1; k < num_cell_z; k++)
//            std::cout << J_total_Z(2,2,k) << std::endl;
//exit(1);

        //---------------------Write to file----------------------------------------------------------------
        utils.write_details(params, Va, fullV, fullp, J_total_Z, Up);

        if(Va_cnt >0) utils.write_JV(params, JV, iter, Va, J_total_Z);


    }//end of main loop

    JV.close();

    std::chrono::high_resolution_clock::time_point finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time = std::chrono::duration_cast<std::chrono::duration<double>>(finish-start);
    std::cout << "CPU time = " << time.count() << std::endl;

    return 0;
}
