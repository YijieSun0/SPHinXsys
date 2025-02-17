#include "inelastic_solid.h"
#include "base_particles.hpp"

namespace SPH
{
	//=================================================================================================//
	void HardeningPlasticSolid::initializePlasticParameters()
	{
		base_particles_->registerVariable(inverse_plastic_strain_, "InversePlasticRightCauchyStrain", 
											[&](size_t i) -> Matd { return Matd::Identity(); });
		base_particles_->registerVariable(hardening_parameter_, "HardeningParameter");
		base_particles_->addVariableToRestart<Matd>("InversePlasticRightCauchyStrain");
		base_particles_->addVariableToRestart<Real>("HardeningParameter");
	}
	//=================================================================================================//
	void HardeningPlasticSolid::assignBaseParticles(BaseParticles *base_particles)
	{
		ElasticSolid::assignBaseParticles(base_particles);
		initializePlasticParameters();
	}
	//=================================================================================================//
	Matd HardeningPlasticSolid::PlasticConstitutiveRelation(const Matd &F, size_t index_i, Real dt)
	{
		Matd be = F * inverse_plastic_strain_[index_i] * F.transpose();
		Matd normalized_be = be * pow(be.determinant(), -OneOverDimensions);
		Real normalized_be_isentropic = normalized_be.trace() * OneOverDimensions;
		Matd deviatoric_PK = DeviatoricKirchhoff(normalized_be - normalized_be_isentropic * Matd::Identity());
		Real deviatoric_PK_norm = deviatoric_PK.norm();
		Real trial_function = deviatoric_PK_norm -
							  sqrt_2_over_3_ * (hardening_modulus_ * hardening_parameter_[index_i] + yield_stress_);
		if (trial_function > 0.0)
		{
			Real renormalized_shear_modulus = normalized_be_isentropic * G0_;
			Real relax_increment = 0.5 * trial_function / (renormalized_shear_modulus + hardening_modulus_ / 3.0);
			hardening_parameter_[index_i] += sqrt_2_over_3_ * relax_increment;
			deviatoric_PK -= 2.0 * renormalized_shear_modulus * relax_increment * deviatoric_PK / deviatoric_PK_norm;
			Matd relaxed_be = deviatoric_PK / G0_ + normalized_be_isentropic * Matd::Identity();
			normalized_be = relaxed_be * pow(relaxed_be.determinant(), -OneOverDimensions);
		}
		Matd inverse_F = F.inverse();
		Matd inverse_F_T = inverse_F.transpose();
		inverse_plastic_strain_[index_i] = inverse_F * normalized_be * inverse_F_T;

		return (deviatoric_PK + VolumetricKirchhoff(F.determinant()) * Matd::Identity()) * inverse_F_T;
	}
	//=================================================================================================//
}
