/**
 * @file 	2d_flow_around_cylinder.cpp
 * @brief 	This is the benchmark test for the wall modeling of viscous flow.
 * @details We consider a flow passing by a cylinder in 2D.
 * @author 	Xiangyu Hu
 */
#include "sphinxsys.h"
#include "2d_flow_around_cylinder.h"

using namespace SPH;

int main(int ac, char *av[])
{
	//----------------------------------------------------------------------
	//	Build up the environment of a SPHSystem.
	//----------------------------------------------------------------------
	SPHSystem sph_system(system_domain_bounds, resolution_ref);
	/** Tag for run particle relaxation for the initial body fitted distribution. */
	sph_system.setRunParticleRelaxation(false);
	/** Tag for computation start with relaxed body fitted particles distribution. */
	sph_system.setReloadParticles(true);
// handle command line arguments
#ifdef BOOST_AVAILABLE
	sph_system.handleCommandlineOptions(ac, av);
#endif
	IOEnvironment io_environment(sph_system);
	ParameterizationIO &parameterization_io = io_environment.defineParameterizationIO();
	//----------------------------------------------------------------------
	//	Creating body, materials and particles.
	//----------------------------------------------------------------------
	FluidBody water_block(sph_system, makeShared<WaterBlock>("WaterBlock"));
	water_block.defineParticlesAndMaterial<FluidParticles, ParameterizedWaterMaterial>(parameterization_io, rho0_f, c_f, mu_f);
	water_block.generateParticles<ParticleGeneratorLattice>();

	SolidBody cylinder(sph_system, makeShared<Cylinder>("Cylinder"));
	cylinder.defineAdaptationRatios(1.15, 2.0);
	cylinder.defineBodyLevelSetShape();
	cylinder.defineParticlesAndMaterial<SolidParticles, Solid>();
	(!sph_system.RunParticleRelaxation() && sph_system.ReloadParticles())
		? cylinder.generateParticles<ParticleGeneratorReload>(io_environment, cylinder.getName())
		: cylinder.generateParticles<ParticleGeneratorLattice>();

	ObserverBody fluid_observer(sph_system, "FluidObserver");
	fluid_observer.generateParticles<ObserverParticleGenerator>(observation_locations);
	//----------------------------------------------------------------------
	//	Run particle relaxation for body-fitted distribution if chosen.
	//----------------------------------------------------------------------
	if (sph_system.RunParticleRelaxation())
	{
		/** body topology only for particle relaxation */
		InnerRelation cylinder_inner(cylinder);
		//----------------------------------------------------------------------
		//	Methods used for particle relaxation.
		//----------------------------------------------------------------------
		/** Random reset the insert body particle position. */
		SimpleDynamics<RandomizeParticlePosition> random_inserted_body_particles(cylinder);
		/** Write the body state to Vtp file. */
		BodyStatesRecordingToVtp write_inserted_body_to_vtp(io_environment, {&cylinder});
		/** Write the particle reload files. */
		ReloadParticleIO write_particle_reload_files(io_environment, cylinder);
		/** A  Physics relaxation step. */
		relax_dynamics::RelaxationStepInner relaxation_step_inner(cylinder_inner);
		//----------------------------------------------------------------------
		//	Particle relaxation starts here.
		//----------------------------------------------------------------------
		random_inserted_body_particles.parallel_exec(0.25);
		relaxation_step_inner.SurfaceBounding().parallel_exec();
		sph_system.updateSystemCellLinkedLists();
		sph_system.updateSystemRelations();
		//----------------------------------------------------------------------
		//	First output before the simulation.
		//----------------------------------------------------------------------
		write_inserted_body_to_vtp.writeToFile(0);

		int ite_p = 0;
		while (ite_p < 1000)
		{
			relaxation_step_inner.parallel_exec();
			ite_p += 1;
			if (ite_p % 200 == 0)
			{
				std::cout << std::fixed << std::setprecision(9) << "Relaxation steps for the inserted body N = " << ite_p << "\n";
				write_inserted_body_to_vtp.writeToFile(ite_p);
			}
			sph_system.updateSystemCellLinkedLists();
			sph_system.updateSystemRelations();
		}
		std::cout << "The physics relaxation process of the cylinder finish !" << std::endl;

		/** Output results. */
		write_particle_reload_files.writeToFile(0);
		return 0;
	}
	//----------------------------------------------------------------------
	//	Define body relation map.
	//	The contact map gives the topological connections between the bodies.
	//	Basically the the range of bodies to build neighbor particle lists.
	//----------------------------------------------------------------------
	InnerRelation water_block_inner(water_block);
	ContactRelation water_block_contact(water_block, {&cylinder});
	ContactRelation cylinder_contact(cylinder, {&water_block});
	ContactRelation fluid_observer_contact(fluid_observer, {&water_block});

	ComplexRelation water_block_complex(water_block_inner, water_block_contact);
	//----------------------------------------------------------------------
	//	Define the main numerical methods used in the simulation.
	//	Note that there may be data dependence on the constructors of these methods.
	//----------------------------------------------------------------------
	SimpleDynamics<NormalDirectionFromBodyShape> cylinder_normal_direction(cylinder);
	/** Initialize particle acceleration. */
	SimpleDynamics<TimeStepInitialization> initialize_a_fluid_step(water_block);
	/** Periodic BCs in x direction. */
	PeriodicConditionUsingCellLinkedList periodic_condition_x(water_block, water_block.getBodyShapeBounds(), xAxis);
	/** Periodic BCs in y direction. */
	PeriodicConditionUsingCellLinkedList periodic_condition_y(water_block, water_block.getBodyShapeBounds(), yAxis);
	/** Evaluation of density by summation approach. */
	InteractionWithUpdate<fluid_dynamics::DensitySummationComplex> update_density_by_summation(water_block_complex);
	/** Time step size without considering sound wave speed. */
	ReduceDynamics<fluid_dynamics::AdvectionTimeStepSize> get_fluid_advection_time_step_size(water_block, U_f);
	/** Time step size with considering sound wave speed. */
	ReduceDynamics<fluid_dynamics::AcousticTimeStepSize> get_fluid_time_step_size(water_block);
	/** Pressure relaxation using Verlet time stepping. */
	/** Here, we do not use Riemann solver for pressure as the flow is viscous. */
	Dynamics1Level<fluid_dynamics::Integration1stHalfRiemannWithWall> pressure_relaxation(water_block_complex);
	Dynamics1Level<fluid_dynamics::Integration2ndHalfWithWall> density_relaxation(water_block_complex);
	/** Computing viscous acceleration with wall. */
	InteractionDynamics<fluid_dynamics::ViscousAccelerationWithWall> viscous_acceleration(water_block_complex);
	/** Impose transport velocity. */
	InteractionDynamics<fluid_dynamics::TransportVelocityCorrectionComplex> transport_velocity_correction(water_block_complex);
	/** Computing vorticity in the flow. */
	InteractionDynamics<fluid_dynamics::VorticityInner> compute_vorticity(water_block_complex.getInnerRelation());
	/** free stream boundary condition. */
	BodyRegionByCell free_stream_buffer(water_block, makeShared<MultiPolygonShape>(createBufferShape()));
	SimpleDynamics<FreeStreamCondition, BodyRegionByCell> freestream_condition(free_stream_buffer);
	//----------------------------------------------------------------------
	//	Algorithms of FSI.
	//----------------------------------------------------------------------
	/** Compute the force exerted on solid body due to fluid pressure and viscosity. */
	InteractionDynamics<solid_dynamics::FluidViscousForceOnSolid> viscous_force_on_cylinder(cylinder_contact);
	InteractionDynamics<solid_dynamics::FluidPressureForceOnSolid> pressure_force_on_cylinder(cylinder_contact);
	/** Computing viscous force acting on wall with wall model. */
	//----------------------------------------------------------------------
	//	Define the methods for I/O operations and observations of the simulation.
	//----------------------------------------------------------------------
	BodyStatesRecordingToVtp write_real_body_states(io_environment, sph_system.real_bodies_);
	RegressionTestTimeAveraged<ReducedQuantityRecording<ReduceDynamics<solid_dynamics::TotalViscousForceOnSolid>>>
		write_total_viscous_force_on_inserted_body(io_environment, cylinder);
	ReducedQuantityRecording<ReduceDynamics<solid_dynamics::TotalViscousForceOnSolid>>
		write_total_force_on_inserted_body(io_environment, cylinder);
	ObservedQuantityRecording<Vecd>
		write_fluid_velocity("Velocity", io_environment, fluid_observer_contact);
	//----------------------------------------------------------------------
	//	Prepare the simulation with cell linked list, configuration
	//	and case specified initial condition if necessary.
	//----------------------------------------------------------------------
	/** initialize cell linked lists for all bodies. */
	sph_system.updateSystemCellLinkedLists();
	/** periodic condition applied after the mesh cell linked list build up
	 * but before the configuration build up. */
	periodic_condition_x.update_cell_linked_list_.parallel_exec();
	periodic_condition_y.update_cell_linked_list_.parallel_exec();
	/** initialize configurations for all bodies. */
	sph_system.updateSystemRelations();
	/** initialize surface normal direction for the insert body. */
	cylinder_normal_direction.parallel_exec();
	//----------------------------------------------------------------------
	//	Setup computing and initial conditions.
	//----------------------------------------------------------------------
	size_t number_of_iterations = 0;
	int screen_output_interval = 100;
	Real end_time = 200.0;
	Real output_interval = end_time / 200.0;
	//----------------------------------------------------------------------
	//	Statistics for CPU time
	//----------------------------------------------------------------------
	tick_count t1 = tick_count::now();
	tick_count::interval_t interval;
	//----------------------------------------------------------------------
	//	First output before the main loop.
	//----------------------------------------------------------------------
	write_real_body_states.writeToFile();
	//----------------------------------------------------------------------
	//	Main loop starts here.
	//----------------------------------------------------------------------
	while (GlobalStaticVariables::physical_time_ < end_time)
	{
		Real integration_time = 0.0;

		/** Integrate time (loop) until the next output time. */
		while (integration_time < output_interval)
		{
			initialize_a_fluid_step.parallel_exec();
			Real Dt = get_fluid_advection_time_step_size.parallel_exec();
			update_density_by_summation.parallel_exec();
			viscous_acceleration.parallel_exec();
			transport_velocity_correction.parallel_exec();

			/** FSI for viscous force. */
			viscous_force_on_cylinder.parallel_exec();
			size_t inner_ite_dt = 0;
			Real relaxation_time = 0.0;
			while (relaxation_time < Dt)
			{
				Real dt = SMIN(get_fluid_time_step_size.parallel_exec(), Dt);
				/** Fluid pressure relaxation, first half. */
				pressure_relaxation.parallel_exec(dt);
				/** FSI for pressure force. */
				pressure_force_on_cylinder.parallel_exec();
				/** Fluid pressure relaxation, second half. */
				density_relaxation.parallel_exec(dt);

				relaxation_time += dt;
				integration_time += dt;
				GlobalStaticVariables::physical_time_ += dt;
				freestream_condition.parallel_exec();
				inner_ite_dt++;
			}

			if (number_of_iterations % screen_output_interval == 0)
			{
				std::cout << std::fixed << std::setprecision(9) << "N=" << number_of_iterations << "	Time = "
						  << GlobalStaticVariables::physical_time_
						  << "	Dt = " << Dt << "	Dt / dt = " << inner_ite_dt << "\n";
			}
			number_of_iterations++;

			/** Water block configuration and periodic condition. */
			periodic_condition_x.bounding_.parallel_exec();
			periodic_condition_y.bounding_.parallel_exec();
			sph_system.updateSystemCellLinkedLists();
			periodic_condition_x.update_cell_linked_list_.parallel_exec();
			periodic_condition_y.update_cell_linked_list_.parallel_exec();
			/** one need update configuration after periodic condition. */
			sph_system.updateSystemRelations();
		}

		tick_count t2 = tick_count::now();
		/** write run-time observation into file */
		compute_vorticity.parallel_exec();
		write_real_body_states.writeToFile();
		write_total_viscous_force_on_inserted_body.writeToFile(number_of_iterations);
		write_total_force_on_inserted_body.writeToFile(number_of_iterations);
		fluid_observer_contact.updateConfiguration();
		write_fluid_velocity.writeToFile(number_of_iterations);

		tick_count t3 = tick_count::now();
		interval += t3 - t2;
	}
	tick_count t4 = tick_count::now();

	tick_count::interval_t tt;
	tt = t4 - t1 - interval;
	std::cout << "Total wall time for computation: " << tt.seconds() << " seconds." << std::endl;

	write_total_viscous_force_on_inserted_body.newResultTest();

	return 0;
}
