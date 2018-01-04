#include "UIState.hpp"

#include "Mesh2D.hpp"
#include "Mesh3D.hpp"

#include "LinearElasticity.hpp"


#include <igl/per_face_normals.h>
#include <igl/triangle/triangulate.h>
#include <igl/copyleft/tetgen/tetrahedralize.h>
#include <igl/Timer.h>


#include <nanogui/formhelper.h>
#include <nanogui/screen.h>


// ... or using a custom callback
  //       viewer_.ngui->addVariable<bool>("bool",[&](bool val) {
  //     boolVariable = val; // set
  // },[&]() {
  //     return boolVariable; // get
  // });


using namespace Eigen;


namespace poly_fem
{

	long UIState::clip_elements(const Eigen::MatrixXd &pts, const Eigen::MatrixXi &tris, const std::vector<int> &ranges, std::vector<bool> &valid_elements)
	{
		viewer.data.clear();

		valid_elements.resize(normalized_barycenter.rows());

		if(!is_slicing)
		{
			std::fill(valid_elements.begin(), valid_elements.end(), true);
			viewer.data.set_mesh(pts, tris);
			viewer.data.set_face_based(false);
			if(state.mesh->is_volume())
			{
				MatrixXd normals;
				igl::per_face_normals(pts, tris, normals);
				viewer.data.set_normals(normals);
			}

			return tris.rows();
		}

		for (long i = 0; i<normalized_barycenter.rows();++i)
			valid_elements[i] = normalized_barycenter(i, slice_coord) < slice_position;

		viewer.data.set_face_based(false);

		int n_vis_valid_tri = 0;

		for (long i = 0; i<normalized_barycenter.rows();++i)
		{
			if(valid_elements[i])
				n_vis_valid_tri += ranges[i+1] - ranges[i];
		}

		MatrixXi valid_tri(n_vis_valid_tri, tris.cols());

		int from = 0;
		for(std::size_t i = 1; i < ranges.size(); ++i)
		{
			if(!valid_elements[i-1]) continue;

			const int range = ranges[i]-ranges[i-1];

			valid_tri.block(from, 0, range, tri_faces.cols()) = tris.block(ranges[i-1], 0, range, tris.cols());

			from += range;
		}



		viewer.data.set_mesh(pts, valid_tri);
		viewer.data.set_face_based(false);
		if(state.mesh->is_volume())
		{
			MatrixXd normals;
			igl::per_face_normals(pts, valid_tri, normals);
			viewer.data.set_normals(normals);
		}

		return valid_tri.rows();
	}

	bool UIState::is_quad(const ElementBases &bs) const
	{
		return (state.mesh->is_volume() && (int(bs.bases.size()) == 8 || int(bs.bases.size()) == 27)) || (!state.mesh->is_volume() && (int(bs.bases.size()) == 4 || int(bs.bases.size()) == 9));
	}

	bool UIState::is_tri(const ElementBases &bs) const
	{
		return !state.mesh->is_volume() && (int(bs.bases.size()) == 3 || int(bs.bases.size()) == 6);
	}

	void UIState::interpolate_function(const MatrixXd &fun, MatrixXd &result)
	{
		MatrixXd tmp;

		int actual_dim = 1;
		if(state.problem.problem_num() == 3)
			actual_dim = state.mesh->is_volume() ? 3:2;

		result.resize(vis_pts.rows(), actual_dim);

		int index = 0;

		for(int i = 0; i < int(state.bases.size()); ++i)
		{
			const ElementBases &bs = state.bases[i];
			MatrixXd local_pts;

			if(is_quad(bs))
				local_pts = local_vis_pts_quad;
			else if(is_tri(bs))
				local_pts = local_vis_pts_tri;
			else{
				local_pts = vis_pts_poly[i];
			}

			MatrixXd local_res = MatrixXd::Zero(local_pts.rows(), actual_dim);

			for(std::size_t j = 0; j < bs.bases.size(); ++j)
			{
				const Basis &b = bs.bases[j];

				b.basis(local_pts, tmp);
				for(int d = 0; d < actual_dim; ++d)
				{
					for(std::size_t ii = 0; ii < b.global().size(); ++ii)
						local_res.col(d) += b.global()[ii].val * tmp * fun(b.global()[ii].index*actual_dim + d);
				}
			}

			result.block(index, 0, local_res.rows(), actual_dim) = local_res;
			index += local_res.rows();
		}
	}


	UIState::UIState()
	: state(State::state())
	{ }

	void UIState::plot_function(const MatrixXd &fun, double min, double max)
	{
		MatrixXd col;
		std::vector<bool> valid_elements;

		if(state.problem.problem_num() == 3)
		{
			const MatrixXd ffun = (fun.array()*fun.array()).rowwise().sum().sqrt(); //norm of displacement, maybe replace with stress
			// const MatrixXd ffun = fun.col(1); //y component

			// LinearElasticity lin_elast;
			// MatrixXd ffun(vis_pts.rows(), 1);

			// int size = 1;
			// if(state.problem.problem_num() == 3)
			// 	size = state.mesh->is_volume() ? 3:2;

			// MatrixXd stresses;
			// int counter = 0;
			// for(int i = 0; i < int(state.bases.size()); ++i)
			// {
			// 	const ElementBases &bs = state.bases[i];

			// 	MatrixXd local_pts;

			// 	if(is_quad(bs))
			// 		local_pts = local_vis_pts_quad;
			// 	else if(is_tri(bs))
			// 		local_pts = local_vis_pts_tri;
			// 	else{
			// 		local_pts = vis_pts_poly[i];
			// 	}
			// 	lin_elast.compute_von_mises_stresses(size, bs, local_pts, fun, stresses);
			// 	ffun.block(counter, 0, stresses.rows(), stresses.cols()) = stresses;
			// 	counter += stresses.rows();
			// }

			if(min < max)
				igl::colormap(color_map, ffun, min, max, col);
			else
				igl::colormap(color_map, ffun, true, col);

			MatrixXd tmp = vis_pts;

			for(long i = 0; i < fun.cols(); ++i) //apply displacement
				tmp.col(i) += fun.col(i);

			clip_elements(tmp, vis_faces, vis_element_ranges, valid_elements);
		}
		else
		{

			if(min < max)
				igl::colormap(color_map, fun, min, max, col);
			else
				igl::colormap(color_map, fun, true, col);

			if(state.mesh->is_volume())
				clip_elements(vis_pts, vis_faces, vis_element_ranges, valid_elements);
			else
			{
				MatrixXd tmp;
				tmp.resize(fun.rows(),3);
				tmp.col(0)=vis_pts.col(0);
				tmp.col(1)=vis_pts.col(1);
				tmp.col(2)=fun;
				clip_elements(tmp, vis_faces, vis_element_ranges, valid_elements);
			}
		}

		viewer.data.set_colors(col);
	}


	UIState &UIState::ui_state(){
		static UIState instance;

		return instance;
	}

	void UIState::init(const std::string &mesh_path, const int n_refs, const int problem_num)
	{
		state.init(mesh_path, n_refs, problem_num);

		auto clear_func = [&](){ viewer.data.clear(); };

		auto show_mesh_func = [&](){
			clear_func();
			current_visualization = Visualizing::InputMesh;

			std::vector<bool> valid_elements;
			const long n_tris = clip_elements(tri_pts, tri_faces, element_ranges, valid_elements);

			std::vector<ElementType> ele_tag;
			state.mesh->compute_element_tag(ele_tag);

			Eigen::MatrixXd cols(n_tris, 3);
			cols.setZero();

			int from = 0;
			for(std::size_t i = 1; i < element_ranges.size(); ++i)
			{
				if(!valid_elements[i-1]) continue;

				const ElementType type = ele_tag[i-1];
				const int range = element_ranges[i]-element_ranges[i-1];

				switch(type)
				{
						//green
					case ElementType::RegularInteriorCube:
					cols.block(from, 1, range, 1).setOnes(); break;

						//dark green
					case ElementType::RegularBoundaryCube:
					cols.block(from, 1, range, 1).setConstant(0.5); break;

						//orange
					case ElementType::SimpleSingularInteriorCube:
					cols.block(from, 0, range, 1).setOnes();
					cols.block(from, 1, range, 1).setConstant(0.5); break;

 						//red
					case ElementType::MultiSingularInteriorCube:
					cols.block(from, 0, range, 1).setOnes(); break;

						//blue
					case ElementType::SingularBoundaryCube:
					cols.block(from, 2, range, 1).setConstant(0.6); break;

				  		 //light blue
					case ElementType::BoundaryPolytope:
					case ElementType::InteriorPolytope:
					cols.block(from, 2, range, 1).setOnes();
					cols.block(from, 1, range, 1).setConstant(0.5); break;

					//grey
					case ElementType::Undefined:
					cols.block(from, 0, range, 3).setConstant(0.5); break;
				}

				from += range;
			}

			viewer.data.set_colors(cols);

			MatrixXd p0, p1;
			state.mesh->get_edges(p0, p1);
			viewer.data.add_edges(p0, p1, MatrixXd::Zero(1, 3));

			// for(int i = 0; i < static_cast<Mesh3D *>(state.mesh)->n_faces(); ++i)
			// {
			// 	MatrixXd p = static_cast<Mesh3D *>(state.mesh)->node_from_face(i);
			// 	viewer.data.add_label(p.transpose(), std::to_string(i));
			// }

			// for(int i = 0; i < state.mesh->n_elements(); ++i)
			// {
			// 	MatrixXd p = static_cast<Mesh3D *>(state.mesh)->node_from_element(i);
			// 	viewer.data.add_label(p.transpose(), std::to_string(i));
			// }

			// for(int i = 0; i < static_cast<Mesh3D *>(state.mesh)->n_pts(); ++i)
			// {
			// 	MatrixXd p; static_cast<Mesh3D *>(state.mesh)->point(i, p);
			// 	viewer.data.add_label(p.transpose(), std::to_string(i));
			// }

			// for(int i = 0; i < state.mesh->n_elements(); ++i)
			// {
			// 	MatrixXd p = static_cast<Mesh2D *>(state.mesh)->node_from_face(i);
			// 	viewer.data.add_label(p.transpose(), std::to_string(i));
			// }
		};

		auto show_vis_mesh_func = [&](){
			clear_func();
			current_visualization = Visualizing::VisMesh;

			std::vector<bool> valid_elements;
			clip_elements(vis_pts, vis_faces, vis_element_ranges, valid_elements);
		};

		auto show_nodes_func = [&](){
			// for(std::size_t i = 0; i < bounday_nodes.size(); ++i)
			// 	std::cout<<bounday_nodes[i]<<std::endl;

			for(std::size_t i = 0; i < state.bases.size(); ++i)
			// for(std::size_t i = 0; i < 1; ++i)
			{
				const ElementBases &basis = state.bases[i];
				// if(!basis.has_parameterization) continue;


				for(std::size_t j = 0; j < basis.bases.size(); ++j)
				{
					for(std::size_t kk = 0; kk < basis.bases[j].global().size(); ++kk)
					{
						const Local2Global &l2g = basis.bases[j].global()[kk];
						int g_index = l2g.index;

						if(state.problem.problem_num() == 3)
							g_index *= 2;

						MatrixXd node = l2g.node;
					// node += MatrixXd::Random(node.rows(), node.cols())/100;
						MatrixXd txt_p = node;
						// for(long k = 0; k < txt_p.size(); ++k)
							// txt_p(k) += 0.02;

						MatrixXd col = MatrixXd::Zero(l2g.node.rows(), 3);
						if(std::find(state.bounday_nodes.begin(), state.bounday_nodes.end(), g_index) != state.bounday_nodes.end())
							col.col(0).setOnes();
						else
							col.col(1).setOnes();


						viewer.data.add_points(node, col);
						viewer.data.add_label(txt_p.transpose(), std::to_string(g_index));
					}
				}
			}
		};

		auto show_quadrature_func = [&](){
			for(std::size_t i = 0; i < state.values.size(); ++i)
			// for(std::size_t i = 0; i < 1; ++i)
			{
				const ElementAssemblyValues &vals = state.values[i];
				if(state.mesh->is_volume())
					viewer.data.add_points(vals.val, vals.quadrature.points);
				else
					viewer.data.add_points(vals.val, MatrixXd::Zero(vals.val.rows(), 3));

				// for(long j = 0; j < vals.val.rows(); ++j)
					// viewer.data.add_label(vals.val.row(j), std::to_string(j));
			}
		};

		auto show_rhs_func = [&](){
			current_visualization = Visualizing::Rhs;
			MatrixXd global_rhs;
			state.interpolate_function(state.rhs, local_vis_pts_quad, global_rhs);

			plot_function(global_rhs, 0, 1);
		};


		auto show_sol_func = [&](){
			current_visualization = Visualizing::Solution;
			MatrixXd global_sol;
			interpolate_function(state.sol, global_sol);
			plot_function(global_sol);
		};


		auto show_error_func = [&]()
		{
			current_visualization = Visualizing::Error;
			MatrixXd global_sol;
			interpolate_function(state.sol, global_sol);

			MatrixXd exact_sol;
			state.problem.exact(vis_pts, exact_sol);

			const MatrixXd err = (global_sol - exact_sol).array().abs();
			plot_function(err);
		};


		auto show_basis_func = [&](){
			if(vis_basis < 0 || vis_basis >= state.n_bases) return;

			current_visualization = Visualizing::VisBasis;

			MatrixXd fun = MatrixXd::Zero(state.n_bases, 1);
			fun(vis_basis) = 1;

			MatrixXd global_fun;
			interpolate_function(fun, global_fun);
			// global_fun /= 100;
			std::cout<<global_fun.minCoeff()<<" "<<global_fun.maxCoeff()<<std::endl;
			plot_function(global_fun);
		};


		auto build_vis_mesh_func = [&](){
			igl::Timer timer; timer.start();
			std::cout<<"Building vis mesh..."<<std::flush;

			const double area_param = 0.00001*state.mesh->n_elements();

			std::stringstream buf;
			buf.precision(100);
			buf.setf(std::ios::fixed, std::ios::floatfield);

			if(state.mesh->is_volume())
			{
				buf<<"Qpq1.414a"<<area_param;
				MatrixXd pts(8,3); pts <<
				0, 0, 0,
				0, 1, 0,
				1, 1, 0,
				1, 0, 0,

				0, 0, 1, //4
				0, 1, 1,
				1, 1, 1,
				1, 0, 1;

				Eigen::MatrixXi faces(12,3); faces <<
				1, 2, 0,
				0, 2, 3,

				5, 4, 6,
				4, 7, 6,

				1, 0, 4,
				1, 4, 5,

				2, 1, 5,
				2, 5, 6,

				3, 2, 6,
				3, 6, 7,

				0, 3, 7,
				0, 7, 4;

				clear_func();

				MatrixXi tets;
				igl::copyleft::tetgen::tetrahedralize(pts, faces, buf.str(), local_vis_pts_quad, tets, local_vis_faces_quad);
			}
			else
			{
				buf<<"Qqa"<<area_param;
				{
					MatrixXd pts(4,2); pts <<
					0,0,
					0,1,
					1,1,
					1,0;

					MatrixXi E(4,2); E <<
					0,1,
					1,2,
					2,3,
					3,0;

					MatrixXd H(0,2);
					igl::triangle::triangulate(pts, E, H, buf.str(), local_vis_pts_quad, local_vis_faces_quad);
				}
				{
					MatrixXd pts(3,2); pts <<
					0,0,
					1,0,
					0,1;

					MatrixXi E(3,2); E <<
					0,1,
					1,2,
					2,0;

					igl::triangle::triangulate(pts, E, MatrixXd(0,2), buf.str(), local_vis_pts_tri, local_vis_faces_tri);
				}
			}

			const auto &current_bases = state.iso_parametric ? state.bases : state.geom_bases;
			int faces_total_size = 0, points_total_size = 0;
			vis_element_ranges.push_back(0);

			for(int i = 0; i < int(current_bases.size()); ++i)
			{
				const ElementBases &bs = current_bases[i];

				if(is_quad(bs)){
					faces_total_size   += local_vis_faces_quad.rows();
					points_total_size += local_vis_pts_quad.rows();
				}
				else if(is_tri(bs))
				{
					faces_total_size   += local_vis_faces_tri.rows();
					points_total_size += local_vis_pts_tri.rows();
				}
				else
				{
					if(state.mesh->is_volume())
					{
						assert(false);
					}
					else
					{
						MatrixXd poly = state.polys[i];
						MatrixXi E(poly.rows(),2);
						for(int e = 0; e < int(poly.rows()); ++e)
						{
							E(e, 0) = e;
							E(e, 1) = (e+1) % poly.rows();
						}

						igl::triangle::triangulate(poly, E, MatrixXd(0,2), "Qpqa0.00001", vis_pts_poly[i], vis_faces_poly[i]);

						faces_total_size   += vis_faces_poly[i].rows();
						points_total_size += vis_pts_poly[i].rows();
					}
				}

				vis_element_ranges.push_back(faces_total_size);
			}

			vis_pts.resize(points_total_size, local_vis_pts_quad.cols());
			vis_faces.resize(faces_total_size, 3);

			MatrixXd mapped, tmp;
			int face_index = 0, point_index = 0;
			for(int i = 0; i < int(current_bases.size()); ++i)
			{
				const ElementBases &bs = current_bases[i];
				if(is_quad(bs))
				{
					bs.eval_geom_mapping(local_vis_pts_quad, mapped);
					vis_faces.block(face_index, 0, local_vis_faces_quad.rows(), 3) = local_vis_faces_quad.array() + point_index;
					face_index += local_vis_faces_quad.rows();

					vis_pts.block(point_index, 0, mapped.rows(), mapped.cols()) = mapped;
					point_index += mapped.rows();
				}
				else if(is_tri(bs))
				{
					bs.eval_geom_mapping(local_vis_pts_tri, mapped);
					vis_faces.block(face_index, 0, local_vis_faces_tri.rows(), 3) = local_vis_faces_tri.array() + point_index;

					face_index += local_vis_faces_tri.rows();

					vis_pts.block(point_index, 0, mapped.rows(), mapped.cols()) = mapped;
					point_index += mapped.rows();
				}
				else{
					vis_faces.block(face_index, 0, vis_faces_poly[i].rows(), 3) = vis_faces_poly[i].array() + point_index;

					face_index += vis_faces_poly[i].rows();

					vis_pts.block(point_index, 0, vis_pts_poly[i].rows(), vis_pts_poly[i].cols()) = vis_pts_poly[i];
					point_index += vis_pts_poly[i].rows();
				}
			}

			assert(point_index == vis_pts.rows());
			assert(face_index == vis_faces.rows());

			if(state.mesh->is_volume())
			{
				//reverse all faces
				for(long i = 0; i < vis_faces.rows(); ++i)
				{
					const int v0 = vis_faces(i, 0);
					const int v1 = vis_faces(i, 1);
					const int v2 = vis_faces(i, 2);

					int tmpc = vis_faces(i, 2);
					vis_faces(i, 2) = vis_faces(i, 1);
					vis_faces(i, 1) = tmpc;
				}
			}
			else
			{
				Matrix2d mmat;
				for(long i = 0; i < vis_faces.rows(); ++i)
				{
					const int v0 = vis_faces(i, 0);
					const int v1 = vis_faces(i, 1);
					const int v2 = vis_faces(i, 2);

					mmat.row(0) = vis_pts.row(v2) - vis_pts.row(v0);
					mmat.row(1) = vis_pts.row(v1) - vis_pts.row(v0);

					if(mmat.determinant() > 0)
					{
						int tmpc = vis_faces(i, 2);
						vis_faces(i, 2) = vis_faces(i, 1);
						vis_faces(i, 1) = tmpc;
					}
				}
			}

			timer.stop();
			std::cout<<" took "<<timer.getElapsedTime()<<"s"<<std::endl;

			if(skip_visualization) return;

			clear_func();
			show_vis_mesh_func();
		};


		auto load_mesh_func = [&](){
			element_ranges.clear();
			vis_element_ranges.clear();

			vis_faces_poly.clear();
			vis_pts_poly.clear();

			state.load_mesh();
			state.compute_mesh_stats();
			state.mesh->triangulate_faces(tri_faces, tri_pts, element_ranges);
			state.mesh->compute_barycenter(normalized_barycenter);

			// std::cout<<"normalized_barycenter\n"<<normalized_barycenter<<"\n\n"<<std::endl;
			for(long i = 0; i < normalized_barycenter.cols(); ++i){
				normalized_barycenter.col(i) = MatrixXd(normalized_barycenter.col(i).array() - normalized_barycenter.col(i).minCoeff());
				normalized_barycenter.col(i) /= normalized_barycenter.col(i).maxCoeff();
			}

			// std::cout<<"normalized_barycenter\n"<<normalized_barycenter<<"\n\n"<<std::endl;

			if(skip_visualization) return;

			clear_func();
			show_mesh_func();
		};

		auto build_basis_func = [&](){
			state.build_basis();

			if(skip_visualization) return;
			// clear_func();
			show_mesh_func();
			show_nodes_func();
		};


		auto compute_assembly_vals_func = [&]() {
			state.compute_assembly_vals();

			if(skip_visualization) return;
			clear_func();
			show_mesh_func();
			show_quadrature_func();
		};

		auto assemble_stiffness_mat_func = [&]() {
			state.assemble_stiffness_mat();
		};


		auto assemble_rhs_func = [&]() {
			state.assemble_rhs();

			// std::cout<<state.rhs<<std::endl;

			if(skip_visualization) return;
			// clear_func();
			// show_rhs_func();
		};

		auto solve_problem_func = [&]() {
			state.solve_problem();

			if(skip_visualization) return;
			clear_func();
			show_sol_func();
		};

		auto compute_errors_func = [&]() {
			state.compute_errors();

			if(skip_visualization) return;
			clear_func();
			show_error_func();
		};


		auto update_slices = [&]() {
			clear_func();
			switch(current_visualization)
			{
				case Visualizing::InputMesh: show_mesh_func(); break;
				case Visualizing::VisMesh: show_vis_mesh_func(); break;
				case Visualizing::Solution: show_sol_func(); break;
				case Visualizing::Rhs: break;
				case Visualizing::Error: show_error_func(); break;
				case Visualizing::VisBasis: show_basis_func(); break;
			}
		};


		viewer.callback_init = [&](igl::viewer::Viewer& viewer_)
		{
			viewer_.ngui->addWindow(Eigen::Vector2i(220,10),"PolyFEM");

			viewer_.ngui->addGroup("Settings");

			viewer_.ngui->addVariable("quad order", state.quadrature_order);
			viewer_.ngui->addVariable("discr order", state.discr_order);
			viewer_.ngui->addVariable("b samples", state.n_boundary_samples);

			viewer_.ngui->addVariable("lambda", state.lambda);
			viewer_.ngui->addVariable("mu", state.mu);

			viewer_.ngui->addVariable("mesh path", state.mesh_path);
			viewer_.ngui->addButton("browse...", [&]() {
				std::string path = nanogui::file_dialog({
					{ "HYBRID", "General polyhedral mesh" }, { "OBJ", "Obj 2D mesh" }
				}, false);

				if (!path.empty())
					state.mesh_path = path;

			});
			viewer_.ngui->addVariable("n refs", state.n_refs);
			viewer_.ngui->addVariable("refinenemt t", state.refinenemt_location);

			viewer_.ngui->addVariable("spline basis", state.use_splines);


			viewer_.ngui->addVariable<igl::ColorMapType>("Colormap", color_map)->setItems({"inferno", "jet", "magma", "parula", "plasma", "viridis"});

			viewer_.ngui->addVariable<ProblemType>("Problem",
				[&](ProblemType val) { state.problem.set_problem_num(val); },
				[&]() { return ProblemType(state.problem.problem_num()); }
				)->setItems({"Linear","Quadratic","Franke", "Elastic", "Zero BC"});

			viewer_.ngui->addVariable("skip visualization", skip_visualization);

			viewer_.ngui->addGroup("Runners");
			viewer_.ngui->addButton("Load mesh", load_mesh_func);
			viewer_.ngui->addButton("Build  basis", build_basis_func);
			viewer_.ngui->addButton("Compute vals", compute_assembly_vals_func);
			viewer_.ngui->addButton("Build vis mesh", build_vis_mesh_func);

			viewer_.ngui->addButton("Assemble stiffness", assemble_stiffness_mat_func);
			viewer_.ngui->addButton("Assemble rhs", assemble_rhs_func);
			viewer_.ngui->addButton("Solve", solve_problem_func);
			viewer_.ngui->addButton("Compute errors", compute_errors_func);

			viewer_.ngui->addButton("Run all", [&](){
				load_mesh_func();
				build_basis_func();

				if(!skip_visualization)
					build_vis_mesh_func();

				compute_assembly_vals_func();
				assemble_stiffness_mat_func();
				assemble_rhs_func();
				solve_problem_func();
				compute_errors_func();
			});

			viewer_.ngui->addWindow(Eigen::Vector2i(400,10),"Debug");
			viewer_.ngui->addButton("Clear", clear_func);
			viewer_.ngui->addButton("Show mesh", show_mesh_func);
			viewer_.ngui->addButton("Show vis mesh", show_vis_mesh_func);
			viewer_.ngui->addButton("Show nodes", show_nodes_func);
			viewer_.ngui->addButton("Show quadrature", show_quadrature_func);
			viewer_.ngui->addButton("Show rhs", show_rhs_func);
			viewer_.ngui->addButton("Show sol", show_sol_func);
			viewer_.ngui->addButton("Show error", show_error_func);

			viewer_.ngui->addVariable("basis num",vis_basis);
			viewer_.ngui->addButton("Show basis", show_basis_func);

			viewer_.ngui->addGroup("Slicing");
			viewer_.ngui->addVariable<int>("coord",[&](int val) {
				slice_coord = val;
				if(is_slicing)
					update_slices();
			},[&]() {
				return slice_coord;
			});
			viewer_.ngui->addVariable<double>("pos",[&](double val) {
				slice_position = val;
				if(is_slicing)
					update_slices();
			},[&]() {
				return slice_position;
			});

			viewer_.ngui->addButton("+0.1", [&](){ slice_position += 0.1; if(is_slicing) update_slices();});
			viewer_.ngui->addButton("-0.1", [&](){ slice_position -= 0.1; if(is_slicing) update_slices();});

			viewer_.ngui->addVariable<bool>("enable",[&](bool val) {
				is_slicing = val;
				update_slices();
			},[&]() {
				return is_slicing;
			});

			// viewer_.ngui->addGroup("Stats");
			// viewer_.ngui->addVariable("NNZ", Type &value)


			viewer_.screen->performLayout();

			return false;
		};

		viewer.launch();
	}

	void UIState::sertialize(const std::string &name)
	{

	}

}
