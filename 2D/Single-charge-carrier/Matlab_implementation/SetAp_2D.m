% Setup of 2D hole continuity matrix using loop and spdiag

function Ap = SetAp_2D(p_mob, Bernoulli_p_values)

global num_elements N

%extract variables from struct, for brevity in eqns
Bp_posX = Bernoulli_p_values.Bp_posX;
Bp_negX = Bernoulli_p_values.Bp_negX;
Bp_posZ = Bernoulli_p_values.Bp_posZ;
Bp_negZ = Bernoulli_p_values.Bp_negZ;

Ap_val = zeros(num_elements, 5);   %this is a matrix which will just store the non-zero diagonals of 2D hole continuity eqn

%NOTE: index is not neccesarily equal to i (the x index of V), it is the
%index of the diagonal arrays.

%Lowest diagonal: corresponds to V(i, j-1)
for index = 1:N*(N-1)      %(1st element corresponds to Nth row  (number of elements = N*(N-1)
    i = mod(index,N);
    if(i ==0)                %the multiples of N correspond to last index
        i = N;
    end
    j = 2 + floor((index-1)/N);    %this is the y index of V which element corresponds to. 1+ floor(index/4)determines which subblock this corresponds to and thus determines j, since the j's for each subblock are all the same.
    
    Ap_val(index,1) = -((p_mob(i+1,j+1) + p_mob(i+1+1,j+1))/2.)*Bp_posZ(i+1,j+1);
end

%main lower diagonal (below main diagonal): corresponds to V(i-1,j)
for index = 1:num_elements-1      %(1st element corresponds to 2nd row)%NOTE: this is tricky!-->some elements are 0 (at the corners of the
%subblocks)
    i = mod(index,N);         %this is x index of V which element corresponds to (note if this = 0, means these are the elements which are 0);
    j = 1 + floor((index-1)/N);
    
    if(mod(index, N) == 0)
        Ap_val(index,2) = 0;   %these are the elements at subblock corners
    else
        Ap_val(index,2) = -((p_mob(i+1,j+1) + p_mob(i+1,j+1+1))/2.)*Bp_posX(i+1,j+1);
    end
end

%main diagonal: corresponds to V(i,j)
for index =  1:num_elements      
    i = mod(index,N);
    if(i ==0)                %the multiples of N correspond to last index
        i = N;
    end
    j = 1 + floor((index-1)/N);
    
    Ap_val(index,3) = ((p_mob(i+1,j+1) + p_mob(i+1,j+1+1))/2.)*Bp_negX(i+1,j+1) + ((p_mob(i+1+1,j+1) + p_mob(i+1+1,j+1+1))/2.)*Bp_posX(i+1+1,j+1) + ((p_mob(i+1,j+1) + p_mob(i+1+1,j+1))/2.)*Bp_negZ(i+1,j+1) + ((p_mob(i+1,j+1+1) + p_mob(i+1+1,j+1+1))/2.)*Bp_posZ(i+1,j+1+1);
end

%main uppper diagonal: corresponds to V(i+1,j)
for index = 2:num_elements     %matlab fills this from the bottom (so i = 2 corresponds to 1st row in matrix)
    i = mod(index-1,N);
    j = 1 + floor((index-2)/N);
    
    if(i == 0)     
        Ap_val(index,4) = 0;
    else
        Ap_val(index,4) = -((p_mob(i+1+1,j+1) + p_mob(i+1+1,j+1+1))/2.)*Bp_negX(i+1+1,j+1);
    end
    
end

%far upper diagonal: corresponds to V(i,j+1)
for index = 1+N:num_elements      %matlab fills from bottom, so this starts at 1+N (since 1st element is in the 2nd subblock of matrix)
    i = mod(index-N,N);
    if(i ==0)                %the multiples of N correspond to last index
        i = N;
    end
    j = 1 + floor((index-1-N)/N);
    
    Ap_val(index,5) = -((p_mob(i+1,j+1+1) + p_mob(i+1+1,j+1+1))/2.)*Bp_negZ(i+1,j+1+1);            %1st element corresponds to 1st row.   this has N^2 -N elements
end

%all not specified elements will remain zero, as they were initialized
%above.


Ap = spdiags(Ap_val, [-N -1 0 1 N], num_elements, num_elements); %A = spdiags(B,d,m,n) creates an m-by-n sparse matrix by taking the columns of B and placing them along the diagonals specified by d.
%diagonals  [-N -1 0 1 N].  -N and N b/c the far diagonals are in next
%subblocks, N diagonals away from main diag.


%Ap_matrix = full(Ap); %use to see the full matrix

