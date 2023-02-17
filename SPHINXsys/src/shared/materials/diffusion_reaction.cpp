#include "diffusion_reaction.h"
#include "base_particles.hpp"

namespace SPH
{
	//=================================================================================================//
	void LocalIsotropicDiffusion::assignBaseParticles(BaseParticles *base_particles)
	{
		IsotropicDiffusion::assignBaseParticles(base_particles);
		initializeThermalConductivity();
	}
	//=================================================================================================//
	void LocalIsotropicDiffusion::initializeThermalConductivity()
	{
		base_particles_->registerVariable(local_thermal_conductivity_, "ThermalDiffusivity", [&](size_t i) -> Real { return diff_cf_; });
		base_particles_->addVariableToWrite<Real>("ThermalDiffusivity");
		base_particles_->addVariableToRestart<Real>("ThermalDiffusivity");
	}
	//=================================================================================================//
	void DirectionalDiffusion::initializeDirectionalDiffusivity(Real diff_cf, Real bias_diff_cf, Vecd bias_direction)
	{
		bias_diff_cf_ = bias_diff_cf;
		bias_direction_ = bias_direction;
		Matd diff_i = diff_cf_ * Matd::Identity() + bias_diff_cf_ * bias_direction_ * bias_direction_.transpose();
		transformed_diffusivity_ = inverseCholeskyDecomposition(diff_i);
	};
	//=================================================================================================//
	void LocalDirectionalDiffusion::assignBaseParticles(BaseParticles *base_particles)
	{
		DirectionalDiffusion::assignBaseParticles(base_particles);
		initializeFiberDirection();
		initializeThermalConductivity();
	};
	//=================================================================================================//
	void LocalDirectionalDiffusion::initializeFiberDirection()
	{
		base_particles_->registerVariable(local_bias_direction_, "Fiber");
		base_particles_->addVariableNameToList<Vecd>(reload_local_parameters_, "Fiber");
	}
	//=================================================================================================//
	void LocalDirectionalDiffusion::initializeThermalConductivity()
	{
		base_particles_->registerVariable<Matd>(local_transformed_diffusivity_, "TransformedDiffusivity");
		base_particles_->registerVariable<Real>(local_thermal_conductivity_, "ThermalDiffusivity", diff_cf_);
		base_particles_->addVariableToWrite<Real>("ThermalDiffusivity");
		base_particles_->addVariableToRestart<Real>("ThermalDiffusivity");
	}
	//=================================================================================================//
	void LocalDirectionalDiffusion::readFromXmlForLocalParameters(const std::string &filefullpath)
	{
		BaseMaterial::readFromXmlForLocalParameters(filefullpath);
		size_t total_real_particles = base_particles_->total_real_particles_;
		for (size_t i = 0; i != total_real_particles; i++)
		{
			Matd diff_i = diff_cf_ * Matd::Identity() + bias_diff_cf_ * local_bias_direction_[i] * local_bias_direction_[i].transpose();
			local_transformed_diffusivity_.push_back(inverseCholeskyDecomposition(diff_i));
		}
		std::cout << "\n Local diffusion parameters setup finished " << std::endl;
	};
	//=================================================================================================//
}
