/**
 * @file 	diffusion_heat_flux.cpp
 * @brief 	This is the second test to validate the optimization.
 * @author 	Bo Zhang and Xiangyu Hu
 */
#include "sphinxsys.h" //SPHinXsys Library
using namespace SPH; //Namespace cite here
//----------------------------------------------------------------------
//	Basic geometry parameters and numerical setup.
//----------------------------------------------------------------------
Real L = 1.0; 	
Real H = 1.0;
Real resolution_ref = H / 100.0;
Real BW = resolution_ref * 2.0;
BoundingBox system_domain_bounds(Vec2d(-BW, -BW), Vec2d(L + BW, H + BW));
//----------------------------------------------------------------------
//	Basic parameters for material properties.
//----------------------------------------------------------------------
Real diffusion_coff = 1;
std::array<std::string, 1> species_name_list{ "Phi" };
//----------------------------------------------------------------------
//	Initial and boundary conditions.
//----------------------------------------------------------------------
Real initial_temperature = 0.0;
Real left_temperature = 300.0;
Real right_temperature = 350.0;
Real heat_flux = 2000.0;
//----------------------------------------------------------------------
//	Geometric shapes used in the system.
//----------------------------------------------------------------------
std::vector<Vecd> createThermalDomain()
{
	std::vector<Vecd> thermalDomainShape;
	thermalDomainShape.push_back(Vecd(0.0, 0.0));
	thermalDomainShape.push_back(Vecd(0.0, H));
	thermalDomainShape.push_back(Vecd(L, H));
	thermalDomainShape.push_back(Vecd(L, 0.0));
	thermalDomainShape.push_back(Vecd(0.0, 0.0));

	return thermalDomainShape;
}

std::vector<Vecd> createBoundaryDomain()
{
	std::vector<Vecd> boundaryDomain;
	boundaryDomain.push_back(Vecd(-BW, -BW));
	boundaryDomain.push_back(Vecd(-BW, H + BW));
	boundaryDomain.push_back(Vecd(L + BW, H + BW));
	boundaryDomain.push_back(Vecd(L + BW, -BW));
	boundaryDomain.push_back(Vecd(-BW, -BW));

	return boundaryDomain;
}

std::vector<Vecd> left_temperature_region
{
	Vecd(0.3 * L, 0), Vecd(0.3 * L, BW), Vecd(0.4 * L, BW),
	Vecd(0.4 * L, 0), Vecd(0.3 * L, 0)
};

std::vector<Vecd> right_temperature_region
{
	Vecd(0.6 * L, 0), Vecd(0.6 * L, BW), Vecd(0.7 * L, BW),
	Vecd(0.7 * L, 0), Vecd(0.6 * L, 0)
};

std::vector<Vecd> heat_flux_region
{
	Vecd(0.45 * L, 0), Vecd(0.45 * L, resolution_ref), Vecd(0.55 * L, resolution_ref),
	Vecd(0.55 * L, 0), Vecd(0.45 * L, 0)
};
//----------------------------------------------------------------------
//	Define SPH bodies. 
//----------------------------------------------------------------------
class DiffusionBody : public MultiPolygonShape
{
public:
	explicit DiffusionBody(const std::string& shape_name) : MultiPolygonShape(shape_name)
	{
		multi_polygon_.addAPolygon(createThermalDomain(), ShapeBooleanOps::add);
	}
};

class WallBoundary : public MultiPolygonShape
{
public:
	explicit WallBoundary(const std::string& shape_name) : MultiPolygonShape(shape_name)
	{
		multi_polygon_.addAPolygon(createBoundaryDomain(), ShapeBooleanOps::add);
		multi_polygon_.addAPolygon(createThermalDomain(), ShapeBooleanOps::sub);
	}
};

MultiPolygon createBoundayConditionRegion()
{
	MultiPolygon multi_polygon;
	multi_polygon.addAPolygon(left_temperature_region, ShapeBooleanOps::add);
	multi_polygon.addAPolygon(right_temperature_region, ShapeBooleanOps::add);
	multi_polygon.addAPolygon(heat_flux_region, ShapeBooleanOps::add);
	return multi_polygon;
}

MultiPolygon createHeatFluxRegion()
{
	MultiPolygon multi_polygon;
	multi_polygon.addAPolygon(heat_flux_region, ShapeBooleanOps::add);
	return multi_polygon;
}
//----------------------------------------------------------------------
//	Setup diffusion material properties. 
//----------------------------------------------------------------------
class DiffusionBodyMaterial
	:public DiffusionReaction<Solid>
{
public:
	DiffusionBodyMaterial()
		:DiffusionReaction<Solid>(species_name_list)
	{
		initializeAnDiffusion<LocalDirectionalDiffusion>("Phi", "Phi", diffusion_coff);
	}
};

//----------------------------------------------------------------------
//	Application dependent initial condition. 
//----------------------------------------------------------------------
class DiffusionBodyInitialCondition
	: public DiffusionReactionInitialCondition<SolidParticles, Solid>
{
protected:
	size_t phi_;
	void update(size_t index_i, Real dt)
	{
		species_n_[phi_][index_i] = 400 + 50 * (double)rand() / RAND_MAX;
	};
public:
	DiffusionBodyInitialCondition(SolidBody &diffusion_body) :
		DiffusionReactionInitialCondition<SolidParticles, Solid>(diffusion_body)
	{
		phi_ = particles_->diffusion_reaction_material_.SpeciesIndexMap()["Phi"];
	}
};

class WallBoundaryInitialCondition
	: public DiffusionReactionInitialCondition<SolidParticles, Solid>
{
protected:
	size_t phi_;

	void update(size_t index_i, Real dt)
	{
		species_n_[phi_][index_i] = -0.0;
		if (pos_[index_i][1] > H && pos_[index_i][0] > 
			0.3*L && pos_[index_i][0] < 0.4*L)
		{
			species_n_[phi_][index_i] = left_temperature;
		}
		if (pos_[index_i][1] > H && pos_[index_i][0] > 
			0.6*L && pos_[index_i][0] < 0.7*L)
		{
			species_n_[phi_][index_i] = right_temperature;
		}
		if (pos_[index_i][1] < 0 && pos_[index_i][0] > 
			0.45*L && pos_[index_i][0] < 0.55*L)
		{
			heat_flux_[index_i] = heat_flux;
			species_n_[phi_][index_i] = 400;
		}
	}
public:
	WallBoundaryInitialCondition(SolidBody &diffusion_body) :
		DiffusionReactionInitialCondition<SolidParticles, Solid>(diffusion_body)
	{
		phi_ = particles_->diffusion_reaction_material_.SpeciesIndexMap()["Phi"];
	}
};

//----------------------------------------------------------------------
//	Specify diffusion relaxation method. 
//----------------------------------------------------------------------
class DiffusionBodyRelaxation
	:public RelaxationOfAllDiffusionSpeciesRK2<
	RelaxationOfAllDiffusionSpeciesWithBC<SolidParticles, Solid, SolidParticles, Solid>>
{
public:
	DiffusionBodyRelaxation(ComplexRelation& body_complex_relation)
		:RelaxationOfAllDiffusionSpeciesRK2(body_complex_relation) {};
	virtual ~DiffusionBodyRelaxation() {};
};
//----------------------------------------------------------------------
//	An observer body to measure temperature at given positions. 
//----------------------------------------------------------------------
class TemperatureObserverParticleGenerator : public ObserverParticleGenerator
{
public:
	TemperatureObserverParticleGenerator(SPHBody &sph_body) : ObserverParticleGenerator(sph_body)
	{
		/** A line of measuring points at the middle line. */
		size_t number_of_observation_points = 100;
		Real range_of_measure = L;
		Real start_of_measure = 0;

		for (size_t i = 0; i < number_of_observation_points; ++i)
		{
			Vec2d point_coordinate(0.5*L, range_of_measure * Real(i) / 
				Real(number_of_observation_points - 1) + start_of_measure);
			positions_.push_back(point_coordinate);
		}
	}
};

//----------------------------------------------------------------------
//	Main program starts here.
//----------------------------------------------------------------------
int main(int ac, char* av[])
{
	//----------------------------------------------------------------------
	//	Build up the environment of a SPHSystem.
	//----------------------------------------------------------------------
	SPHSystem sph_system(system_domain_bounds, resolution_ref);
	IOEnvironment io_environment(sph_system);
	//----------------------------------------------------------------------
	//	Creating body, materials and particles.
	//----------------------------------------------------------------------
	//---------------------------------------------------------------------- 
	SolidBody diffusion_body(sph_system, makeShared<DiffusionBody>("DiffusionBody"));
	diffusion_body.defineParticlesAndMaterial<DiffusionReactionParticles<SolidParticles, Solid>, DiffusionBodyMaterial>();
	diffusion_body.generateParticles<ParticleGeneratorLattice>();

	SolidBody wall_boundary(sph_system, makeShared<WallBoundary>("WallBoundary"));
	wall_boundary.defineParticlesAndMaterial<DiffusionReactionParticles<SolidParticles, Solid>, DiffusionBodyMaterial>();
	wall_boundary.generateParticles<ParticleGeneratorLattice>();

	BodyRegionByParticle BC_region(wall_boundary, makeShared<MultiPolygonShape>(createBoundayConditionRegion(), "BC_region"));
	BodyRegionByParticle heat_flux_region(wall_boundary, makeShared<MultiPolygonShape>(createHeatFluxRegion(), "heat_flux_region"));
	//----------------------------  ------------------------------------------
	//	Particle and body creation of temperature observers.
	//----------------------------------------------------------------------
	ObserverBody temperature_observer(sph_system, "TemperatureObserver");
	temperature_observer.generateParticles<TemperatureObserverParticleGenerator>();
	//----------------------------------------------------------------------
	//	Define body relation map.
	//	The contact map gives the topological connections between the bodies.
	//	Basically the range of bodies to build neighbor particle lists.
	//----------------------------------------------------------------------
	ComplexRelation diffusion_body_complex(diffusion_body, { &BC_region });
	ComplexRelation wall_boundary_complex(wall_boundary, { &diffusion_body });
	ContactRelation temperature_observer_contact(temperature_observer, { &diffusion_body });
	//----------------------------------------------------------------------
	//	Define the main numerical methods used in the simulation.
	//	Note that there may be data dependence on the constructors of these methods.
	//----------------------------------------------------------------------
	SimpleDynamics<DiffusionBodyInitialCondition> setup_diffusion_initial_condition(diffusion_body);
	SimpleDynamics<WallBoundaryInitialCondition> setup_boundary_condition(wall_boundary);
	GetDiffusionTimeStepSize<SolidParticles, Solid> get_time_step_size(diffusion_body);
	//----------------------------------------------------------------------
	//	Define the methods for I/O operations and observations of the simulation.
	//----------------------------------------------------------------------
	BodyStatesRecordingToVtp write_states(io_environment, sph_system.real_bodies_);
	RestartIO	restart_io(io_environment, sph_system.real_bodies_);
	ObservedQuantityRecording<Real>
		write_solid_temperature("Phi", io_environment, temperature_observer_contact);
	/************************************************************************/
	/* Splitting thermal diffusivity optimization                */
	/************************************************************************/
	DiffusionBodyRelaxation temperature_relaxation(diffusion_body_complex);
	InteractionDynamics<UpdateUnitNormalVector<SolidParticles, Solid, SolidParticles, Solid>>
		update_diffusion_body_normal_vector(diffusion_body_complex);
	InteractionDynamics<UpdateUnitNormalVector<SolidParticles, Solid, SolidParticles, Solid>>
		update_wall_boundary_normal_vector(wall_boundary_complex);
	InteractionSplit<TemperatureSplittingByBCWithBoundary<SolidParticles, 
		Solid, SolidParticles, Solid, Real>>
		temperature_splitting_bc(wall_boundary_complex, "Phi");
	InteractionSplit<UpdateTemperatureBCResidual<TemperatureSplittingByBCWithBoundary<SolidParticles, 
		Solid, SolidParticles, Solid, Real>, ComplexRelation, Real>>
		update_temperature_bc_residual(wall_boundary_complex, "Phi");
	InteractionSplit<DampingBySplittingWithWall<Real, DampingBySplittingInner>>
		temperature_damping(diffusion_body_complex, "Phi", diffusion_coff);
	InteractionSplit<TemperatureSplittingByPDEWithBoundary<SolidParticles, Solid,
		SolidParticles, Solid, Real>> 
		temperature_splitting_pde(diffusion_body_complex, "Phi");
	InteractionSplit<UpdateTemperaturePDEResidual<TemperatureSplittingByPDEWithBoundary<SolidParticles, 
		Solid, SolidParticles, Solid, Real>, ComplexRelation, Real>> 
		update_temperature_pde_residual(diffusion_body_complex, "Phi");
	ReduceAverage<ComputeAveragedErrorOrPositiveParameter<SolidParticles, Solid>>
		calculate_averaged_local_residual(diffusion_body, "residual_T_local");
	ReduceAverage<ComputeAveragedErrorOrPositiveParameter<SolidParticles, Solid>>
		calculate_averaged_global_residual(diffusion_body, "residual_T_global");
	ReduceDynamics<ComputeMaximumError<SolidParticles, Solid>>
		calculate_maximum_local_residual(diffusion_body, "residual_T_local");
	ReduceDynamics<ComputeMaximumError<SolidParticles, Solid>>
		calculate_maximum_global_residual(diffusion_body, "residual_T_global");
	ReduceAverage<DiffusionReactionSpeciesSummation<SolidParticles, Solid>, BodyRegionByParticle>
		calculate_averaged_temperature(heat_flux_region, "Phi");
	//----------------------------------------------------------------------
	//	Prepare the simulation with cell linked list, configuration
	//	and case specified initial condition if necessary. 
	//----------------------------------------------------------------------
	sph_system.initializeSystemCellLinkedLists(); 
	sph_system.initializeSystemConfigurations();
	setup_diffusion_initial_condition.parallel_exec();
	setup_boundary_condition.parallel_exec();
	update_diffusion_body_normal_vector.parallel_exec();
	update_wall_boundary_normal_vector.parallel_exec();
	//----------------------------------------------------------------------
	//	Load restart file if necessary.
	//----------------------------------------------------------------------
	if (sph_system.RestartStep() != 0)
	{
		GlobalStaticVariables::physical_time_ = restart_io.readRestartFiles(sph_system.RestartStep());
		diffusion_body.updateCellLinkedList();
		diffusion_body_complex.updateConfiguration();
	}
	//----------------------------------------------------------------------
	//	Setup for time-stepping control
	//----------------------------------------------------------------------
	int ite = sph_system.RestartStep();
	Real T0 = 100;
	Real End_Time = T0;
	Real Observe_time = 0.01 * End_Time;
	int restart_output_interval = 1000;
	Vecd averaged_residual_T(0.0, 0.0);
	Vecd maximum_residual_T(0.0, 0.0);
	Real dt = 0.0;
	//----------------------------------------------------------------------
	//	Statistics for CPU time
	//----------------------------------------------------------------------
	tick_count t1 = tick_count::now();
	tick_count::interval_t interval;
	//----------------------------------------------------------------------
	//	Main loop starts here.
	//----------------------------------------------------------------------
	std::string filefullpath_PDE_error = io_environment.output_folder_ + "/" + "PDE_residual.dat";
	std::ofstream out_file_PDE_error(filefullpath_PDE_error.c_str(), std::ios::app);
	std::string filefullpath_averaged_temperature = io_environment.output_folder_ + "/" + "averaged_temperature.dat";
	std::ofstream out_file_averaged_temperature(filefullpath_averaged_temperature.c_str(), std::ios::app);

	while (maximum_residual_T[1] > 1e-3 * 50)
	{
		dt = get_time_step_size.parallel_exec();
		if (ite % 500 == 0)
		{
			write_states.writeToFile(ite);
			write_solid_temperature.writeToFile(ite);
			std::cout << "N= " << ite << " Time: " << GlobalStaticVariables::physical_time_ << "	dt: " << dt << "\n";
			out_file_PDE_error << std::fixed << std::setprecision(9) << ite << "residual" << averaged_residual_T[1] << "\n";
			out_file_averaged_temperature << std::fixed << std::setprecision(9) << ite << "residual" << calculate_averaged_temperature.parallel_exec() << "\n";
		}

		temperature_relaxation.parallel_exec(dt);

		temperature_splitting_bc.parallel_exec(dt);
		//temperature_splitting_pde.parallel_exec(dt);
		//temperature_damping.parallel_exec(dt);

		averaged_residual_T[0] = calculate_averaged_local_residual.parallel_exec(dt);
		maximum_residual_T[0] = calculate_maximum_local_residual.parallel_exec(dt);
		update_temperature_pde_residual.parallel_exec(dt);
		maximum_residual_T[1] = calculate_maximum_global_residual.parallel_exec(dt);

		ite++; GlobalStaticVariables::physical_time_ += dt;

		if (ite % restart_output_interval == 0)
		{
			restart_io.writeToFile(ite);
		}
	}
	out_file_PDE_error.close();
	out_file_averaged_temperature.close();

	tick_count t4 = tick_count::now();
	tick_count::interval_t tt;
	tt = t4 - t1;
	std::cout << "Total wall time for computation: " << tt.seconds() << " seconds." << std::endl;
	std::cout << "Total physical time for computation: " << GlobalStaticVariables::physical_time_ << " seconds." << std::endl;
	return 0;
}