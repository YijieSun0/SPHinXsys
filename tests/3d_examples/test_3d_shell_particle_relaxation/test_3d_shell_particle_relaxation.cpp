/**
 * @file 	test_3d_shell_particle_relaxation.cpp
 * @brief 	This is the test of using levelset to generate shell particles with single resolution and relax particles.
 * @details	We use this case to test the particle generation and relaxation by levelset for a complex thin structures geometry (3D).
 * @author 	Dong Wu and Xiangyu Hu
 */

#include "sphinxsys.h"
using namespace SPH;
//----------------------------------------------------------------------
//	Set the file path to the data file.
//----------------------------------------------------------------------
std::string full_path_to_geometry = "./input/curved_tube.stl";
//----------------------------------------------------------------------
//	Basic geometry parameters and numerical setup.
//----------------------------------------------------------------------
Vec3d domain_lower_bound(12, 14, 446);
Vec3d domain_upper_bound(1315, 1317, 1302);
Real dp_0 = 25.0;
Real thickness = 50.0;
// level set resolution much higher than that of particles is required
Real level_set_refinement_ratio = dp_0 / (0.1 * thickness);
//----------------------------------------------------------------------
//	Domain bounds of the system.
//----------------------------------------------------------------------
BoundingBox system_domain_bounds(domain_lower_bound, domain_upper_bound);
//----------------------------------------------------------------------
//	Define the body shape.
//----------------------------------------------------------------------
class ImportedShellModel: public ComplexShape
{
public:
	explicit ImportedShellModel(const std::string &shape_name) : ComplexShape(shape_name)
	{
		add<TriangleMeshShapeSTL>(full_path_to_geometry, Vecd(0), 1.0);
	}
};
//--------------------------------------------------------------------------
//	Main program starts here.
//--------------------------------------------------------------------------
int main(int ac, char *av[])
{
	//----------------------------------------------------------------------
	//	Build up a SPHSystem.
	//----------------------------------------------------------------------
	SPHSystem system(system_domain_bounds, dp_0);
	IOEnvironment io_environment(system);
	//----------------------------------------------------------------------
	//	Creating body, materials and particles.
	//----------------------------------------------------------------------
	RealBody imported_model(system, makeShared<ImportedShellModel>("ImportedShellModel"));
	imported_model.defineBodyLevelSetShape(level_set_refinement_ratio)->correctLevelSetSign()->writeLevelSet(io_environment);
	//here dummy linear elastic solid is use because no solid dynamics in particle relaxation
	imported_model.defineParticlesAndMaterial<ShellParticles, SaintVenantKirchhoffSolid>(1.0, 1.0, 0.0);
	imported_model.generateParticles<ThickSurfaceParticleGeneratorLattice>(thickness);
	imported_model.addBodyStateForRecording<Vecd>("NormalDirection");
	//----------------------------------------------------------------------
	//	Define simple file input and outputs functions.
	//----------------------------------------------------------------------
	BodyStatesRecordingToVtp write_imported_model_to_vtp(io_environment, {imported_model});
	MeshRecordingToPlt write_mesh_cell_linked_list(io_environment, imported_model.cell_linked_list_);
	//----------------------------------------------------------------------
	//	Define body relation map.
	//	The contact map gives the topological connections between the bodies.
	//	Basically the the range of bodies to build neighbor particle lists.
	//----------------------------------------------------------------------
	InnerRelation imported_model_inner(imported_model);
	//----------------------------------------------------------------------
	//	Methods used for particle relaxation.
	//----------------------------------------------------------------------
	SimpleDynamics<RandomizeParticlePosition> random_imported_model_particles(imported_model);
	/** A  Physics relaxation step. */
	relax_dynamics::ShellRelaxationStepInner relaxation_step_inner(imported_model_inner, thickness, level_set_refinement_ratio);
	relax_dynamics::ShellNormalDirectionPrediction shell_normal_prediction(imported_model_inner, thickness);	
	//----------------------------------------------------------------------
	//	Particle relaxation starts here.
	//----------------------------------------------------------------------
	random_imported_model_particles.parallel_exec(0.25);
	relaxation_step_inner.mid_surface_bounding_.parallel_exec();
	write_imported_model_to_vtp.writeToFile(0.0);
	imported_model.updateCellLinkedList();
	write_mesh_cell_linked_list.writeToFile(0.0);
	//----------------------------------------------------------------------
	//	Particle relaxation time stepping start here.
	//----------------------------------------------------------------------
	int ite_p = 0;
	while (ite_p < 1000)
	{
		if (ite_p % 100 == 0)
		{
			std::cout << std::fixed << std::setprecision(9) << "Relaxation steps for the inserted body N = " << ite_p << "\n";
			write_imported_model_to_vtp.writeToFile(ite_p);
		}
		relaxation_step_inner.parallel_exec();
		ite_p += 1;
	}
	shell_normal_prediction.exec();
	write_imported_model_to_vtp.writeToFile(ite_p);
	std::cout << "The physics relaxation process of imported model finish !" << std::endl;

	return 0;
}
