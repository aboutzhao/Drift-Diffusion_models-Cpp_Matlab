%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%         3D Drift Diffusion for Holes with Finite Differences

%           THIS IS THE SINGLE CARRIER VERSION--> NO GENERATION 
%
%             -Scharfetter-Gummel discretization
%             -decoupled Gummel iterations method
%
%            Created by: Timofey Golubev (2018.06.29)
%
%     This includes the 3D poisson equation and 3D continuity/drift-diffusion
%     equations using Scharfetter-Gummel discretization. The Poisson equation
%     is solved first, and the solution of potential is used to calculate the
%     Bernoulli functions and solve the continuity eqn's.
%
%   Boundary conditions for Poisson equation are:
%
%     -a fixed voltage at (x,0) and (x, Nz) defined by V_bottomBC
%      and V_topBC which are defining the  electrodes
%
%    -insulating boundary conditions: V(0,y,z) = V(1,y,z) and
%     V(N+1,y,z) = V(N,y,z) (N is the last INTERIOR mesh point).
%     so the potential at the boundary is assumed to be the same as just inside
%     the boundary. Gradient of potential normal to these boundaries is 0.
%    V(x,0,z) = V(x,1,z) and V(x,N+1,z) = V(x,N,z)
%
%   Matrix equations are AV*V = bV, Ap*p = bp, and An*n = bn where AV, Ap, and An are sparse matrices
%   (generated using spdiag), for the Poisson and continuity equations.
%   V is the solution for electric potential, p is the solution for hole
%   density, n is solution for electron density
%   bV is the rhs of Poisson eqn which contains the charge densities and boundary conditions
%   bp is the rhs of hole continuity eqn which contains net generation rate
%   and BCs
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
clear; close all; clc;
global num_cell N num_elements Vt N_dos p_topBC p_leftBC_x p_rightBC_x p_leftBC_y p_rightBC_y p_bottomBC Cn CV Cp n_leftBC_x  n_rightBC_x n_leftBC_y  n_rightBC_y
global n_bottomBC n_topBC V_leftBC_x V_leftBC_y V_rightBC_x V_rightBC_y V_bottomBC V_topBC G_max

%% Physical Constants

q =  1.60217646*10^-19;         %elementary charge (C)
kb = 1.3806503*10^-23;          %Boltzmann const. (J/k)
T = 296.;                       %temperature (K)
epsilon_0 =  8.85418782*10^-12; %F/m
Vt = (kb*T)/q;

%% Simulation Setup

%Voltage sweep loop
Va_min = -0.5;            %volts
Va_max = -0.45;
increment = 0.01;         %by which to increase V
num_V = floor((Va_max-Va_min)/increment)+1;   %number of V points

%Simulation parameters
w_eq = 0.2;               %linear mixing factor for 1st convergence (0 applied voltage, no generation equilibrium case)
w_i = 0.2;                 %starting linear mixing factor for Va_min (will be auto decreased if convergence not reached)
tolerance = 5*10^-12;        %error tolerance
tolerance_i =  5*10^-12;     %initial error tolerance, will be increased if can't converge to this level

%% System Setup
L = 10.0000001e-9;     %there's some integer rounding issue, so use this .0000001
dx = 1e-9;                        %mesh size
num_cell = floor(L/dx);
N = num_cell -1;       %number of INTERIOR mesh points (total mesh pts = num_cell +1 b/c matlab indixes from 1)
num_elements = N*(N+1)^2;  %NOTE: this will specify number of elements in the solution vector V which = N*(N+1)^2 b/c in x, y direction we include the right bc for Pbc's so have N+1. In k we have just N

%Electronic density of states of holes and electrons
N_VB = 10^24;         %density of states in valence band (holes)
N_CB = 10^24;         %density of states in conduction bands (electrons)
E_gap = 1.5;          %bandgap of the active layer(in eV)
N_dos = 10^24.;       %scaling factor helps CV be on order of 1

%injection barriers
inj_a = 0.2;	%at anode
inj_c = 0.1;	%at cathode

%work functions of anode and cathode
WF_anode = 4.8;
WF_cathode = 3.7;

Vbi = WF_anode - WF_cathode +inj_a +inj_c;  %built-in field

G_max = 4*10^27;

%% Define matrices of system parameters

tic

%Preallocate vectors and matrices
fullV = zeros(N+2, N+2, N+2);
fullp = zeros(N+2, N+2, N+2);
% fulln = zeros(N+2, N+2, N+2);
Jp_Z = zeros(num_cell, num_cell, num_cell);
Jn_Z = zeros(num_cell, num_cell, num_cell);
Jp_X = zeros(num_cell, num_cell, num_cell);
% Jn_X = zeros(num_cell, num_cell, num_cell);
% Jp_Y = zeros(num_cell, num_cell, num_cell);
% Jn_Y = zeros(num_cell, num_cell, num_cell);
V_values = zeros(num_V+1,1);
J_total_Z_middle = zeros(num_V+1,1);

% Relative dielectric constant matrix (can be position dependent)
%Epsilons are defined at 1/2 integer points, so epsilons inside
%the cells, not at cell boundaries
%will use indexing: i + 1/2 is defined as i+1 for the index
epsilon = 3.0*ones(num_cell+2, num_cell +2, num_cell +2);
for i =  1:num_cell+1   %go extra +1 so matches size with Bernoulli fnc's which multiply by
    for j = 1:num_cell+1
        for k = 1:num_cell+1
            epsilon_avged.eps_X_avg(i,j,k) = (epsilon(i,j,k) + epsilon(i,j+1,k) + epsilon(i,j,k+1) + epsilon(i,j+1,k+1))./4.;
            epsilon_avged.eps_Y_avg(i,j,k) = (epsilon(i,j,k) + epsilon(i+1,j,k) + epsilon(i,j,k+1) + epsilon(i+1,j,k+1))./4.;
            epsilon_avged.eps_Z_avg(i,j,k) = (epsilon(i,j,k) + epsilon(i+1,j,k) + epsilon(i,j+1,k) + epsilon(i+1,j+1,k))./4.;
            
            
            %             n_mob_avged.n_mob_X_avg(i,j,k) = (n_mob(i,j,k) + n_mob(i,j+1,k) + n_mob(i,j,k+1) + n_mob(i,j+1,k+1))./4.;
            %             n_mob_avged.n_mob_Y_avg(i,j,k) = (n_mob(i,j,k) + n_mob(i+1,j,k) + n_mob(i,j,k+1) + n_mob(i+1,j,k+1))./4.;
            %             n_mob_avged.n_mob_Z_avg(i,j,k) = (n_mob(i,j,k) + n_mob(i+1,j,k) + n_mob(i,j+1,k) + n_mob(i+1,j+1,k))./4.;
            %add num_cell+2 values for i to account for extra bndry pt
            epsilon_avged.eps_X_avg(num_cell+2,j,k) = epsilon_avged.eps_X_avg(1,j,k);
            epsilon_avged.eps_Y_avg(num_cell+2,j,k) = epsilon_avged.eps_Y_avg(1,j,k);
            epsilon_avged.eps_Z_avg(num_cell+2,j,k) = epsilon_avged.eps_Z_avg(1,j,k);
            
            %add num_cell+2 values for j to account for extra bndry pt
            epsilon_avged.eps_X_avg(i,num_cell+2,k) = epsilon_avged.eps_X_avg(i,1,k);
            epsilon_avged.eps_Y_avg(i,num_cell+2,k) = epsilon_avged.eps_Y_avg(i,1,k);
            epsilon_avged.eps_Z_avg(i,num_cell+2,k) = epsilon_avged.eps_Z_avg(i,1,k);
        end
    end
end
%NOTE: NEED 1 MORE at num_cell+2 FOR X AND Y values only
%-->this is the wrap around average for
%PBC's! Takes avg of N+1th element with the 0th (defined as 1st element in
%Matlab)
%FOR NOW JUST ASSUME (REASONABLY) THAT EPSILONS ALONG X, Y BOUNDARIES WILL BE
%UNIFORM.....--> OTHERWISE PBC's don't make ANY SENSE!!!--> SINCE NEED TO
%HAVE CONTINOUS EPSILONG IN THOSE DIRECTIONS to extend them periodically
% so I just added another element to j and k in above loop


%-----------------Mobilities setup-----------------------------------------
% Define mobilities matrix (can be position dependent)
p_mob = (4.5*10^-6)*ones(num_cell+2, num_cell+2, num_cell+2);
% n_mob = p_mob;

mobil = 5.*10^-6;        %scaling for mobility 

p_mob = p_mob./mobil;
% n_mob = n_mob./mobil;

%Pre-calculate the mobility averages 
%using indexing: i+1/2 is defined as i+1 for index, just like the epsilons
p_mob_avged.p_mob_X_avg = zeros(num_cell+2, num_cell+2, num_cell+1);
p_mob_avged.p_mob_Y_avg = zeros(num_cell+2, num_cell+2, num_cell+1);
p_mob_avged.p_mob_Z_avg = zeros(num_cell+2, num_cell+2, num_cell+1);

% n_mob_avged.n_mob_X_avg = zeros(num_cell+1, num_cell+1, num_cell+1);
% n_mob_avged.n_mob_Y_avg = zeros(num_cell+1, num_cell+1, num_cell+1);
% n_mob_avged.n_mob_Z_avg = zeros(num_cell+1, num_cell+1, num_cell+1);

for i =  1:num_cell+1   %go extra +1 so matches size with Bernoulli fnc's which multiply by
    for j = 1:num_cell+1  %to account for extra bndry pt added at right side
        for k = 1:num_cell+1
            p_mob_avged.p_mob_X_avg(i,j,k) = (p_mob(i,j,k) + p_mob(i,j+1,k) + p_mob(i,j,k+1) + p_mob(i,j+1,k+1))./4.;
            p_mob_avged.p_mob_Y_avg(i,j,k) = (p_mob(i,j,k) + p_mob(i+1,j,k) + p_mob(i,j,k+1) + p_mob(i+1,j,k+1))./4.;
            p_mob_avged.p_mob_Z_avg(i,j,k) = (p_mob(i,j,k) + p_mob(i+1,j,k) + p_mob(i,j+1,k) + p_mob(i+1,j+1,k))./4.;
            
            %             n_mob_avged.n_mob_X_avg(i,j,k) = (n_mob(i,j,k) + n_mob(i,j+1,k) + n_mob(i,j,k+1) + n_mob(i,j+1,k+1))./4.;
            %             n_mob_avged.n_mob_Y_avg(i,j,k) = (n_mob(i,j,k) + n_mob(i+1,j,k) + n_mob(i,j,k+1) + n_mob(i+1,j,k+1))./4.;
            %             n_mob_avged.n_mob_Z_avg(i,j,k) = (n_mob(i,j,k) + n_mob(i+1,j,k) + n_mob(i,j+1,k) + n_mob(i+1,j+1,k))./4.;
            %add num_cell+2 values for i to account for extra bndry pt
            p_mob_avged.p_mob_X_avg(num_cell+2,j,k) = p_mob_avged.p_mob_X_avg(1,j,k);
            p_mob_avged.p_mob_Y_avg(num_cell+2,j,k) = p_mob_avged.p_mob_Y_avg(1,j,k);
            p_mob_avged.p_mob_Z_avg(num_cell+2,j,k) = p_mob_avged.p_mob_Z_avg(1,j,k);
            
            %add num_cell+2 values for j to account for extra bndry pt
            p_mob_avged.p_mob_X_avg(i,num_cell+2,k) = p_mob_avged.p_mob_X_avg(i,1,k);
            p_mob_avged.p_mob_Y_avg(i,num_cell+2,k) = p_mob_avged.p_mob_Y_avg(i,1,k);
            p_mob_avged.p_mob_Z_avg(i,num_cell+2,k) = p_mob_avged.p_mob_Z_avg(i,1,k);
        end
    end
end


%--------------------------------------------------------------------------
%Scaling coefficients
Cp = dx^2/(Vt*N_dos*mobil);          %note: scaled p_mob and n_mob are inside matrices
% Cn = dx^2/(Vt*N_dos*mobil);
CV = (N_dos*dx^2*q)/(epsilon_0*Vt);    %relative permitivity was moved into the matrix

%% Define Poisson equation boundary conditions and initial conditions

% Initial conditions
V_bottomBC(1:N+2, 1:N+2) = -((Vbi)/(2*Vt)-inj_a/Vt);  %needs to be matrix, so can add i.e. afm tip
V_topBC(1:N+2, 1:N+2) = (Vbi)/(2*Vt)-inj_c/Vt;
diff = (V_topBC(1,1) - V_bottomBC(1,1))/num_cell;
% V(1:N) = V_bottomBC + diff;  %define V's corresponding to 1st subblock here (1st interior row of system)
% index = 0;
V_matrix = zeros(N+1,N+1,N);

%SET UP V_matrix first--> then use permute and (:) to get V--> that will be easier
for k = 1:N
    for i = 1:N+1
        for j = 1:N+1
            V_matrix(i, j, k) = V_bottomBC(i+1,j+1) +  diff*k;  %note: V_matrix contains just the inside values
        end
    end
end
permuted_V_matrix = permute(V_matrix, [3 2 1]);
V = permuted_V_matrix(:);

%-------------------------------------------------------------------------------------------------
%% Define continuity equn boundary and initial conditions
%these are scaled
% n_bottomBC = N_CB*exp(-(E_gap-inj_a)/Vt)/N_dos;
p_bottomBC(1:N+2,1:N+2) = N_VB*exp(-inj_a/Vt)/N_dos;
% n_topBC = N_CB*exp(-inj_c/Vt)/N_dos;
p_topBC(1:N+2,1:N+2) = N_VB*exp(-(E_gap-inj_c)/Vt)/N_dos;

%define initial conditions as min value of BCs
min_dense = min(p_bottomBC(1,1), p_topBC(1,1));
p = min_dense*ones(num_elements, 1);
% p = n;

%form matrices for easy filling of bp
% n_matrix = reshape(n,N,N,N);
p_matrix = reshape(p,N+1,N+1,N);  %this is a reshape before solving, so keep as x,y,z format

%-------------------------------------------------------------------------------------------------

% Set up Poisson matrix equation
AV = SetAV_3D(epsilon_avged);
[L,U] = lu(AV);  %do and LU factorization here--> since Poisson matrix doesn't change
%this will significantly speed up backslash, on LU factorized matrix
%spy(AV);  %allows to see matrix structure, very useful!

%% Main voltage loop
Va_cnt = 0;
for Va_cnt = 0:num_V +1
    tic
    not_converged = false;
    not_cnv_cnt = 0;
    
    %stop the calculation if tolerance becomes too high
    if(tolerance >10^-5)
        break
    end
    
    %1st iteration is to find the equilibrium values (no generation rate)
    if(Va_cnt ==0)
        tolerance = tolerance*10^2;       %relax tolerance for equil convergence
        w = w_eq;                         %use smaller mixing factor for equil convergence
        Va = 0;
        Up = zeros(num_elements, 1);  %better to store as 1D vector
%         Un= Up;
    end
    if(Va_cnt ==1)
        tolerance = tolerance_i;       %reset tolerance back
        w=w_i;
%         G = GenerationRate();  %only use it once, since stays constant
    end
    if(Va_cnt >0)
        Va = Va_min+increment*(Va_cnt-1);     %set Va value
        %Va = Va_max-increment*(Va_cnt-1);    %decrease Va by increment in each iteration
    end
    
    %Voltage boundary conditions
    V_bottomBC(1:N+2, 1:N+2) = -((Vbi  -Va)/(2*Vt)-inj_a/Vt);
    V_topBC(1:N+2, 1:N+2) = (Vbi- Va)/(2*Vt) - inj_c/Vt;
      
    iter = 1;
    error_np =  1;
    % Solver loop
    while(error_np > tolerance)
        %% Poisson Solve
        bV = SetbV_3D(p, epsilon);

        %solve for V
        oldV = V;

            newV = U\(L\bV);  %much faster to solve pre-factorized matrix. Not applicable to cont. eqn. b/c matrices keep changing.
%          newV = AV\bV;
%    [newV,~] = bicgstab(AV,bV, 10^-14, 1000, [], [], V);  
  %This is fastest for larger systems (i.e. 30x30x30)
%NOTE: USING bicgstab as default (w/o specifying a tolerance), results in BAD results!! 


%for poisson, don't see any cpu advantage in using bicgstab, vs. the
%prefactorized LU plus \.

        
        if(iter >1)
            V = newV*w + oldV*(1.-w);
        else
            V = newV;  %no mixing for 1st iter
        end
        
        %reshape the V vector into the V matrix
        
        V_matrix = reshape(V,N,N+1,N+1); %it is this ordering b/c V has order of z,x,y 
        %need to permute the matrix to go back to x,y,z format
        V_matrix = permute(V_matrix, [3 2 1]);
        %---------------------------------------------------------------------------------        
        %add on the BC's to get full potential matrix
        fullV(2:N+2,2:N+2, 2:N+1) = V_matrix;
        fullV(1:N+2,1:N+2,1) = V_bottomBC(1:N+2,1:N+2);
        fullV(1:N+2,1:N+2,N+2) = V_topBC(1:N+2,1:N+2);
        %note: right BC is already in V_matrix -> left BC = rightBC from
        %pbc's
        fullV(1,2:N+2,2:N+1) = V_matrix(N+1,:,:);  %x bc's
        fullV(2:N+2,1,2:N+1) = V_matrix(:,N+1,:);
        %fill edges
        fullV(1,1,2:N+1) = V_matrix(N+1,1,:);  %left = right
        fullV(1,N+2,2:N+1) = V_matrix(1,N+1,:);
        
        
        %% Update net generation rate
%         if(Va_cnt > 0)
%             Up = G;   %these only  include insides since want ctoo be consistent with i,j matrix indices
%             %Up= zeros(num_elements,num_elements);
% %             Un= Up;
%         end

        
        %% Continuity equations solve
        Bernoulli_p_values = Calculate_Bernoullis_p(fullV);  %the values are returned as a struct
%         Bernoulli_n_values = Calculate_Bernoullis_n(fullV);
        Ap = SetAp_3D(p_mob_avged, Bernoulli_p_values);           
%         An = SetAn_3D(n_mob_avged, Bernoulli_n_values);
        bp = Setbp_3D(Bernoulli_p_values, p_mob, Up);
%         bn = Setbn_3D(Bernoulli_n_values, n_mob, Un);
        
        
        %NOTE TO SEE THE WHOLE SPARSE MATRICES: use :
        % full([name of sparse matrix])
        
        oldp = p;
        
        %          newp = Ap\bp;
        %          newp = qmr(Ap, bp, 10^-14, 1000);  %not bad: 0.035sec vs. 0.025 with bicgstab
        
        [newp, ~] = bicgstab(Ap, bp, 10^-14, 1000, [], [], p); %This is fast!, fastest iterative solver for continuity eqn.
        
%         oldn = n;
        %          newn = An\bn;
%         [newn, ~] = bicgstab(An,bn, 10^-14, 1000);  %NOTE: using a lower tolerance, i..e 10^-14, makes this faster--> since more accuracy makes overall convergence of DD model faster
        %Note: seems 10^-14, is about the best level it can converge to
        
        % if get negative p's or n's, make them equal 0
        for i = 1:num_elements
            if(newp(i) <0.0)
                newp(i) = 0;
            end
%             if(newn(i) <0.0)
%                 newn(i) = 0;
%             end
        end
        
        old_error =  error_np;
        count = 0;
        error_np_matrix = zeros(1,num_elements); %need to reset the matrix b/c when more newp's become 0, need to remove that error element from matrix.
        for i = 1:num_elements  %recall that newp, oldp are stored as vectors
            if(newp(i) ~=0)
                count = count+1;  %counts number of non zero error calculations
                error_np_matrix(count) = (abs(newp(i)-oldp(i)))./abs(oldp(i));  %need the dot slash, otherwise it tries to do matrix operation! %ERROR SHOULD BE CALCULATED BEFORE WEIGHTING
            end
        end
        error_np = max(error_np_matrix)
        
        %auto decrease w if not converging
        if(error_np>= old_error)
            not_cnv_cnt = not_cnv_cnt+1;
        end
        if(not_cnv_cnt>2000)
            w = w/2.;
            tolerance = tolerance*10;
            not_cnv_cnt = 0;  %reset the count
        end
        
        %w
        %tolerance
        
        %weighting
        p = newp*w + oldp*(1.-w);
%         n = newn*w + oldn*(1.-w);
        
        %% Apply continuity eqn BCs
        
        %reshape the vectors into matrices
        p_matrix = reshape(p,N,N+1,N+1);
        p_matrix = permute(p_matrix, [3 2 1]);
%         n_matrix = reshape(n,N,N,N);
      
        %-------------------------------------------------------------------------------------------------
        
        %add on the BC's to get full potential matrix
        fullp(2:N+2,2:N+2, 2:N+1) = p_matrix;
        fullp(1:N+2,1:N+2,1) = p_bottomBC(1:N+2,1:N+2);
        fullp(1:N+2,1:N+2,N+2) = p_topBC(1:N+2,1:N+2);
        %note: right BC is already in V_matrix -> left BC = rightBC from
        %pbc's
        fullp(1,2:N+2,2:N+1) = p_matrix(N+1,:,:);  %x bc's
        fullp(2:N+2,1,2:N+1) = p_matrix(:,N+1,:);
        %fill edges
        fullp(1,1,2:N+1) = p_matrix(N+1,1,:);  %left = right
        fullp(1,N+2,2:N+1) = p_matrix(1,N+1,:);
        
        %add on the BC's to get full potential matrix
%         fulln(2:N+1,2:N+1, 2:N+1) = n_matrix;
%         fulln(1:N+2,1:N+2,1) = n_bottomBC;
%         fulln(1:N+2,1:N+2,N+2) = n_topBC;
%         fulln(1,2:N+1,2:N+1) = n_leftBC_x;
%         fulln(N+2,2:N+1,2:N+1) = n_rightBC_x;
%         fulln(2:N+1,1,2:N+1) = n_leftBC_y;
%         fulln(2:N+1,N+2,2:N+1) = n_rightBC_y;
%         %fill edges
%         fulln(1,1,2:N+1) = n_leftBC_x(1,:);
%         fulln(1,N+2,2:N+1) = n_leftBC_x(N,:);
%         fulln(N+2,1,2:N+1) = n_rightBC_x(1,:);
%         fulln(N+2,N+2,2:N+1) = n_rightBC_x(N,:);
        
        iter = iter +1;   
        
    end 
    
    % Calculate drift diffusion currents
    % Use the SG definition
    Bp_posX = Bernoulli_p_values.Bp_posX;
    Bp_negX = Bernoulli_p_values.Bp_negX;
    Bp_posY = Bernoulli_p_values.Bp_posY;
    Bp_negY = Bernoulli_p_values.Bp_negY;
    Bp_posZ = Bernoulli_p_values.Bp_posZ;
    Bp_negZ = Bernoulli_p_values.Bp_negZ;
    
%     Bn_posX = Bernoulli_n_values.Bn_posX;
%     Bn_negX = Bernoulli_n_values.Bn_negX;
%     Bn_posY = Bernoulli_n_values.Bn_posY;
%     Bn_negY = Bernoulli_n_values.Bn_negY;
%     Bn_posZ = Bernoulli_n_values.Bn_posZ;
%     Bn_negZ = Bernoulli_n_values.Bn_negZ;
    
    %the J(i+1,j+1) is to define J's (which are defined at mid cells, as rounded up integer)
    for i = 1:num_cell-1
        for j = 1:num_cell-1
            for k = 1:num_cell-1
                Jp_Z(i+1,j+1,k+1) = -(q*Vt*N_dos*mobil/dx)*(p_mob(i+1,j+1,k+1)*fullp(i+1,j+1,k+1)*Bp_negZ(i+1,j+1,k+1)-p_mob(i+1,j+1,k+1)*fullp(i+1,j+1,k)*Bp_posZ(i+1,j+1,k+1));
%                 Jn_Z(i+1,j+1,k+1) =  (q*Vt*N_dos*mobil/dx)*(n_mob(i+1,j+1,k+1)*fulln(i+1,j+1,k+1)*Bn_posZ(i+1,j+1,k+1)-n_mob(i+1,j+1,k+1)*fulln(i+1,j+1,k)*Bn_negZ(i+1,j+1,k+1));
                
                Jp_X(i+1,j+1,k+1) = -(q*Vt*N_dos*mobil/dx)*(p_mob(i+1,j+1,k+1)*fullp(i+1,j+1,k+1)*Bp_negX(i+1,j+1,k+1)-p_mob(i+1,j+1,k+1)*fullp(i,j+1,k+1)*Bp_posX(i+1,j+1,k+1));
%                 Jn_X(i+1,j+1,k+1) =  (q*Vt*N_dos*mobil/dx)*(n_mob(i+1,j+1,k+1)*fulln(i+1,j+1,k+1)*Bn_posX(i+1,j+1,k+1)-n_mob(i+1,j+1,k+1)*fulln(i,j+1,k+1)*Bn_negX(i+1,j+1,k+1));
                
                Jp_Y(i+1,j+1,k+1) = -(q*Vt*N_dos*mobil/dx)*(p_mob(i+1,j+1,k+1)*fullp(i+1,j+1,k+1)*Bp_negY(i+1,j+1,k+1)-p_mob(i+1,j+1,k+1)*fullp(i+1,j,k+1)*Bp_posY(i+1,j+1,k+1));
%                 Jn_Y(i+1,j+1,k+1) =  (q*Vt*N_dos*mobil/dx)*(n_mob(i+1,j+1,k+1)*fulln(i+1,j+1,k+1)*Bn_posY(i+1,j+1,k+1)-n_mob(i+1,j+1,k+1)*fulln(i+1,j,k+1)*Bn_negY(i+1,j+1,k+1));
            end        
        end
    end
    J_total_Z = Jp_Z;
    J_total_X = Jp_X;
    J_total_Y = Jp_Y;
    
    
    %Setup for JV curve
    if(Va_cnt>0)
        V_values(Va_cnt,:) = Va;
        J_total_Z_middle(Va_cnt) = J_total_Z(floor(num_cell/2),floor(num_cell/2),floor(num_cell/2));  %just store the J (in perpendicular to electrodes direction) in middle for the JV curve output
    end
    
    %Save data
    str = sprintf('%.2f',Va);
    filename = [str 'V.txt']
    fid = fopen(fullfile(filename),'w'); %fullfile allows to make filename from parts
    
    if(Va_cnt ==0)
        equil = fopen(fullfile('Equil.txt'),'w');
        for k = 2:num_cell
            i = floor(num_cell/2);
            j = floor(num_cell/2);
            fprintf(equil,'%.2e %.2e %.2e %.8e %.8e \r\n ',(i-1)*dx, (j-1)*dx, (k-1)*dx, Vt*fullV(i,j,k), N_dos*fullp(i,j,k));
        end
        fclose(equil);
    end
    
    Up_matrix = reshape(Up,N,N+1,N+1);
    Up_matrix = permute(Up_matrix, [3 2 1]);
%     Un_matrix = reshape(Un,N,N);
    
    if(Va_cnt > 0)
        for k = 2:num_cell
            i = floor(num_cell/2);     %JUST OUTPUT LINE PROFILE ALONG Z --> otherwise hard to compare results to 1D model
            j = floor(num_cell/2);
            fprintf(fid,'%.2e %.2e %.2e %.8e %.8e %.8e %.8e %.8e %.8e %.8e\r\n', (i-1)*dx, (j-1)*dx, (k-1)*dx, Vt*fullV(i,j,k), N_dos*fullp(i,j,k), J_total_X(i,j,k), J_total_Z(i,j,k), Up_matrix(i-1,j-1,k-1), w, tolerance);
        end
    end
    fclose(fid);
    
    %% JV setup:
    file2 = fopen(fullfile('JV.txt'),'w');
    for i = 1:Va_cnt
        fprintf(file2, '%.8e %.8e \r\n', V_values(i,1), J_total_Z_middle(i));
    end
    fclose(file2);
    
    %% Final Plots: done for the last Va
    str = sprintf('%.2g', Va);
    
    toc  %time each Va
    
end

toc

%plot V
% surf(1:N+2,1:N+2, Vt*fullV)
% xlabel('Position ($m$)','interpreter','latex','FontSize',14);
% ylabel({'Electric Potential (V)'},'interpreter','latex','FontSize',14);

figure
%plot p
% surf(1:N+2,1:N+2, log(N_dos*fullp))
% hold on
% surf(1:N+2,1:N+2, log(N_dos*fulln))
% hold off
% xlabel('Position ($m$)','interpreter','latex','FontSize',14);
% zlabel({'Log of carrier densities ($1/m^3$)'},'interpreter','latex','FontSize',14);

%plot line profiles of charge densities in the thickness direction
figure
plot(log(N_dos*fullp(1,1:N+2)))
hold on
% plot(log(N_dos*fulln(1,1:N+2)))
hold off
xlabel('Position ($m$)','interpreter','latex','FontSize',14);
ylabel({'Log of carrier densities ($1/m^3$)'},'interpreter','latex','FontSize',14);

figure
plot(Vt*fullV(1,1:N+2))
xlabel('Position ($m$)','interpreter','latex','FontSize',14);
ylabel({'Electric Potential (V)'},'interpreter','latex','FontSize',14);
