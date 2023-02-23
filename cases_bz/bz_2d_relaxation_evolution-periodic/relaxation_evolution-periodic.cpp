/**
 * @file 	relaxation_evolution.cpp
 * @brief 	This is the first case by testing the relaxation with evolution method.
 * @author 	Bo Zhang
 */
#include "sphinxsys.h"
using namespace SPH;
//----------------------------------------------------------------------
//	Basic geometry parameters and numerical setup.
//----------------------------------------------------------------------
Real LL = 1.0;					
Real LH = 1.0;
Real resolution_ref = LL / 40.0;
Real BW = resolution_ref * 2.0;
BoundingBox system_domain_bounds(Vec2d(-BW - LL, -BW - LH), Vec2d(LL+ BW, LH + BW));
//----------------------------------------------------------------------
//	Define geometries
//----------------------------------------------------------------------
Vec2d water_block_halfsize = Vec2d(0.5 * LL, 0.5 * LH);
Vec2d water_block_translation = water_block_halfsize;
class Insert : public ComplexShape
{
public:
	explicit Insert(const std::string& shape_name) : ComplexShape(shape_name)
	{
		add<TransformShape<GeometricShapeBox>>(Transform2d(water_block_halfsize), water_block_translation);
	}
};

int main(int ac, char *av[])
{
	//----------------------------------------------------------------------
	//	Build up the environment of a SPHSystem with global controls.
	//----------------------------------------------------------------------
	SPHSystem sph_system(system_domain_bounds, resolution_ref);
	sph_system.setRunParticleRelaxation(true);
	IOEnvironment io_environment(sph_system);
	//----------------------------------------------------------------------
	//	Creating body, materials and particles.
	//----------------------------------------------------------------------
	SolidBody body(sph_system, makeShared<Insert>("InsertedBody"));
	body.defineBodyLevelSetShape()->writeLevelSet(io_environment);
	body.defineParticlesAndMaterial();
	body.addBodyStateForRecording<Vecd>("Position");
	(!sph_system.RunParticleRelaxation() && sph_system.ReloadParticles())
		? body.generateParticles<ParticleGeneratorReload>(io_environment, body.getName())
		: body.generateParticles<ParticleGeneratorLattice>();
	//----------------------------------------------------------------------
	//	Define body relation map.
	//	The contact map gives the topological connections between the bodies.
	//	Basically the the range of bodies to build neighbor particle lists.
	//----------------------------------------------------------------------
	InnerRelation insert_body_inner(body);
	//----------------------------------------------------------------------
	//	Run particle relaxation for body-fitted distribution if chosen.
	//----------------------------------------------------------------------
	if (sph_system.RunParticleRelaxation())
	{
		//----------------------------------------------------------------------
		//	Methods used for particle relaxation.
		//----------------------------------------------------------------------
		/** Random reset the insert body particle position. */
		SimpleDynamics<RandomizeParticlePosition> random_insert_body_particles(body);
		/** Write the body state to Vtp file. */
		BodyStatesRecordingToVtp write_insert_body_to_vtp(io_environment, {&body});
		/** Write the particle reload files. */
		ReloadParticleIO write_particle_reload_files(io_environment, {&body});
		/** A  Physics relaxation step. */
		relax_dynamics::RelaxationEvolutionInner relaxation_inner_implicit(insert_body_inner);
		relax_dynamics::RelaxationStepInner relaxation_inner_explicit(insert_body_inner);
		InteractionDynamics<relax_dynamics::UpdateParticleKineticEnergy> update_kinetic_energy(insert_body_inner);
		ReduceAverage<QuantitySummation<Real>> average_residue(body, "residue");
		body.addBodyStateForRecording<Real>("residue");
		relax_dynamics::ModificationStepForConsistency modification_step_for_consistency(insert_body_inner);

		PeriodicConditionUsingCellLinkedList periodic_condition_x(body, body.getBodyShapeBounds(), xAxis);
		PeriodicConditionUsingCellLinkedList periodic_condition_y(body, body.getBodyShapeBounds(), yAxis);
		//----------------------------------------------------------------------
		//	Particle relaxation starts here.
		//----------------------------------------------------------------------
		random_insert_body_particles.parallel_exec(0.25);
		sph_system.initializeSystemCellLinkedLists();
		periodic_condition_x.update_cell_linked_list_.parallel_exec();
		periodic_condition_y.update_cell_linked_list_.parallel_exec();
		sph_system.initializeSystemConfigurations();
		relaxation_inner_explicit.SurfaceBounding().parallel_exec();
		write_insert_body_to_vtp.writeToFile(0);
		//----------------------------------------------------------------------
		//	Relax particles of the insert body.
		//----------------------------------------------------------------------
		std::string filefullpath_residue = io_environment.output_folder_ + "/" + "residue.dat";
		std::ofstream out_file_residue(filefullpath_residue.c_str(), std::ios::app);

		int ite_p = 0;
		Real dt = 1.0 / 200.0; 
		while (ite_p < 2000)
		{
			periodic_condition_x.bounding_.parallel_exec();
			periodic_condition_y.bounding_.parallel_exec();
			body.updateCellLinkedList();
			periodic_condition_x.update_cell_linked_list_.parallel_exec();
			periodic_condition_y.update_cell_linked_list_.parallel_exec();
			insert_body_inner.updateConfiguration();
			relaxation_inner_explicit.parallel_exec(dt);

			if (ite_p == 0)
			{
				update_kinetic_energy.parallel_exec();
				out_file_residue << std::fixed << std::setprecision(12) << 0 << "   " << average_residue.parallel_exec() << "\n";
			}
			
			ite_p += 1;
			if (ite_p % 50 == 0)
			{
				std::cout << std::fixed << std::setprecision(9) << "Relaxation steps for the inserted body N = " << ite_p << "\n";
				update_kinetic_energy.parallel_exec();
				out_file_residue << std::fixed << std::setprecision(12) << ite_p << "   " << average_residue.parallel_exec() << "\n";
				write_insert_body_to_vtp.writeToFile(ite_p);
			}
		}
		std::cout << "The physics relaxation process of inserted body finish !" << std::endl;
		/** Output results. */
		write_particle_reload_files.writeToFile(0);
		return 0;
	}
}
