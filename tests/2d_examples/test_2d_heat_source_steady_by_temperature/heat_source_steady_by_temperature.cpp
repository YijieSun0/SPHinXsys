/**
 * @file 	heat_source_steady.cpp
 * @brief 	This is the first test to demonstrate SPHInXsys as an optimization tool.
 * @details Consider a 2d block thermal domain with two constant temperature regions at the lower
 * 			and upper boundaries. The radiation-like source is distributed in the entire block domain.
 * 			The optimization target is to achieve lowest average temperature by modifying the distribution of
 * 			thermal diffusion rate in the domain with an extra conservation constraint that
 * 			the integral of the thermal diffusion rate in the entire domain is constant.
 * @author 	Bo Zhang and Xiangyu Hu
 */
#include "sphinxsys.h" // using SPHinXsys library
using namespace SPH;   // namespace cite here
//----------------------------------------------------------------------
//	Global geometry parameters and numerical setup.
//----------------------------------------------------------------------
Real L = 1.0;					 // inner domain length
Real H = 1.0;					 // inner domain height
Real resolution_ref = H / 100.0; // reference resolution for discretization
Real BW = resolution_ref * 2.0;	 // boundary width
BoundingBox system_domain_bounds(Vec2d(-BW, -BW), Vec2d(L + BW, H + BW));
//----------------------------------------------------------------------
//	Global parameters for physics state variables.
//----------------------------------------------------------------------
std::string variable_name = "Phi";
std::string variable_target_name = "Phi_target";
std::string residue_name = "ThermalEquationResidue";
Real lower_temperature = 300.0;
Real upper_temperature = 350.0;
Real reference_temperature = upper_temperature - lower_temperature;
Real heat_source = 100.0;
Real learning_rate = 0.006;
Real learning_strength_ref = 1.0;
//----------------------------------------------------------------------
//	Global parameters for material properties or coefficient variables.
//----------------------------------------------------------------------
std::string coefficient_name = "ThermalDiffusivity";
std::string reference_coefficient = "ReferenceThermalDiffusivity";
Real diffusion_coff = 1.0;
//----------------------------------------------------------------------
//	Geometric regions used in the system.
//----------------------------------------------------------------------
Vec2d block_halfsize = Vec2d(0.5 * L, 0.5 * H);					 // local center at origin
Vec2d block_translation = block_halfsize;						 // translation to global coordinates
Vec2d constraint_halfsize = Vec2d(0.05 * L, 0.5 * BW);			 // constraint block half size
Vec2d top_constraint_translation = Vec2d(0.5 * L, L + 0.5 * BW); // top constraint
Vec2d bottom_constraint_translation = Vec2d(0.5 * L, -0.5 * BW); // bottom constraint
class IsothermalBoundaries : public ComplexShape
{
public:
	explicit IsothermalBoundaries(const std::string& shape_name)
		: ComplexShape(shape_name)
	{
		add<TransformShape<GeometricShapeBox>>(Transform2d(top_constraint_translation), constraint_halfsize);
		add<TransformShape<GeometricShapeBox>>(Transform2d(bottom_constraint_translation), constraint_halfsize);
	}
};
//----------------------------------------------------------------------
//	Initial condition for temperature.
//----------------------------------------------------------------------
class DiffusionBodyInitialCondition : public ValueAssignment<Real>
{
public:
	explicit DiffusionBodyInitialCondition(SPHBody& diffusion_body)
		: ValueAssignment<Real>(diffusion_body, variable_name),
		pos_(particles_->pos_) {};
	void update(size_t index_i, Real dt)
	{
		variable_[index_i] = 375.0 + 25.0 * (((double)rand() / (RAND_MAX)) - 0.5) * 2.0;
	};

protected:
	StdLargeVec<Vecd>& pos_;
};
//----------------------------------------------------------------------
//	Constraints for isothermal boundaries.
//----------------------------------------------------------------------
class IsothermalBoundariesConstraints : public ValueAssignment<Real>
{
public:
	explicit IsothermalBoundariesConstraints(SolidBody& isothermal_boundaries)
		: ValueAssignment<Real>(isothermal_boundaries, variable_name),
		pos_(particles_->pos_) {};

	void update(size_t index_i, Real dt)
	{
		variable_[index_i] = pos_[index_i][1] > 0.5 ? lower_temperature : upper_temperature;
	}

protected:
	StdLargeVec<Vecd>& pos_;
};
//----------------------------------------------------------------------
//	Initial coefficient distribution.
//----------------------------------------------------------------------
class DiffusivityDistribution : public ValueAssignment<Real>
{
public:
	explicit DiffusivityDistribution(SPHBody& diffusion_body)
		: ValueAssignment<Real>(diffusion_body, coefficient_name),
		pos_(particles_->pos_) {};
	void update(size_t index_i, Real dt)
	{
		variable_[index_i] = diffusion_coff;
	};

protected:
	StdLargeVec<Vecd>& pos_;
};
//----------------------------------------------------------------------
//	Coefficient reference for imposing coefficient evolution.
//----------------------------------------------------------------------
class ReferenceThermalDiffusivity : public ValueAssignment<Real>
{
public:
	ReferenceThermalDiffusivity(SPHBody& diffusion_body, const std::string& coefficient_name_ref)
		: ValueAssignment<Real>(diffusion_body, coefficient_name),
		variable_ref(*particles_->template getVariableByName<Real>(coefficient_name_ref)) {};
	void update(size_t index_i, Real dt)
	{
		variable_ref[index_i] = variable_[index_i];
	};

protected:
	StdLargeVec<Real>& variable_ref;
};
//----------------------------------------------------------------------
//	Equation residue to measure the solution convergence properties.
//----------------------------------------------------------------------
class ThermalEquationResidue
	: public OperatorWithBoundary<LaplacianInner<Real, CoefficientByParticle<Real>>,
	                              LaplacianFromWall<Real, CoefficientByParticle<Real>>>

{
	Real source_;
	StdLargeVec<Real>& residue_;

public:
	ThermalEquationResidue(ComplexRelation& complex_relation,
		                   const std::string& in_name, const std::string& out_name,
		                   const std::string& eta_name, Real source)
		: OperatorWithBoundary<LaplacianInner<Real, CoefficientByParticle<Real>>,
		                       LaplacianFromWall<Real, CoefficientByParticle<Real>>>(
			complex_relation, in_name, out_name, eta_name),
		residue_(base_operator_.OutVariable()), source_(source) {};
	void interaction(size_t index_i, Real dt)
	{
		OperatorWithBoundary<
			LaplacianInner<Real, CoefficientByParticle<Real>>,
			LaplacianFromWall<Real, CoefficientByParticle<Real>>>::interaction(index_i, dt);
		residue_[index_i] += source_;
	};
};
//----------------------------------------------------------------------
//  Impose optimization target by directly decreasing the temperature.
//----------------------------------------------------------------------
class ImposeTargetFunction : public LocalDynamics, public GeneralDataDelegateSimple
{
public:
	ImposeTargetFunction(SPHBody& sph_body, const std::string& variable_name, const Real& learning_rate)
		: LocalDynamics(sph_body), GeneralDataDelegateSimple(sph_body),
		variable_(*particles_->getVariableByName<Real>(variable_name)),
		variable_target_(*particles_->getVariableByName<Real>(variable_target_name)),
		learning_rate_(learning_rate) {};
	virtual ~ImposeTargetFunction() {};
	void setSourceStrength(Real learning_rate) { learning_rate_ = learning_rate; };
	void update(size_t index_i, Real dt)
	{
		//variable_[index_i] -= learning_rate_ * reference_temperature;
		variable_target_[index_i] = variable_[index_i] - learning_rate_ * reference_temperature;
	};

protected:
	StdLargeVec<Real>& variable_;
	StdLargeVec<Real>& variable_target_;
	Real learning_rate_;
};
//----------------------------------------------------------------------
//	Evolution of the coefficient to achieve imposed target by temperature
//----------------------------------------------------------------------
class CoefficientEvolutionExplicitTem : public LocalDynamics, public DissipationDataInner
{
public:
	CoefficientEvolutionExplicitTem(BaseInnerRelation& inner_relation,
									const std::string& variable_name, const std::string& eta)
		: LocalDynamics(inner_relation.sph_body_), DissipationDataInner(inner_relation),
		  rho_(particles_->rho_), source_(0.0),
		  variable_(*particles_->getVariableByName<Real>(variable_name)),
		  variable_target_(*particles_->getVariableByName<Real>(variable_target_name)),
		  residue_(*particles_->getVariableByName<Real>(residue_name)),
		  eta_(*particles_->template getVariableByName<Real>(coefficient_name))
	{
		particles_->registerVariable(change_rate_, "DiffusionCoefficientChangeRate");
	};
	virtual ~CoefficientEvolutionExplicitTem() {};

	void interaction(size_t index_i, Real dt)
	{
		//Real variable_i = variable_[index_i];
		Real variable_i = variable_target_[index_i];
		Real eta_i = eta_[index_i];

		Real change_rate = heat_source - residue_[index_i];
		const Neighborhood& inner_neighborhood = inner_configuration_[index_i];
		for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
		{
			Real b_ij = 2.0 * inner_neighborhood.dW_ijV_j_[n] / inner_neighborhood.r_ij_[n];
			size_t index_j = inner_neighborhood.j_[n];

			Real variable_diff = (variable_i - variable_target_[index_j]);
			Real variable_diff_abs = SMAX(ABS(variable_diff), 5.0);
			Real coefficient_ave = 0.5 * (eta_i + eta_[index_j]);
			Real coefficient_diff = 0.5 * (eta_i - eta_[index_j]);

			change_rate += b_ij * (coefficient_ave * variable_diff + coefficient_diff * variable_diff_abs);
		}
		change_rate_[index_i] = change_rate / rho_[index_i];
	};

	void update(size_t index_i, Real dt)
	{
		Real increment = change_rate_[index_i] * dt;
		Real theta = increment < 0.0 ? SMIN((0.01 + Eps - eta_[index_i]) / increment, 1.0) : 1.0;
		eta_[index_i] += increment * theta;
	};

	void setSource(Real source) { source_ = source; };

protected:
	StdLargeVec<Real>& rho_;
	StdLargeVec<Real> change_rate_;
	StdLargeVec<Real>& variable_;
	StdLargeVec<Real>& variable_target_;
	StdLargeVec<Real>& residue_;
	StdLargeVec<Real>& eta_, eta_ref_; /**< variable damping coefficient */
	Real source_;
};
//----------------------------------------------------------------------
//	Evolution of the coefficient to achieve imposed target from the wall by temperature
//----------------------------------------------------------------------
class CoefficientEvolutionWithWallExplicitTem : public CoefficientEvolutionExplicitTem,
	public DissipationDataWithWall
{
public:
	CoefficientEvolutionWithWallExplicitTem(ComplexRelation& complex_relation,
		const std::string& variable_name, const std::string& eta)
		: CoefficientEvolutionExplicitTem(complex_relation.getInnerRelation(),
			variable_name, coefficient_name),
		DissipationDataWithWall(complex_relation.getContactRelation())
	{
		for (size_t k = 0; k != contact_particles_.size(); ++k)
		{
			wall_variable_.push_back(contact_particles_[k]->template getVariableByName<Real>(variable_name));
		}
	};
	virtual ~CoefficientEvolutionWithWallExplicitTem() {};

	void interaction(size_t index_i, Real dt)
	{
		CoefficientEvolutionExplicitTem::interaction(index_i, dt);

		//Real variable_i = variable_[index_i];
		Real variable_i = variable_target_[index_i];

		Real change_rate = 0.0;
		for (size_t k = 0; k < contact_configuration_.size(); ++k)
		{
			const StdLargeVec<Real>& variable_k = *(wall_variable_[k]);
			const Neighborhood& contact_neighborhood = (*contact_configuration_[k])[index_i];
			for (size_t n = 0; n != contact_neighborhood.current_size_; ++n)
			{
				Real b_ij = 2.0 * contact_neighborhood.dW_ijV_j_[n] / contact_neighborhood.r_ij_[n];
				size_t index_j = contact_neighborhood.j_[n];

				Real variable_diff = (variable_i - variable_k[index_j]);
				change_rate += b_ij * eta_[index_i] * variable_diff;
			}
		}
		change_rate_[index_i] += change_rate / rho_[index_i];
	};

protected:
	StdVec<StdLargeVec<Real>*> wall_variable_;
};
//----------------------------------------------------------------------
//  Evolution of the coefficient to achieve imposed target
//----------------------------------------------------------------------
class CoefficientEvolutionImplicit : public LocalDynamics, public DissipationDataInner
{
public:
	CoefficientEvolutionImplicit(BaseInnerRelation& inner_relation,
		const std::string& variable_name, const std::string& eta)
		: LocalDynamics(inner_relation.sph_body_), DissipationDataInner(inner_relation),
		rho_(particles_->rho_),
		variable_(*particles_->getVariableByName<Real>(variable_name)),
		residue_(*particles_->getVariableByName<Real>(residue_name)),
		eta_(*particles_->template getVariableByName<Real>(coefficient_name)) {};
	virtual ~CoefficientEvolutionImplicit() {};

	virtual ErrorAndParameters<Real> computeErrorAndParameters(size_t index_i, Real dt = 0.0)
	{
		ErrorAndParameters<Real> error_and_parameters;
		Neighborhood& inner_neighborhood = inner_configuration_[index_i];

		for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
		{
			Real b_ij = 2.0 * inner_neighborhood.dW_ijV_j_[n] * dt / inner_neighborhood.r_ij_[n];
			size_t index_j = inner_neighborhood.j_[n];

			Real variable_diff = (variable_[index_i] - variable_[index_j]);
			Real variable_diff_abs = ABS(variable_diff);
			Real coefficient_ave = 0.5 * (eta_[index_i] + eta_[index_j]);
			Real coefficient_diff = 0.5 * (eta_[index_i] - eta_[index_j]);

			error_and_parameters.error_ -= b_ij * (coefficient_ave * variable_diff + coefficient_diff * variable_diff_abs);
			error_and_parameters.a_ += b_ij * (0.5 * variable_diff + 0.5 * variable_diff_abs);
			error_and_parameters.c_ += (b_ij * 0.5 * variable_diff - 0.5 * variable_diff_abs) * (b_ij * 0.5 * variable_diff - -0.5 * variable_diff_abs);
		}
		//error_and_parameters.a_ -= 1;
		error_and_parameters.error_ -= heat_source * dt;
		error_and_parameters.error_ += residue_[index_i] * dt;
		return error_and_parameters;
	};

	virtual void updateStatesByError(size_t index_i, Real dt, const ErrorAndParameters<Real>& error_and_parameters)
	{
		Real parameter_l = error_and_parameters.a_ * error_and_parameters.a_ + error_and_parameters.c_;
		Real parameter_k = error_and_parameters.error_ / (parameter_l + TinyReal);

		eta_[index_i] += error_and_parameters.a_ * parameter_k;
		if (eta_[index_i] < 0.001) { eta_[index_i] = 0.001; }

		Neighborhood& inner_neighborhood = inner_configuration_[index_i];
		for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
		{
			Real b_ij = 2.0 * inner_neighborhood.dW_ijV_j_[n] * dt / inner_neighborhood.r_ij_[n];
			size_t index_j = inner_neighborhood.j_[n];
			Real variable_diff = (variable_[index_i] - variable_[index_j]);
			Real variable_diff_abs = ABS(variable_diff);

			eta_[index_j] += (b_ij * 0.5 * variable_diff) * parameter_k;
			if (eta_[index_j] < 0.001) { eta_[index_j] = 0.001; }
		}
	};

	virtual void interaction(size_t index_i, Real dt = 0.0)
	{
		ErrorAndParameters<Real> error_and_parameters = computeErrorAndParameters(index_i, dt);
		updateStatesByError(index_i, dt, error_and_parameters);
	};

protected:
	StdLargeVec<Real>& rho_;
	StdLargeVec<Real>& variable_;
	StdLargeVec<Real>& residue_;
	StdLargeVec<Real>& eta_; /**< variable damping coefficient */
};
//----------------------------------------------------------------------
//  Evolution of the coefficient to achieve imposed target from the wall
//----------------------------------------------------------------------
class CoefficientEvolutionWithWallImplicit : public CoefficientEvolutionImplicit,
	public DissipationDataWithWall
{
public:
	CoefficientEvolutionWithWallImplicit(ComplexRelation& complex_relation,
		const std::string& variable_name, const std::string& eta)
		: CoefficientEvolutionImplicit(complex_relation.getInnerRelation(), variable_name, coefficient_name),
		DissipationDataWithWall(complex_relation.getContactRelation())
	{
		for (size_t k = 0; k != contact_particles_.size(); ++k)
		{
			wall_variable_.push_back(contact_particles_[k]->template getVariableByName<Real>(variable_name));
		}
	}
	virtual ~CoefficientEvolutionWithWallImplicit() {};

	virtual ErrorAndParameters<Real> computeErrorAndParameters(size_t index_i, Real dt = 0.0)
	{
		ErrorAndParameters<Real> error_and_parameters = CoefficientEvolutionImplicit::
			computeErrorAndParameters(index_i, dt);

		for (size_t k = 0; k < contact_configuration_.size(); ++k)
		{
			const StdLargeVec<Real>& variable_k = *(wall_variable_[k]);
			const Neighborhood& contact_neighborhood = (*contact_configuration_[k])[index_i];
			for (size_t n = 0; n != contact_neighborhood.current_size_; ++n)
			{
				Real b_ij = 2.0 * contact_neighborhood.dW_ijV_j_[n] / contact_neighborhood.r_ij_[n];
				size_t index_j = contact_neighborhood.j_[n];
				Real variable_diff = (variable_[index_i] - variable_k[index_j]);

				error_and_parameters.error_ -= b_ij * eta_[index_i] * variable_diff;
				error_and_parameters.a_ += b_ij * variable_diff;
			}
		}
		return error_and_parameters;
	};

protected:
	StdVec<StdLargeVec<Real>*> wall_variable_;
};
//----------------------------------------------------------------------
//	Main program starts here.
//----------------------------------------------------------------------
int main()
{
	//----------------------------------------------------------------------
	//	Build up the environment of a SPHSystem.
	//----------------------------------------------------------------------
	SPHSystem sph_system(system_domain_bounds, resolution_ref);
	IOEnvironment io_environment(sph_system);
	//----------------------------------------------------------------------
	//	Creating body, materials and particles.
	//----------------------------------------------------------------------
	SolidBody diffusion_body(
		sph_system, makeShared<TransformShape<GeometricShapeBox>>(
			Transform2d(block_translation), block_halfsize, "DiffusionBody"));
	diffusion_body.defineParticlesAndMaterial<SolidParticles, Solid>();
	diffusion_body.generateParticles<ParticleGeneratorLattice>();
	//----------------------------------------------------------------------
	//	add extra discrete variables (not defined in the library)
	//----------------------------------------------------------------------
	StdLargeVec<Real> body_temperature;
	diffusion_body.addBodyState<Real>(body_temperature, variable_name);
	diffusion_body.addBodyStateForRecording<Real>(variable_name);
	diffusion_body.addBodyStateToRestart<Real>(variable_name);
	StdLargeVec<Real> body_target_temperature;
	diffusion_body.addBodyState<Real>(body_target_temperature, variable_target_name);
	diffusion_body.addBodyStateForRecording<Real>(variable_target_name);
	StdLargeVec<Real> diffusion_coefficient;
	diffusion_body.addBodyState<Real>(diffusion_coefficient, coefficient_name);
	diffusion_body.addBodyStateForRecording<Real>(coefficient_name);
	diffusion_body.addBodyStateToRestart<Real>(coefficient_name);
	StdLargeVec<Real> laplacian_residue;
	diffusion_body.addBodyState<Real>(laplacian_residue, residue_name);
	diffusion_body.addBodyStateForRecording<Real>(residue_name);

	SolidBody isothermal_boundaries(sph_system, makeShared<IsothermalBoundaries>("IsothermalBoundaries"));
	isothermal_boundaries.defineParticlesAndMaterial<SolidParticles, Solid>();
	isothermal_boundaries.generateParticles<ParticleGeneratorLattice>();
	//----------------------------------------------------------------------
	//	add extra discrete variables (not defined in the library)
	//----------------------------------------------------------------------
	StdLargeVec<Real> constrained_temperature;
	isothermal_boundaries.addBodyState<Real>(constrained_temperature, variable_name);
	isothermal_boundaries.addBodyStateForRecording<Real>(variable_name);
	//----------------------------------------------------------------------
	//	Define body relation map.
	//	The contact map gives the topological connections between the bodies.
	//	Basically the range of bodies to build neighbor particle lists.
	//----------------------------------------------------------------------
	ComplexRelation diffusion_body_complex(diffusion_body, { &isothermal_boundaries });
	//----------------------------------------------------------------------
	//	Define the main numerical methods used in the simulation.
	//	Note that there may be data dependence on the constructors of these methods.
	//----------------------------------------------------------------------
	SimpleDynamics<DiffusionBodyInitialCondition> diffusion_initial_condition(diffusion_body);
	SimpleDynamics<IsothermalBoundariesConstraints> boundary_constraint(isothermal_boundaries);
	SimpleDynamics<DiffusivityDistribution> coefficient_distribution(diffusion_body);
	SimpleDynamics<ConstraintTotalScalarAmount> constrain_total_coefficient(diffusion_body, coefficient_name);
	SimpleDynamics<ImposingSourceTerm<Real>> thermal_source(diffusion_body, variable_name, heat_source);
	SimpleDynamics<ImposeTargetFunction> target_function(diffusion_body, variable_name, learning_rate);
	InteractionDynamics<ThermalEquationResidue>
		thermal_equation_residue(diffusion_body_complex, variable_name, residue_name, coefficient_name, heat_source);
	ReduceDynamics<MaximumNorm<Real>> maximum_equation_residue(diffusion_body, residue_name);
	ReduceDynamics<QuantityMoment<Real>> total_coefficient(diffusion_body, coefficient_name);
	ReduceAverage<QuantitySummation<Real>> average_temperature(diffusion_body, variable_name);
	ReduceAverage<AverageNorm<Real>> average_equation_residue(diffusion_body, residue_name);
	//----------------------------------------------------------------------
	//	Define the methods for I/O operations and observations of the simulation.
	//----------------------------------------------------------------------
	BodyStatesRecordingToVtp write_states(io_environment, sph_system.real_bodies_);
	RestartIO restart_io(io_environment, sph_system.real_bodies_);
	//----------------------------------------------------------------------
	//	Thermal diffusivity optimization
	//----------------------------------------------------------------------
	InteractionSplit<DampingSplittingWithWallCoefficientByParticle<Real>>
		implicit_heat_transfer_solver(diffusion_body_complex, variable_name, coefficient_name);
	InteractionWithUpdate<CoefficientEvolutionWithWallExplicitTem>
		coefficient_evolution_with_wall_tem(diffusion_body_complex, variable_name, coefficient_name);
	//----------------------------------------------------------------------
	//	Prepare the simulation with cell linked list, configuration
	//	and case specified initial condition if necessary.
	//----------------------------------------------------------------------
	sph_system.initializeSystemCellLinkedLists();
	sph_system.initializeSystemConfigurations();
	diffusion_initial_condition.parallel_exec();
	boundary_constraint.parallel_exec();
	coefficient_distribution.parallel_exec();
	constrain_total_coefficient.setupInitialScalarAmount();
	thermal_equation_residue.parallel_exec();
	//----------------------------------------------------------------------
	//	Setup for time-stepping control
	//----------------------------------------------------------------------
	int ite = 0;
	int ite_learn = 0;
	Real End_Time = 5.0;
	Real Relaxation_Time = 1.0;
	Real Observe_time = 0.01 * End_Time; 
	Real dt = 1.0e-4;
	Real dt_coeff = SMIN(dt, 0.25 * resolution_ref * resolution_ref / reference_temperature);
	int target_steps = 10; // default number of iteration for imposing target
	bool imposing_target = true;
	Real allowed_equation_residue = 30e4;
	//----------------------------------------------------------------------
	//	First output before the main loop.
	//----------------------------------------------------------------------
	write_states.writeToFile(ite);
	//----------------------------------------------------------------------
	//	Main loop starts here.
	//----------------------------------------------------------------------
	std::string filefullpath_all_information = io_environment.output_folder_ + "/" + "all_information.dat";
	std::ofstream out_file_all_information(filefullpath_all_information.c_str(), std::ios::app);

	Real equation_residue_max = Infinity; // initial value
	Real equation_residue_ave = Infinity; // initial value

	while (GlobalStaticVariables::physical_time_ < End_Time)
	{
		Real relaxation_time = 0.0;
		while (relaxation_time < Observe_time)
		{
			if (imposing_target)
			{
				// target imposing step
				ite_learn++;
				thermal_equation_residue.parallel_exec();
				target_function.parallel_exec();
				for (size_t k = 0; k != target_steps; ++k)
				{
					coefficient_evolution_with_wall_tem.parallel_exec(dt_coeff);
					constrain_total_coefficient.parallel_exec();
				}
			}

			// equation solving step
			thermal_source.parallel_exec(dt);
			implicit_heat_transfer_solver.parallel_exec(dt);
			relaxation_time += dt;
			GlobalStaticVariables::physical_time_ += dt;

			// residue evaluation step
			thermal_equation_residue.parallel_exec();
			Real residue_max_after_target = maximum_equation_residue.parallel_exec();
			Real residue_ave_after_target = average_equation_residue.parallel_exec();
			if (residue_max_after_target > equation_residue_max && residue_max_after_target > allowed_equation_residue)
			{
				imposing_target = false; 
				equation_residue_ave = residue_ave_after_target;
			}
			else
			{
				imposing_target = true;
				equation_residue_max = residue_max_after_target;
				equation_residue_ave = residue_ave_after_target;
			};

			ite++;
			if ((ite % 100 == 0))
			{
				std::cout << "N= " << ite << " Time: " << GlobalStaticVariables::physical_time_ << "	dt: " << dt << "\n";
				std::cout << "Total diffusivity is " << total_coefficient.parallel_exec() << "\n";
				std::cout << "Average temperature is " << average_temperature.parallel_exec() << "\n";
				std::cout << "Thermal equation maximum residue is " << equation_residue_max << "\n";
				std::cout << "Thermal equation average residue is " << equation_residue_ave << "\n";
				std::cout << "The learning times are " << ite_learn << "\n";
				/*out_file_all_information << std::fixed << std::setprecision(12) << ite << "   " << ite << "   " <<
					average_temperature.parallel_exec() << "   " <<
					maximum_equation_residue.parallel_exec() << "   " <<
					equation_residue_max << "   " <<
					equation_residue_ave << "   " <<
					imposing_target << "\n";*/
			}
		}

		write_states.writeToFile();
	}

	while (GlobalStaticVariables::physical_time_ < Relaxation_Time + End_Time)
	{
		Real relaxation_time = 0;
		while (relaxation_time < Observe_time)
		{
			// equation solving step
			thermal_source.parallel_exec(dt);
			implicit_heat_transfer_solver.parallel_exec(dt);
			relaxation_time += dt;
			GlobalStaticVariables::physical_time_ += dt;

			ite++;
			if (ite % 100 == 0)
			{
				thermal_equation_residue.parallel_exec();
				Real residue_max_after_target = maximum_equation_residue.parallel_exec();
				Real residue_ave_after_target = average_equation_residue.parallel_exec();
				std::cout << "N= " << ite << " Time: " << GlobalStaticVariables::physical_time_ << "	dt: " << dt << "\n";
				std::cout << "Total diffusivity is " << total_coefficient.parallel_exec() << "\n";
				std::cout << "Average temperature is " << average_temperature.parallel_exec() << "\n";
				std::cout << "Thermal equation maximum residue is " << equation_residue_max << "\n";
				std::cout << "Thermal equation average residue is " << equation_residue_ave << "\n";
				std::cout << "The learning times are " << ite_learn << "\n";
				/*out_file_all_information << std::fixed << std::setprecision(12) << ite << "   " << ite << "   " <<
					average_temperature.parallel_exec() << "   " <<
					maximum_equation_residue.parallel_exec() << "   " <<
					equation_residue_max << "   " <<
					equation_residue_ave << "   " <<
					imposing_target << "\n";*/
			}
		}
		write_states.writeToFile();
	}

	std::cout << "The final physical time has finished." << std::endl;
	return 0;
}
