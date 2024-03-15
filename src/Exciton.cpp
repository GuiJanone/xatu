#include <math.h>
#include <iomanip>
#include "xatu/System.hpp"
#include "xatu/Exciton.hpp"
#include "xatu/utils.hpp"
#include "xatu/davidson.hpp"

using namespace arma;
using namespace std::chrono;

namespace xatu {

/* ------------------------------ Setters ------------------------------ */

/**
 * Sets the number of unit cells along each axis.
 * @param ncell Number of unit cells per axis.
 * @return void
 */
void Exciton::setUnitCells(int ncell){
    if(ncell > 0){
        ncell_ = ncell;
    }
    else{
        std::cout << "ncell must be a positive number" << std::endl;
    }
}

/**
 * Sets the bands involved in the exciton calculation from a vector.
 * @param bands Vector of integers corresponding to the indices of the bands.
 * @return void 
 */
void Exciton::setBands(const arma::ivec& bands){
    bands_ = bands;
    std::vector<arma::s64> valence, conduction;
    for(int i = 0; i < bands.n_elem; i++){
        if (bands(i) <= 0){
            valence.push_back(bands(i) + system->fermiLevel);
        }
        else{
            conduction.push_back(bands(i) + system->fermiLevel);
        }
    }
    this->valenceBands_ = arma::ivec(valence);
    this->conductionBands_ = arma::ivec(conduction);
}

/**
 * Sets the bands involved in the exciton calculation specifying the number of bands
 * above and below the Fermi level.
 * @param nbands Number of valence (conduction) bands used.
 * @param nrmbands Number of valence (conduction) bands removed from calculation.
 */
void Exciton::setBands(int nbands, int nrmbands){
    int fermiLevel = system->fermiLevel;
    if(nbands > 0 && nrmbands > 0){
        this->valenceBands_ = arma::regspace<arma::ivec>(fermiLevel - nbands + 1, fermiLevel - nrmbands);
        this->conductionBands_ = arma::regspace<arma::ivec>(fermiLevel + 1 + nrmbands, fermiLevel + nbands);
        this->bands_ = arma::join_rows(valenceBands, conductionBands);
    }
    else{
        std::cout << "Included bands and removed bands must be positive numbers" << std::endl;
    }
}

/**
 * Sets the center-of-mass momentum of the exciton.
 * @param Q Momentum vector.
 * @return void 
 */
void Exciton::setQ(const arma::rowvec& Q){
    if(Q.n_elem == 3){
        Q_ = Q;
    }
    else{
        std::cout << "Q vector must be 3d" << std::endl;
    }
    
}

/**
 * Sets the cutoff over unit cells used in the calculation of the lattice Fourier transform
 * for the interactions.
 * @param cutoff Number of unit cells to consider.
 * @return void 
 */
void Exciton::setCutoff(double cutoff){
    if(cutoff > 0){
        cutoff_ = cutoff;
        if(cutoff > ncell){
            std::cout << "Warning: cutoff is higher than number of unit cells" << std::endl;
        }
    }
    else{
        std::cout << "cutoff must be a positive number" << std::endl;
    }
}

/**
 * Sets the value of the scissor cut of change the gap of the system.
 * @param shift Value of scissor cut (in eV). Can be positive or negative.
 * @return void 
 */
void Exciton::setScissor(double shift){
    this->scissor_ = shift;
}

/**
 * To toggle on or off the exchange term in the interaction matrix elements.
 * @param exchange Either true of false
 * @return void
*/
void Exciton::setExchange(bool exchange){
    this->exchange = exchange;
}

/*------------------------------------ Electron-hole pair basis ------------------------------------*/

/**
 * Initialise basis to be used in the construction of the BSE matrix.
 * @param conductionBands Conduction bands that will populate the electrons of the exciton.
 * @param valenceBands Valence bands to be populated by holes of the exciton.
 * @return Matrix where each row denotes an electron-hole pair, '{v, c, k}'.
 */
arma::imat Exciton::createBasis(const arma::ivec& conductionBands, 
                                const arma::ivec& valenceBands){

    arma::imat states = arma::zeros<arma::imat>(excitonbasisdim, 3);
    int it = 0;
    for (int i = 0; i < system->nk; i++){
        for (int k = 0; k < (int)conductionBands.n_elem; k++){
            for (int j = 0; j < (int)valenceBands.n_elem; j++){

                arma::irowvec state = { valenceBands(j), conductionBands(k), i };
                states.row(it) = state;
                it++;
            };
        };
    };

    basisStates_ = states;

    return states;
};

/**
 * Overload of createBasis method to work with class attributes instead of given ones.
 * @return void.
 */
void Exciton::initializeBasis(){
    this->basisStates_ = createBasis(conductionBands, valenceBands);
};

/**
 * Criterium to fix the phase of the single-particle eigenstates after diagonalization.
 * @details The prescription we take here is to impose that the sum of all the coefficients is real.
 * @return Fixed coefficients. 
 */
arma::cx_mat Exciton::fixGlobalPhase(arma::cx_mat& coefs){

    arma::cx_rowvec sums = arma::sum(coefs);
    std::complex<double> imag(0, 1);
    for(int j = 0; j < sums.n_elem; j++){
        double phase = arg(sums(j));
        coefs.col(j) *= exp(-imag*phase);
    }

    return coefs;
}

/**
 * Creates a dictionary that maps bands to indices for storage.
 * @return void
 */
void Exciton::generateBandDictionary(){

    std::map<int, int> bandToIndex;
    for(int i = 0; i < bandList.n_elem; i++){
        bandToIndex[bandList(i)] = i;
    };

    this->bandToIndex = bandToIndex;
};


/**
 * Method to print information about the exciton.
 * @return void 
 */
void Exciton::printInformation(){
    cout << std::left << std::setw(30) << "Number of cells: " << ncell << endl;
    cout << std::left << std::setw(30) << "Valence bands:";
    for (int i = 0; i < valenceBands.n_elem; i++){
        cout << valenceBands(i) << "\t";
    }
    cout << endl;

    cout << std::left << std::setw(30) << "Conduction bands: ";
    for (int i = 0; i < conductionBands.n_elem; i++){
        cout << conductionBands(i) << "\t";
    }
    cout << "\n" << endl;

    if(exchange){
        cout << std::left << std::setw(30) << "Exchange: " << (exchange ? "True" : "False") << endl;
    }
    if(arma::norm(Q) > 1E-7){
        cout << std::left << std::setw(30) << "Q: "; 
        for (auto qi : Q){
            cout << qi << "  ";
        }
        cout << endl;
    }
    cout << std::left << std::setw(30) << "Scissor cut: " << scissor_ << endl;
}

}