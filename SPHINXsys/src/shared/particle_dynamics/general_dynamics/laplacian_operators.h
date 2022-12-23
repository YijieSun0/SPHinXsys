/* -----------------------------------------------------------------------------*
 *                               SPHinXsys                                      *
 * -----------------------------------------------------------------------------*
 * SPHinXsys (pronunciation: s'finksis) is an acronym from Smoothed Particle    *
 * Hydrodynamics for industrial compleX systems. It provides C++ APIs for       *
 * physical accurate simulation and aims to model coupled industrial dynamic    *
 * systems including fluid, solid, multi-body dynamics and beyond with SPH      *
 * (smoothed particle hydrodynamics), a meshless computational method using     *
 * particle discretization.                                                     *
 *                                                                              *
 * SPHinXsys is partially funded by German Research Foundation                  *
 * (Deutsche Forschungsgemeinschaft) DFG HU1527/6-1, HU1527/10-1,               *
 * HU1527/12-1 and HU1527/12-4.                                                 *
 *                                                                              *
 * Portions copyright (c) 2017-2022 Technical University of Munich and          *
 * the authors' affiliations.                                                   *
 *                                                                              *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may      *
 * not use this file except in compliance with the License. You may obtain a    *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.           *
 *                                                                              *
 * -----------------------------------------------------------------------------*/
/**
 * @file 	laplacian_operators.h
 * @brief 	This is the particle dynamics applicable for all type bodies
 * @author	Xiangyu Hu
 */

#ifndef LAPLACIAN_OPERATORS_H
#define LAPLACIAN_OPERATORS_H

#include "general_operators.h"

namespace SPH
{
    /**
     * @class LaplacianInner
     * @brief Base class for computing Laplacian operators with inner relation
     * This can be used for computing dissipative terms
     */
    template <typename DataType>
    class LaplacianInner : public BaseOperatorInner<DataType, DataType>
    {
    public:
        LaplacianInner(BaseInnerRelation &inner_relation,
                           const std::string &in_variable_name, const std::string &out_variable_name)
            : BaseOperatorInner<DataType, DataType>(inner_relation, in_variable_name, out_variable_name){};
        virtual ~LaplacianInner(){};

    protected:
        template <typename CoefficientFunction>
        void inline loopNeighbors(size_t index_i, const CoefficientFunction &coefficient)
        {
            DataType sum = ZeroData<DataType>::value;
            const Neighborhood &neighborhood = this->inner_configuration_[index_i];
            for (size_t n = 0; n != neighborhood.current_size_; ++n)
            {
                size_t index_j = neighborhood.j_[n];
                sum += 2.0 * coefficient(index_i, index_j) *
                       (this->in_variable_[index_i] - this->in_variable_[index_j]) *
                       neighborhood.dW_ijV_j_[n] / neighborhood.r_ij_[n];
            }
            this->out_variable_[index_i] = sum;
        };
    };

    /**
     * @class LaplacianContact
     * @brief Base class for computing Laplacian operators with contact relation
     * This can be used for computing dissipative terms
     */
    template <typename DataType>
    class LaplacianContact : public BaseOperatorContact<DataType, DataType>
    {
    public:
        LaplacianContact(BaseContactRelation &contact_relation,
                             const std::string &in_variable_name, const std::string &out_variable_name)
            : BaseOperatorContact<DataType, DataType>(contact_relation, in_variable_name, out_variable_name){};
        virtual ~LaplacianContact(){};

    protected:
        template <typename CoefficientFunction>
        void inline loopNeighbors(size_t index_i, const CoefficientFunction &coefficient)
        {
            DataType sum = ZeroData<DataType>::value;
            for (size_t k = 0; k < this->contact_configuration_.size(); ++k)
            {
                const Neighborhood &neighborhood = (*this->contact_configuration_[k])[index_i];
                StdLargeVec<DataType> &in_variable_k = *(this->contact_in_variable_[k]);
                for (size_t n = 0; n != neighborhood.current_size_; ++n)
                {
                    size_t index_j = neighborhood.j_[n];
                    sum += 2.0 * coefficient(index_i, index_j) *
                           (this->in_variable_[index_i] - in_variable_k[index_j]) *
                           neighborhood.dW_ijV_j_[n] / neighborhood.r_ij_[n];
                }
                this->out_variable_[index_i] += sum;
            }
        };
    };
}
#endif // LAPLACIAN_OPERATORS_H