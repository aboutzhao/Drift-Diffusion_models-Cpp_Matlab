#include "parameters.h"


void Parameters::Initialize()
{
    //input from file the parameters
    std::ifstream parameters;
    parameters.open("parameters.inp");
    //check if file was opened
    if (!parameters) {
        std::cerr << "Unable to open file parameters.inp";
        exit(1);   // call system to stop
    }

    try{
        std::string comment;  //to "eat" the comments. will only work is comment has no spaces btw words
        parameters >> comment;  //header line
        parameters >> Lx >> comment;
        isPositive(Lx, comment);
        parameters >> Ly >> comment;
        isPositive(Ly, comment);
        parameters >> Lz >> comment;
        isPositive(Lz, comment);

        parameters >> dx >> comment;
        isPositive(dx, comment);
        parameters >> dy >> comment;
        isPositive(dy, comment);
        parameters >> dz >> comment;
        isPositive(dz, comment);

        parameters >> N_LUMO >> comment;  //we will just ignore the comments
        isPositive(N_LUMO,comment);
        parameters >> N_HOMO >> comment;
        isPositive(N_HOMO,comment);

        parameters >> eps_active >> comment;
        isPositive(eps_active,comment);
        parameters >> p_mob_active >> comment;
        isPositive(p_mob_active,comment);
        parameters >> mobil >> comment;
        isPositive(mobil,comment);

        parameters >> Va_min >> comment;
        parameters >> Va_max >> comment;
        parameters >> increment >> comment;
        isPositive(increment,comment);
        parameters >> w_eq >> comment;
        isPositive(w_eq,comment);
        parameters >> w_i >> comment;
        isPositive(w_i,comment);
        parameters >> tolerance_i >> comment;
        isPositive(tolerance_i,comment);
        parameters >> w_reduce_factor >> comment;
        isPositive(w_reduce_factor,comment);
        parameters >> tol_relax_factor >> comment;
        isPositive(tol_relax_factor,comment);
        parameters.close();
        N_dos = N_HOMO;     //scaling factor helps CV be on order of 1

    }
    catch(std::exception &e){
        std::cerr << e.what() << std::endl;
        exit(1);
    }

    num_cell_x = floor(Lx/dx);
    num_cell_y = floor(Ly/dy);
    num_cell_z = floor(Lz/dz);

    Nx = num_cell_x - 1;
    Ny = num_cell_y - 1;
    Nz = num_cell_z - 1;
    num_elements = (Nx+1)*(Ny+1)*(Nz+1);

}

void Parameters::isPositive(double input, const std::string &comment)
{
    if(input <=0){
        std::cerr << "error: Non-positive input for " << comment << std::endl;
        std::cerr << "Input was read as " << input << std::endl;
        throw std::runtime_error("Invalid input. This input must be positive.");
    }
}

void Parameters::isPositive(int input, const std::string &comment)
{
    if(input <=0){
        std::cerr << "error: Non-positive input for " << comment << std::endl;
        std::cerr << "Input was read as " << input << std::endl;
        throw std::runtime_error("Invalid input. This input must be positive.");
    }
}

void Parameters::isNegative(double input, const std::string &comment)
{
    if(input >=0){
        std::cerr << "error: Non-negative input for " << comment << std::endl;
        std::cerr << "Input was read as " << input << std::endl;
        throw std::runtime_error("Invalid input. This input must be negative.");
    }
}

void Parameters::isNegative(int input, const std::string &comment)
{
    if(input >=0){
        std::cerr << "error: Non-negative input for " << comment << std::endl;
        std::cerr << "Input was read as " << input << std::endl;
        throw std::runtime_error("Invalid input. This input must be negative.");
    }

}
