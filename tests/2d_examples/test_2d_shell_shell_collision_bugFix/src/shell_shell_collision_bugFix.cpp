/**
 * @file 	shell_shell_collision.cpp
 * @brief 	An elastic shell palte will contact a shell wall
 * @details This is a case to test shell->shell collision without impact.
 * @details Both the upper plate and the lower wall are thin shell structures.
 * @author 	Massoud Rezavand, Virtonomy GmbH
 */
#include "sphinxsys.h"	//SPHinXsys Library.
using namespace SPH;	//Namespace cite here.
//----------------------------------------------------------------------
//	Basic geometry parameters and numerical setup.
//----------------------------------------------------------------------
Real gravity_g = 0.5;
Real PT = 1.0;									  /** Thickness of the square plate. */
Vec2d n_0 = Vec2d(0.0, 1.0);					  /** Pseudo-normal. */
int particle_number = 40;						  /** Particle number in the direction of the length */
int BWD = 1;
Real PL = 10.0;
Real resolution_ref = PL / (Real)particle_number; /** Initial reference particle spacing. */
Real BW = resolution_ref * (Real)BWD; /** Boundary width, determined by specific layer of boundary particles. */
BoundingBox system_domain_bounds(Vec2d(-PL, -PL), Vec2d(PL + BW, PL));
//----------------------------------------------------------------------
//	Global paramters on material properties
//----------------------------------------------------------------------
Real rho0_s = 1.0;					/** Normalized density. */
Real Youngs_modulus = 5e4;			/** Normalized Youngs Modulus. */
Real poisson = 0.45;				/** Poisson ratio. */
Real physical_viscosity = 200.0;	/** physical damping, here we choose the same value as numerical viscosity. */
//----------------------------------------------------------------------
//	Bodies with cases-dependent geometries (ComplexShape).
//----------------------------------------------------------------------
/** Define application dependent particle generator for the upper plate. */
class UpperPlateParticleGenerator : public ParticleGeneratorDirect
{
public:
	UpperPlateParticleGenerator() : ParticleGeneratorDirect()
	{
		for (int i = 0; i < (particle_number + 2 * BWD); i++)
		{
			Real x = resolution_ref * i - BW + resolution_ref * 0.5;
			positions_volumes_.push_back(std::make_pair(Vecd(x - PL/2., 0.5), resolution_ref));
		}
	}
};

/** Define application dependent particle generator for lower shell wall. */
class PlateParticleGeneratorWall : public ParticleGeneratorDirect
{
public:
	PlateParticleGeneratorWall() : ParticleGeneratorDirect()
	{
		// the plate and boundary
		for (int i = 0; i < (particle_number + 2 * BWD); i++)
		{
			Real x = resolution_ref * i - BW + resolution_ref * 0.5;
			positions_volumes_.push_back(std::make_pair(Vecd(x - PL/2., 0.0), resolution_ref));
		}
	}
};

/** Define the boundary geometry. */
class BoundaryGeometry : public BodyPartByParticle
{
public:
	BoundaryGeometry(SPHBody &body, const std::string &body_part_name)
		: BodyPartByParticle(body, body_part_name)
	{
		TaggingParticleMethod tagging_particle_method = std::bind(&BoundaryGeometry::tagManually, this, _1);
		tagParticles(tagging_particle_method);
	};
	virtual ~BoundaryGeometry(){};

private:
	void tagManually(size_t index_i)
	{
		if (base_particles_->pos_n_[index_i][0] < 0.0 or base_particles_->pos_n_[index_i][0] > PL-1.)
		{
			body_part_particles_.push_back(index_i);
		}
	};
};

//----------------------------------------------------------------------
//	Main program starts here.
//----------------------------------------------------------------------
int main(int ac, char* av[])
{
	//----------------------------------------------------------------------
	//	Build up the environment of a SPHSystem with global controls.
	//----------------------------------------------------------------------
	SPHSystem sph_system(system_domain_bounds, resolution_ref);
	/** Tag for running particle relaxation for the initially body-fitted distribution */
	sph_system.run_particle_relaxation_ = false;
	/** Tag for starting with relaxed body-fitted particles distribution */
	sph_system.reload_particles_ = false;
	/** Tag for computation from restart files. 0: start with initial condition */
	sph_system.restart_step_ = 0;
	/** Define external force.*/
	Gravity gravity(Vecd(0.0, -gravity_g));
	/** Handle command line arguments. */
	sph_system.handleCommandlineOptions(ac, av);
	/** I/O environment. */
	In_Output 	in_output(sph_system);
	//----------------------------------------------------------------------
	//	Creating body, materials and particles.
	//----------------------------------------------------------------------
	ThinStructure upperPlate(sph_system, "UpperPlate", makeShared<SPHAdaptation>(1.15, 1.0));
	ShellParticles upperPlate_particles(upperPlate,
								makeShared<LinearElasticSolid>(rho0_s, Youngs_modulus, poisson),
								makeShared<UpperPlateParticleGenerator>(), PT);

	/** Creat a plate body. */
	ThinStructure wall_boundary(sph_system, "Wall", makeShared<SPHAdaptation>(1.15, 1.0));
	/** Creat particles for the elastic body. */
	ShellParticles wall_particles(wall_boundary,
								makeShared<LinearElasticSolid>(rho0_s, Youngs_modulus, poisson),
								makeShared<PlateParticleGeneratorWall>(), PT);
	wall_particles.addAVariableToWrite<Vecd>("PriorAcceleration");
	//----------------------------------------------------------------------
	//	Define body relation map.
	//	The contact map gives the topological connections between the bodies.
	//	Basically the the range of bodies to build neighbor particle lists.
	//----------------------------------------------------------------------
	BodyRelationInner upperPlate_inner(upperPlate);
	BodyRelationInner wall_inner(wall_boundary);
	SolidBodyRelationContact upperPlate_contact(upperPlate, {&wall_boundary});
	SolidBodyRelationContact wall_upperPlate_contact(wall_boundary, {&upperPlate});
	// wall_particles.addAVariableToWrite<Vec3d>("PriorAcceleration");
	upperPlate_particles.addAVariableToWrite<Vec3d>("PriorAcceleration");
	//----------------------------------------------------------------------
	//	Define the main numerical methods used in the simultion.
	//	Note that there may be data dependence on the constructors of these methods.
	//----------------------------------------------------------------------
	TimeStepInitialization 	upperPlate_initialize_timestep(upperPlate, gravity);
	TimeStepInitialization 	wall_initialize_timestep(wall_boundary);
	thin_structure_dynamics::ShellCorrectConfiguration upperPlate_corrected_configuration(upperPlate_inner);
	thin_structure_dynamics::ShellCorrectConfiguration wall_corrected_configuration(wall_inner);
	thin_structure_dynamics::ShellAcousticTimeStepSize upperPlate_get_time_step_size(upperPlate);
	/** stress relaxation for the upperPlates. */
	thin_structure_dynamics::ShellStressRelaxationFirstHalf upperPlate_stress_relaxation_first_half(upperPlate_inner);
	thin_structure_dynamics::ShellStressRelaxationSecondHalf upperPlate_stress_relaxation_second_half(upperPlate_inner);
	thin_structure_dynamics::ShellStressRelaxationFirstHalf wall_stress_relaxation_first_half(wall_inner);
	thin_structure_dynamics::ShellStressRelaxationSecondHalf wall_stress_relaxation_second_half(wall_inner);
	/** Algorithms for solid-solid contact. */
	solid_dynamics::ShellContactDensity upperPlate_update_contact_density(upperPlate_contact);
	solid_dynamics::ShellContactDensity wall_upperPlate_update_contact_density(wall_upperPlate_contact);
	solid_dynamics::ShellShellContactForce upperPlate_compute_solid_contact_forces(upperPlate_contact);
	solid_dynamics::ShellShellContactForce wall_compute_solid_contact_forces(wall_upperPlate_contact);
	/** Damping */
	DampingWithRandomChoice<DampingPairwiseInner<Vec2d>>
		upperPlate_position_damping(upperPlate_inner, 0.2, "Velocity", physical_viscosity);
	DampingWithRandomChoice<DampingPairwiseInner<Vec2d>>
		upperPlate_rotation_damping(upperPlate_inner, 0.2, "AngularVelocity", physical_viscosity);

	DampingWithRandomChoice<DampingPairwiseInner<Vec2d>>
		wall_position_damping(wall_inner, 0.2, "Velocity", physical_viscosity);
	DampingWithRandomChoice<DampingPairwiseInner<Vec2d>>
		wall_rotation_damping(wall_inner, 0.2, "AngularVelocity", physical_viscosity);
	/** Constrain the Boundary. */
	BoundaryGeometry boundary_geometry(wall_boundary, "BoundaryGeometry");
	thin_structure_dynamics::ConstrainShellBodyRegion constrain_holder(wall_boundary, boundary_geometry);
	// thin_structure_dynamics::ConstrainShellBodyRegion	constrain_holder(wall_boundary, holder);
	//----------------------------------------------------------------------
	//	Define the methods for I/O operations and observations of the simulation.
	//----------------------------------------------------------------------
	BodyStatesRecordingToVtp	body_states_recording(in_output, sph_system.real_bodies_);
	//----------------------------------------------------------------------
	//	Prepare the simulation with cell linked list, configuration
	//	and case specified initial condition if necessary. 
	//----------------------------------------------------------------------
	sph_system.initializeSystemCellLinkedLists();
	sph_system.initializeSystemConfigurations();
	wall_particles.initializeNormalDirectionFromBodyShape();
	upperPlate_corrected_configuration.parallel_exec();
	wall_corrected_configuration.parallel_exec();
	
	/** Initial states output. */
	body_states_recording.writeToFile(0);
	/** Main loop. */
	int ite 		= 0;
	Real T0 		= 10.0;
	Real End_Time 	= T0;
	Real D_Time 	= 0.01*T0;
	Real Dt 		= 0.1*D_Time;			
	Real dt 		= 0.0; 	
	//----------------------------------------------------------------------
	//	Statistics for CPU time
	//----------------------------------------------------------------------
	tick_count t1 = tick_count::now();
	tick_count::interval_t interval;
	//----------------------------------------------------------------------
	//	Main loop starts here.
	//----------------------------------------------------------------------
	while (GlobalStaticVariables::physical_time_ < End_Time)
	{
		Real integration_time = 0.0;
		while (integration_time < D_Time) 
		{
			Real relaxation_time = 0.0;
			while (relaxation_time < Dt) 
			{
				upperPlate_initialize_timestep.parallel_exec();
				wall_initialize_timestep.parallel_exec();
				if (ite % 100 == 0) 
				{
					std::cout << "N=" << ite << " Time: "
						<< GlobalStaticVariables::physical_time_ << "	dt: " << dt << "\n";
				}
				upperPlate_update_contact_density.parallel_exec();
				upperPlate_compute_solid_contact_forces.parallel_exec();

				wall_upperPlate_update_contact_density.parallel_exec();
				wall_compute_solid_contact_forces.parallel_exec();

				upperPlate_stress_relaxation_first_half.parallel_exec(dt);
				upperPlate_position_damping.parallel_exec(dt);
				upperPlate_rotation_damping.parallel_exec(dt);
				upperPlate_stress_relaxation_second_half.parallel_exec(dt);
				
				wall_stress_relaxation_first_half.parallel_exec(dt);
				constrain_holder.parallel_exec(dt);
				wall_position_damping.parallel_exec(dt);
				wall_rotation_damping.parallel_exec(dt);
				constrain_holder.parallel_exec(dt);
				wall_stress_relaxation_second_half.parallel_exec(dt);

				upperPlate.updateCellLinkedList();
				upperPlate_contact.updateConfiguration();
				wall_boundary.updateCellLinkedList();
				wall_upperPlate_contact.updateConfiguration();

				ite++;
				Real dt_free = upperPlate_get_time_step_size.parallel_exec();
				dt = dt_free;
				relaxation_time += dt;
				integration_time += dt;
				GlobalStaticVariables::physical_time_ += dt;
			}
		}
		tick_count t2 = tick_count::now();
		body_states_recording.writeToFile(ite);
		tick_count t3 = tick_count::now();
		interval += t3 - t2;
	}
	tick_count t4 = tick_count::now();

	tick_count::interval_t tt;
	tt = t4 - t1 - interval;
	std::cout << "Total wall time for computation: " << tt.seconds() << " seconds." << std::endl;
	return 0;
}
