% Setup of 3D hole continuity matrix using loop and spdiag

function Ap = SetAp_3D(p_mob, Bernoulli_p_values)

global num_elements N

%extract variables from struct, for brevity in eqns
Bp_posX = Bernoulli_p_values.Bp_posX;
Bp_negX = Bernoulli_p_values.Bp_negX;
Bp_posY = Bernoulli_p_values.Bp_posY;
Bp_negY = Bernoulli_p_values.Bp_negY;
Bp_posZ = Bernoulli_p_values.Bp_posZ;
Bp_negZ = Bernoulli_p_values.Bp_negZ;

Ap_val = zeros(num_elements, 7);   %this is a matrix which will just store the non-zero diagonals of 2D hole continuity eqn

%NOTE: index is not neccesarily equal to i (the x index of V), it is the
%index of the diagonal arrays.
%--------------------------------------------------------------------------
%Lowest diagonal: 
i = 1;
j = 1;
k = 2;  %these are the 1st indices OF MAIN DIAG (or rhs), that 1st element of lowest diag corresponds to.
for index = 1:N^3 - N^2      % (1st element corresponds to Nth row  (number of elements = N^3 - N^2) 
    Ap_val(index,1) = -((p_mob(i+1,j+1,k+1) + p_mob(i+1+1,j+1,k+1) + p_mob(i+1, j+1+1, k+1) + p_mob(i+1+1,j+1+1,k+1))/4.)*Bp_posZ(i+1,j+1,k+1);
    
    i = i+1;
    if (i > N)
        i = 1;  %reset i when reach end of subblock
        j = j+1;   
    end
     if (j > N)  
        j = 1;
        k = k+1;
    end
    
end  
%--------------------------------------------------------------------------
%lower diagonal
i = 1;
j = 2;  %NOTE: this are the rhs indices (= main diag indices) which these elements correspond to.
k = 1;
for index = 1:N^3 - N      
    if (j > 1)
        Ap_val(index,2) = -((p_mob(i+1,j+1, k+1) + p_mob(i+1+1, j+1, k+1) + p_mob(i+1, j+1, k+1+1) + p_mob(i+1+1, j+1, k+1+1))/4.)*Bp_posY(i+1,j+1,k+1);
    end
    
    i = i+1;
    if (i > N)
        i = 1;  %reset i when reach end of subblock
        j = j+1;
    end
    if (j > N)  
        j = 1;
        k = k+1;
    end
end      

%--------------------------------------------------------------------------
%main lower diagonal (below main diagonal)
i = 2;
j = 1;
k = 1;
for index = 1:N^3 - 1    
    if (i > 1)
        Ap_val(index,3) = -((p_mob(i+1,j+1, k+1) + p_mob(i+1,j+1+1, k+1) + p_mob(i+1,j+1,k+1+1) + p_mob(i+1,j+1+1,k+1+1))/4.)*Bp_posX(i+1,j+1,k+1);
    end
    
    i = i+1;
    if (i > N)
        i = 1;
        j = j+1;
    end
    if (j > N)
        j = 1;
        k = k+1;
    end
end
%--------------------------------------------------------------------------
%main diagonal
i = 1;
j = 1;
k = 1;
for index =  1:num_elements          
    Ap_val(index,4) = ((p_mob(i+1,j+1,k+1) + p_mob(i+1+1,j+1,k+1) + p_mob(i+1, j+1+1, k+1) + p_mob(i+1+1,j+1+1,k+1))/4.)*Bp_negZ(i+1,j+1,k+1) ...
                    + ((p_mob(i+1,j+1, k+1) + p_mob(i+1+1, j+1, k+1) + p_mob(i+1, j+1, k+1+1) + p_mob(i+1+1, j+1, k+1+1))/4.)*Bp_negY(i+1,j+1,k+1) ...
                    + ((p_mob(i+1,j+1, k+1) + p_mob(i+1,j+1+1, k+1) + p_mob(i+1,j+1,k+1+1) + p_mob(i+1,j+1+1,k+1+1))/4.)*Bp_negX(i+1,j+1,k+1) ...
                    + ((p_mob(i+1+1,j+1,k+1) + p_mob(i+1+1,j+1+1,k+1) + p_mob(i+1+1,j+1,k+1+1) + p_mob(i+1+1,j+1+1,k+1+1))/4.)*Bp_posX(i+1+1,j+1,k+1) ...
                    + ((p_mob(i+1, j+1+1, k+1) + p_mob(i+1+1, j+1+1, k+1) + p_mob(i+1, j+1+1, k+1+1) + p_mob(i+1+1, j+1+1, k+1+1))/4.)*Bp_posY(i+1,j+1+1,k+1) ...
                    + ((p_mob(i+1,j+1, k+1+1) + p_mob(i+1+1,j+1,k+1+1) + p_mob(i+1,j+1+1,k+1+1) + p_mob(i+1+1,j+1+1,k+1+1))/4.)*Bp_posZ(i+1,j+1,k+1+1);

    i = i+1;
    if (i > N)
        i = 1;
        j = j+1;
    end
    if (j > N)
        j = 1;
        k = k+1;
    end

end
%--------------------------------------------------------------------------
%main uppper diagonal
i = 1;
j = 1;
k = 1;
for index = 2:N^3 - 1 +1    %matlab fills this from the bottom (so i = 2 corresponds to 1st row in matrix)
    
    if (i > 0)
        Ap_val(index,5) = -((p_mob(i+1+1,j+1,k+1) + p_mob(i+1+1,j+1+1,k+1) + p_mob(i+1+1,j+1,k+1+1) + p_mob(i+1+1,j+1+1,k+1+1))/4.)*Bp_negX(i+1+1,j+1,k+1);
    end
    
    i=i+1;
    if (i > N-1)  %here are only N-1 elements per block and i starts from 1
        i = 0;
        j  = j+1;
    end
    if (j > N)
        j = 1;
        k = k+1;
    end
    
end      
%--------------------------------------------------------------------------
%upper diagonal
i = 1;
j = 1;
k = 1;
for index = 1+N:N^3 - N +N 
    if (j > 0)
        Ap_val(index, 6) = -((p_mob(i+1, j+1+1, k+1) + p_mob(i+1+1, j+1+1, k+1) + p_mob(i+1, j+1+1, k+1+1) + p_mob(i+1+1, j+1+1, k+1+1))/4.)*Bp_negY(i+1,j+1+1,k+1);
    end
    
     i = i+1;
     if (i > N)
        i = 1;
        j = j+1;
     end
     if (j > N-1)
        j = 0;
        k = k+1;
     end
end
%--------------------------------------------------------------------------
%far upper diagonal
i = 1;
j = 1;
k = 1;
for index = 1+N*N:num_elements      %matlab fills from bottom, so this starts at 1+N (since 1st element is in the 2nd subblock of matrix) 
    Ap_val(index,7) = -((p_mob(i+1,j+1, k+1+1) + p_mob(i+1+1,j+1,k+1+1) + p_mob(i+1,j+1+1,k+1+1) + p_mob(i+1+1,j+1+1,k+1+1))/4.)*Bp_negZ(i+1,j+1,k+1+1);         
    
    i = i+1;  
    if (i > N)
        i = 1;
        j = j+1;
    end
     if (j > N)
        j = 1;
        k = k+1;
    end
end 

%all not specified elements will remain zero, as they were initialized
%above.

Ap = spdiags(Ap_val, [-N^2 -N -1 0 1 N N^2], num_elements, num_elements); %A = spdiags(B,d,m,n) creates an m-by-n sparse matrix by taking the columns of B and placing them along the diagonals specified by d.

%Ap_matrix = full(Ap); %use to see the full matrix

