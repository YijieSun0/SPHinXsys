/* -------------------------------------------------------------------------*
 *								SPHinXsys									*
 * -------------------------------------------------------------------------*
 * SPHinXsys (pronunciation: s'finksis) is an acronym from Smoothed Particle*
 * Hydrodynamics for industrial compleX systems. It provides C++ APIs for	*
 * physical accurate simulation and aims to model coupled industrial dynamic*
 * systems including fluid, solid, multi-body dynamics and beyond with SPH	*
 * (smoothed particle hydrodynamics), a meshless computational method using	*
 * particle discretization.													*
 *																			*
 * SPHinXsys is partially funded by German Research Foundation				*
 * (Deutsche Forschungsgemeinschaft) DFG HU1527/6-1, HU1527/10-1,			*
 *  HU1527/12-1 and HU1527/12-4													*
 *                                                                          *
 * Portions copyright (c) 2017-2022 Technical University of Munich and		*
 * the authors' affiliations.												*
 *                                                                          *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may  *
 * not use this file except in compliance with the License. You may obtain a*
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.       *
 *                                                                          *
 * ------------------------------------------------------------------------*/
/**
 * @file 	fluid_surface_inner.hpp
 * @brief 	Here, we define the algorithm classes for fluid surfaces.
 * @details Fluid indicators are mainly used here to classify different region in a fluid.
 * @author	Chi ZHang and Xiangyu Hu
 */

#pragma once

#include "fluid_surface_inner.h"

namespace SPH
{
	//=================================================================================================//
	namespace fluid_dynamics
	{
        //=================================================================================================//
		template <class ExecutionPolicy>
        void FreeSurfaceIndicationInner::
		interaction(const ExecutionPolicy &execution_policy, size_t index_i, Real dt)
        {
            Real pos_div = 0.0;
            const Neighborhood &inner_neighborhood = inner_configuration_[index_i];
            for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
            {
                pos_div -= inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.r_ij_[n];
            }
            pos_div_[index_i] = pos_div;
        }
        //=================================================================================================//
		template <class ExecutionPolicy>
        void ColorFunctionGradientInner::
		interaction(const ExecutionPolicy &execution_policy, size_t index_i, Real dt)
        {
            Vecd gradient = Vecd::Zero();
            const Neighborhood &inner_neighborhood = inner_configuration_[index_i];
            if (pos_div_[index_i] < threshold_by_dimensions_)
            {
                for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
                {
                    gradient -= inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.e_ij_[n];
                }
            }
            color_grad_[index_i] = gradient;
            surface_norm_[index_i] = gradient / (gradient.norm() + TinyReal);
        }
        //=================================================================================================//
		template <class ExecutionPolicy>
        void ColorFunctionGradientInterpolationInner::
		interaction(const ExecutionPolicy &execution_policy, size_t index_i, Real dt)
        {
            Vecd grad = Vecd::Zero();
            Real weight(0);
            Real total_weight(0);
            if (surface_indicator_[index_i] == 1 && pos_div_[index_i] > threshold_by_dimensions_)
            {
                Neighborhood &inner_neighborhood = inner_configuration_[index_i];
                for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
                {
                    size_t index_j = inner_neighborhood.j_[n];
                    if (surface_indicator_[index_j] == 1 && pos_div_[index_j] < threshold_by_dimensions_)
                    {
                        weight = inner_neighborhood.W_ij_[n] * Vol_[index_j];
                        grad += weight * color_grad_[index_j];
                        total_weight += weight;
                    }
                }
                Vecd grad_norm = grad / (total_weight + TinyReal);
                color_grad_[index_i] = grad_norm;
                surface_norm_[index_i] = grad_norm / (grad_norm.norm() + TinyReal);
            }
        }
        //=================================================================================================//
 		template <class ExecutionPolicy>
       void SurfaceTensionAccelerationInner::
	   interaction(const ExecutionPolicy &execution_policy, size_t index_i, Real dt)
        {
            Vecd n_i = surface_norm_[index_i];
            Real curvature(0.0);
            Real renormalized_curvature(0);
            Real pos_div(0);
            if (surface_indicator_[index_i] == 1)
            {
                Neighborhood &inner_neighborhood = inner_configuration_[index_i];
                for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
                {
                    size_t index_j = inner_neighborhood.j_[n];
                    if (surface_indicator_[index_j] == 1)
                    {
                        Vecd n_j = surface_norm_[index_j];
                        Vecd n_ij = n_i - n_j;
                        curvature -= inner_neighborhood.dW_ijV_j_[n] * n_ij.dot(inner_neighborhood.e_ij_[n]);
                        pos_div -= inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.r_ij_[n];
                    }
                }
            }
            /**
             Adami et al. 2010 has a typo in equation.
             (dv / dt)_s = (1.0 / rho) (-sigma * k * n * delta)
                         = (1/rho) * curvature * color_grad
                         = (1/m) * curvature * color_grad * vol
             */
            renormalized_curvature = (Real)Dimensions * curvature / ABS(pos_div + TinyReal);
            Vecd acceleration = gamma_ * renormalized_curvature * color_grad_[index_i] * Vol_[index_i];
            acc_prior_[index_i] -= acceleration / mass_[index_i];
        }
		//=================================================================================================//
		template <class FreeSurfaceIdentification>
		template <typename... ConstructorArgs>
		SpatialTemporalFreeSurfaceIdentification<FreeSurfaceIdentification>::
			SpatialTemporalFreeSurfaceIdentification(ConstructorArgs &&...args)
			: FreeSurfaceIdentification(std::forward<ConstructorArgs>(args)...)
		{
			this->particles_->registerVariable(previous_surface_indicator_, "PreviousSurfaceIndicator", [&](size_t i) -> int {return 1;});
			this->particles_->template registerSortableVariable<int>("PreviousSurfaceIndicator");
		}
		//=================================================================================================//
		template <class FreeSurfaceIdentification>
		template <class ExecutionPolicy>
		void SpatialTemporalFreeSurfaceIdentification<FreeSurfaceIdentification>::
			interaction(const ExecutionPolicy &execution_policy, size_t index_i, Real dt)
		{
			FreeSurfaceIdentification::interaction(execution_policy, index_i, dt);

			if (this->pos_div_[index_i] < this->threshold_by_dimensions_)
			{
				checkNearPreviousFreeSurface(index_i);
			}
		}
		//=================================================================================================//
		template <class FreeSurfaceIdentification>
		void SpatialTemporalFreeSurfaceIdentification<FreeSurfaceIdentification>::
			checkNearPreviousFreeSurface(size_t index_i)
		{
			if (previous_surface_indicator_[index_i] != 1)
			{
				bool is_near_previous_surface = false;
				const Neighborhood &inner_neighborhood = this->inner_configuration_[index_i];
				for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
				{
					if (previous_surface_indicator_[inner_neighborhood.j_[n]] == 1)
					{
						is_near_previous_surface = true;
					}
				}
				if (!is_near_previous_surface)
				{
					this->pos_div_[index_i] = 2.0 * this->threshold_by_dimensions_;
				}
			}
		}
		//=================================================================================================//
		template <class FreeSurfaceIdentification>
		void SpatialTemporalFreeSurfaceIdentification<FreeSurfaceIdentification>::
			update(size_t index_i, Real dt)
		{
			FreeSurfaceIdentification::update(index_i, dt);

			previous_surface_indicator_[index_i] = this->surface_indicator_[index_i];
		}
		//=================================================================================================//
		template <class DensitySummationType>
		void DensitySummationFreeSurface<DensitySummationType>::update(size_t index_i, Real dt)
		{
			this->rho_[index_i] = ReinitializedDensity(this->rho_sum_[index_i], this->rho0_, this->rho_[index_i]);
		}
		//=================================================================================================//
		template <class DensitySummationFreeSurfaceType>
		template <typename... ConstructorArgs>
		DensitySummationFreeStream<DensitySummationFreeSurfaceType>::
			DensitySummationFreeStream(ConstructorArgs &&...args)
			: DensitySummationFreeSurfaceType(std::forward<ConstructorArgs>(args)...),
			  surface_indicator_(*this->particles_->template getVariableByName<int>("SurfaceIndicator")){};
		//=================================================================================================//
		template <class DensitySummationFreeSurfaceType>
		void DensitySummationFreeStream<DensitySummationFreeSurfaceType>::update(size_t index_i, Real dt)
		{
			if (this->rho_sum_[index_i] < this->rho0_ && isNearSurface(index_i))
			{
				this->rho_[index_i] = this->ReinitializedDensity(this->rho_sum_[index_i], this->rho0_, this->rho_[index_i]);
			}
			else
			{
				this->rho_[index_i] = this->rho_sum_[index_i];
			}
		}
		//=================================================================================================//
		template <class DensitySummationFreeSurfaceType>
		bool DensitySummationFreeStream<DensitySummationFreeSurfaceType>::isNearSurface(size_t index_i)
		{
			bool is_near_surface = true;
			if (surface_indicator_[index_i] != 1)
			{
				is_near_surface = false;
				const Neighborhood &inner_neighborhood = this->inner_configuration_[index_i];
				for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
				{
					if (surface_indicator_[inner_neighborhood.j_[n]] == 1)
					{
						is_near_surface = true;
						break;
					}
				}
			}
			return is_near_surface;
		}
		//=================================================================================================//
	}
	//=================================================================================================//
}
