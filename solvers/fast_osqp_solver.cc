#include "fast_osqp_solver.h"

#include <vector>

#include "dairlib/lcmt_qp.hpp"
#include <lcm/lcm-cpp.hpp>
#include <osqp.h>

#include "drake/common/text_logging.h"
#include "drake/math/eigen_sparse_triplet.h"
#include "drake/solvers/mathematical_program.h"
#include "drake/solvers/osqp_solver.h"


using drake::math::SparseMatrixToTriplets;
using drake::solvers::Binding;
using drake::solvers::Constraint;
using drake::solvers::MathematicalProgram;
using drake::solvers::MathematicalProgramResult;
using drake::solvers::OsqpSolver;
using drake::solvers::OsqpSolverDetails;
using drake::solvers::SolutionResult;
using drake::solvers::SolverOptions;
using drake::solvers::VectorXDecisionVariable;
using drake::solvers::internal::BindingDynamicCast;

namespace dairlib {
namespace solvers {
namespace {
void ParseQuadraticCosts(const MathematicalProgram& prog,
                         Eigen::SparseMatrix<c_float>* P,
                         std::vector<c_float>* q, double* constant_cost_term) {
  DRAKE_ASSERT(static_cast<int>(q->size()) == prog.num_vars());

  // Loop through each quadratic costs in prog, and compute the Hessian matrix
  // P, the linear cost q, and the constant cost term.
  std::vector<Eigen::Triplet<c_float>> P_triplets;
  for (const auto& quadratic_cost : prog.quadratic_costs()) {
    const VectorXDecisionVariable& x = quadratic_cost.variables();
    // x_indices are the indices of the variables x (the variables bound with
    // this quadratic cost) in the program decision variables.
    const std::vector<int> x_indices = prog.FindDecisionVariableIndices(x);

    // Add quadratic_cost.Q to the Hessian P.
    const std::vector<Eigen::Triplet<double>> Qi_triplets =
        SparseMatrixToTriplets(quadratic_cost.evaluator()->Q());
    P_triplets.reserve(P_triplets.size() + Qi_triplets.size());
    for (int i = 0; i < static_cast<int>(Qi_triplets.size()); ++i) {
      // Unpack the field of the triplet (for clarity below).
      const int row = x_indices[Qi_triplets[i].row()];
      const int col = x_indices[Qi_triplets[i].col()];
      const double value = Qi_triplets[i].value();
      // Since OSQP 0.6.0 the P matrix is required to be upper triangular, so
      // we only add upper triangular entries to P_triplets.
      if (row <= col) {
        P_triplets.emplace_back(row, col, static_cast<c_float>(value));
      }
    }

    // Add quadratic_cost.b to the linear cost term q.
    for (int i = 0; i < x.rows(); ++i) {
      q->at(x_indices[i]) += quadratic_cost.evaluator()->b()(i);
    }

    // Add quadratic_cost.c to constant term
    *constant_cost_term += quadratic_cost.evaluator()->c();
  }
  P->resize(prog.num_vars(), prog.num_vars());
  P->setFromTriplets(P_triplets.begin(), P_triplets.end());
}

void ParseLinearCosts(const MathematicalProgram& prog, std::vector<c_float>* q,
                      double* constant_cost_term) {
  // Add the linear costs to the osqp cost.
  DRAKE_ASSERT(static_cast<int>(q->size()) == prog.num_vars());

  // Loop over the linear costs stored inside prog.
  for (const auto& linear_cost : prog.linear_costs()) {
    for (int i = 0; i < static_cast<int>(linear_cost.GetNumElements()); ++i) {
      // Append the linear cost term to q.
      if (linear_cost.evaluator()->a()(i) != 0) {
        const int x_index =
            prog.FindDecisionVariableIndex(linear_cost.variables()(i));
        q->at(x_index) += linear_cost.evaluator()->a()(i);
      }
    }
    // Add the constant cost term to constant_cost_term.
    *constant_cost_term += linear_cost.evaluator()->b();
  }
}

// OSQP defines its own infinity in osqp/include/glob_opts.h.
c_float ConvertInfinity(double val) {
  if (std::isinf(val)) {
    if (val > 0) {
      return OSQP_INFTY;
    }
    return -OSQP_INFTY;
  }
  return static_cast<c_float>(val);
}

// Will call this function to parse both LinearConstraint and
// LinearEqualityConstraint.
template <typename C>
void ParseLinearConstraints(
    const MathematicalProgram& prog,
    const std::vector<Binding<C>>& linear_constraints,
    std::vector<Eigen::Triplet<c_float>>* A_triplets, std::vector<c_float>* l,
    std::vector<c_float>* u, int* num_A_rows,
    std::unordered_map<Binding<Constraint>, int>* constraint_start_row) {
  // Loop over the linear constraints, stack them to get l, u and A.
  for (const auto& constraint : linear_constraints) {
    const std::vector<int> x_indices =
        prog.FindDecisionVariableIndices(constraint.variables());
    const std::vector<Eigen::Triplet<double>> Ai_triplets =
        SparseMatrixToTriplets(constraint.evaluator()->A());
    const Binding<Constraint> constraint_cast =
        BindingDynamicCast<Constraint>(constraint);
    constraint_start_row->emplace(constraint_cast, *num_A_rows);
    // Append constraint.A to osqp A.
    for (const auto& Ai_triplet : Ai_triplets) {
      A_triplets->emplace_back(*num_A_rows + Ai_triplet.row(),
                               x_indices[Ai_triplet.col()],
                               static_cast<c_float>(Ai_triplet.value()));
    }
    const int num_Ai_rows = constraint.evaluator()->num_constraints();
    l->reserve(l->size() + num_Ai_rows);
    u->reserve(u->size() + num_Ai_rows);
    for (int i = 0; i < num_Ai_rows; ++i) {
      l->push_back(ConvertInfinity(constraint.evaluator()->lower_bound()(i)));
      u->push_back(ConvertInfinity(constraint.evaluator()->upper_bound()(i)));
    }
    *num_A_rows += num_Ai_rows;
  }
}

void ParseBoundingBoxConstraints(
    const MathematicalProgram& prog,
    std::vector<Eigen::Triplet<c_float>>* A_triplets, std::vector<c_float>* l,
    std::vector<c_float>* u, int* num_A_rows,
    std::unordered_map<Binding<Constraint>, int>* constraint_start_row) {
  // Loop over the linear constraints, stack them to get l, u and A.
  for (const auto& constraint : prog.bounding_box_constraints()) {
    const Binding<Constraint> constraint_cast =
        BindingDynamicCast<Constraint>(constraint);
    constraint_start_row->emplace(constraint_cast, *num_A_rows);
    // Append constraint.A to osqp A.
    for (int i = 0; i < static_cast<int>(constraint.GetNumElements()); ++i) {
      A_triplets->emplace_back(
          *num_A_rows + i,
          prog.FindDecisionVariableIndex(constraint.variables()(i)),
          static_cast<c_float>(1));
    }
    const int num_Ai_rows = constraint.evaluator()->num_constraints();
    l->reserve(l->size() + num_Ai_rows);
    u->reserve(u->size() + num_Ai_rows);
    for (int i = 0; i < num_Ai_rows; ++i) {
      l->push_back(ConvertInfinity(constraint.evaluator()->lower_bound()(i)));
      u->push_back(ConvertInfinity(constraint.evaluator()->upper_bound()(i)));
    }
    *num_A_rows += num_Ai_rows;
  }
}

void ParseAllLinearConstraints(
    const MathematicalProgram& prog, Eigen::SparseMatrix<c_float>* A,
    std::vector<c_float>* l, std::vector<c_float>* u,
    std::unordered_map<Binding<Constraint>, int>* constraint_start_row) {
  std::vector<Eigen::Triplet<c_float>> A_triplets;
  l->clear();
  u->clear();
  int num_A_rows = 0;
  ParseLinearConstraints(prog, prog.linear_constraints(), &A_triplets, l, u,
                         &num_A_rows, constraint_start_row);
  ParseLinearConstraints(prog, prog.linear_equality_constraints(), &A_triplets,
                         l, u, &num_A_rows, constraint_start_row);
  ParseBoundingBoxConstraints(prog, &A_triplets, l, u, &num_A_rows,
                              constraint_start_row);
  A->resize(num_A_rows, prog.num_vars());
  A->setFromTriplets(A_triplets.begin(), A_triplets.end());
}

// Convert an Eigen::SparseMatrix to csc_matrix, to be used by osqp.
// Make sure the input Eigen sparse matrix is compressed, by calling
// makeCompressed() function.
// The caller of this function is responsible for freeing the memory allocated
// here.
csc* EigenSparseToCSC(const Eigen::SparseMatrix<c_float>& mat) {
  // A csc matrix is in the compressed column major.
  c_float* values =
      static_cast<c_float*>(c_malloc(sizeof(c_float) * mat.nonZeros()));
  c_int* inner_indices =
      static_cast<c_int*>(c_malloc(sizeof(c_int) * mat.nonZeros()));
  c_int* outer_indices =
      static_cast<c_int*>(c_malloc(sizeof(c_int) * (mat.cols() + 1)));
  for (int i = 0; i < mat.nonZeros(); ++i) {
    values[i] = *(mat.valuePtr() + i);
    inner_indices[i] = static_cast<c_int>(*(mat.innerIndexPtr() + i));
  }
  for (int i = 0; i < mat.cols() + 1; ++i) {
    outer_indices[i] = static_cast<c_int>(*(mat.outerIndexPtr() + i));
  }
  return csc_matrix(mat.rows(), mat.cols(), mat.nonZeros(), values,
                    inner_indices, outer_indices);
}

template <typename T1, typename T2>
void SetFastOsqpSolverSetting(const std::unordered_map<std::string, T1>& options,
                          const std::string& option_name,
                          T2* osqp_setting_field) {
  const auto it = options.find(option_name);
  if (it != options.end()) {
    *osqp_setting_field = it->second;
  }
}

template <typename T1, typename T2>
void SetFastOsqpSolverSettingWithDefaultValue(
    const std::unordered_map<std::string, T1>& options,
    const std::string& option_name, T2* osqp_setting_field,
    const T1& default_field_value) {
  const auto it = options.find(option_name);
  if (it != options.end()) {
    *osqp_setting_field = it->second;
  } else {
    *osqp_setting_field = default_field_value;
  }
}

void SetFastOsqpSolverSettings(const SolverOptions& solver_options,
                               OSQPSettings* settings) {
  const std::unordered_map<std::string, double>& options_double =
      solver_options.GetOptionsDouble(OsqpSolver::id());
  const std::unordered_map<std::string, int>& options_int =
      solver_options.GetOptionsInt(OsqpSolver::id());
  SetFastOsqpSolverSetting(options_double, "rho", &(settings->rho));
  SetFastOsqpSolverSetting(options_double, "sigma", &(settings->sigma));
  SetFastOsqpSolverSetting(options_int, "max_iter", &(settings->max_iter));
  SetFastOsqpSolverSetting(options_double, "eps_abs", &(settings->eps_abs));
  SetFastOsqpSolverSetting(options_double, "eps_rel", &(settings->eps_rel));
  SetFastOsqpSolverSetting(options_double, "eps_prim_inf",
                       &(settings->eps_prim_inf));
  SetFastOsqpSolverSetting(options_double, "eps_dual_inf",
                       &(settings->eps_dual_inf));
  SetFastOsqpSolverSetting(options_double, "alpha", &(settings->alpha));
  SetFastOsqpSolverSetting(options_double, "delta", &(settings->delta));
  // Default polish to true, to get an accurate solution.
  SetFastOsqpSolverSettingWithDefaultValue(options_int, "polish",
                                       &(settings->polish), 1);
  SetFastOsqpSolverSetting(options_int, "polish_refine_iter",
                       &(settings->polish_refine_iter));
  SetFastOsqpSolverSettingWithDefaultValue(options_int, "verbose",
                                       &(settings->verbose), 0);
  SetFastOsqpSolverSetting(options_int, "scaled_termination",
                       &(settings->scaled_termination));
  SetFastOsqpSolverSetting(options_int, "check_termination",
                       &(settings->check_termination));
  SetFastOsqpSolverSetting(options_int, "warm_start", &(settings->warm_start));
  SetFastOsqpSolverSetting(options_int, "scaling", &(settings->scaling));
  SetFastOsqpSolverSetting(options_int, "adaptive_rho", &(settings->adaptive_rho));
  SetFastOsqpSolverSetting(options_double, "adaptive_rho_interval",
                       &(settings->adaptive_rho_interval));
  SetFastOsqpSolverSetting(options_double, "adaptive_rho_tolerance",
                       &(settings->adaptive_rho_tolerance));
  SetFastOsqpSolverSetting(options_double, "adaptive_rho_fraction",
                       &(settings->adaptive_rho_fraction));
  SetFastOsqpSolverSetting(options_double, "time_limit", &(settings->time_limit));
}

template <typename C>
void SetDualSolution(
    const std::vector<Binding<C>>& constraints,
    const Eigen::VectorXd& all_dual_solution,
    const std::unordered_map<Binding<Constraint>, int>& constraint_start_row,
    MathematicalProgramResult* result) {
  for (const auto& constraint : constraints) {
    // OSQP uses the dual variable `y` as the negation of the shadow price, so
    // we need to negate `all_dual_solution` as Drake interprets dual solution
    // as the shadow price.
    const Binding<Constraint> constraint_cast =
        BindingDynamicCast<Constraint>(constraint);
    result->set_dual_solution(
        constraint,
        -all_dual_solution.segment(constraint_start_row.at(constraint_cast),
                                   constraint.evaluator()->num_constraints()));
  }
}
}  // namespace

bool FastOsqpSolver::is_available() { return true; }

void FastOsqpSolver::InitializeSolver(const MathematicalProgram& prog,
                                      const SolverOptions& solver_options) {
  // Get the cost for the QP.
  Eigen::SparseMatrix<c_float> P_sparse;
  std::vector<c_float> q(prog.num_vars(), 0);
  double constant_cost_term{0};

  ParseQuadraticCosts(prog, &P_sparse, &q, &constant_cost_term);
  ParseLinearCosts(prog, &q, &constant_cost_term);

  // linear_constraint_start_row[binding] stores the starting row index in A
  // corresponding to the linear constraint `binding`.
  std::unordered_map<Binding<Constraint>, int> constraint_start_row;

  // Parse the linear constraints.
  Eigen::SparseMatrix<c_float> A_sparse;
  std::vector<c_float> l, u;
  ParseAllLinearConstraints(prog, &A_sparse, &l, &u, &constraint_start_row);

  // Now pass the constraint and cost to osqp data.
  osqp_data_ = nullptr;

  // Populate data.
  osqp_data_ = static_cast<OSQPData*>(c_malloc(sizeof(OSQPData)));

  osqp_data_->n = prog.num_vars();
  osqp_data_->m = A_sparse.rows();
  osqp_data_->P = EigenSparseToCSC(P_sparse);
  osqp_data_->q = q.data();
  osqp_data_->A = EigenSparseToCSC(A_sparse);
  osqp_data_->l = l.data();
  osqp_data_->u = u.data();

  // Define Solver settings as default.
  // Problem settings
  osqp_settings_ = static_cast<OSQPSettings*>(c_malloc(sizeof(OSQPSettings)));
  osqp_set_default_settings(osqp_settings_);
  SetFastOsqpSolverSettings(solver_options, osqp_settings_);

  // Setup workspace.
  workspace_ = nullptr;
  const c_int osqp_setup_err =
      osqp_setup(&workspace_, osqp_data_, osqp_settings_);
  if (osqp_setup_err != 0) {
    std::cerr << "Error setting up osqp solver.";
  }
}

void FastOsqpSolver::DoSolve(const MathematicalProgram& prog,
                             const Eigen::VectorXd& initial_guess,
                             const SolverOptions& merged_options,
                             MathematicalProgramResult* result) const {
  if (!prog.GetVariableScaling().empty()) {
    static const drake::logging::Warn log_once(
        "OsqpSolver doesn't support the feature of variable scaling.");
  }

  OsqpSolverDetails& solver_details =
      result->SetSolverDetailsType<OsqpSolverDetails>();

  // OSQP solves a convex quadratic programming problem
  // min 0.5 xᵀPx + qᵀx
  // s.t l ≤ Ax ≤ u
  // OSQP is written in C, so this function will be in C style.

  // Get the cost for the QP.
  Eigen::SparseMatrix<c_float> P_sparse;
  std::vector<c_float> q(prog.num_vars(), 0);
  double constant_cost_term{0};
  //
  ParseQuadraticCosts(prog, &P_sparse, &q, &constant_cost_term);
  ParseLinearCosts(prog, &q, &constant_cost_term);

  // linear_constraint_start_row[binding] stores the starting row index in A
  // corresponding to the linear constraint `binding`.
  std::unordered_map<Binding<Constraint>, int> constraint_start_row;

  // Parse the linear constraints.
  Eigen::SparseMatrix<c_float> A_sparse;
  std::vector<c_float> l, u;
  ParseAllLinearConstraints(prog, &A_sparse, &l, &u, &constraint_start_row);

  csc* P_csc = EigenSparseToCSC(P_sparse);
  csc* A_csc = EigenSparseToCSC(A_sparse);
  osqp_update_lin_cost(workspace_, q.data());
  osqp_update_bounds(workspace_, l.data(), u.data());
  osqp_update_P_A(workspace_, P_csc->x, OSQP_NULL, P_csc->nzmax, A_csc->x,
                  OSQP_NULL, A_csc->nzmax);
  SetFastOsqpSolverSettings(merged_options, osqp_settings_);
  // If any step fails, it will set the solution_result and skip other steps.
  std::optional<SolutionResult> solution_result;

  lcmt_qp msg;
  msg.n_x = prog.num_vars();

  Eigen::MatrixXd Q(P_sparse);

  // Note: Amessage is transposed, becaues Eigen defaults to column major
  for (int i = 0; i < prog.num_vars(); i++) {
    msg.Q.push_back(std::vector<double>(Q.col(i).data(),
                                        Q.col(i).data() + prog.num_vars()));
  }
  msg.w = q;


  Eigen::MatrixXd A(A_sparse);

  // std::cout << A.row(68) << std::endl;
  msg.n_ineq = A.rows();
  for (int i = 0; i < A.rows(); i++) {
    std::vector<double> row(A.cols());
    for (int j = 0; j < A.cols(); j++) {
      row[j] = A(i,j);
    }
    msg.A_ineq.push_back(row);
  }

  msg.ineq_lb = l;
  msg.ineq_ub = u;
  msg.x_lb = std::vector<double>(prog.num_vars(), -std::numeric_limits<double>::infinity());
  msg.x_ub = std::vector<double>(prog.num_vars(), std::numeric_limits<double>::infinity());

  msg.n_eq = 0;
  lcm::LCM lcm;
  lcm.publish("QP_LOG", &msg);

  // Solve problem.
  if (!solution_result) {
    DRAKE_THROW_UNLESS(workspace_ != nullptr);
    const c_int osqp_solve_err = osqp_solve(workspace_);
    if (osqp_solve_err != 0) {
      solution_result = SolutionResult::kInvalidInput;
    }
  }

  // Extract results.
  if (!solution_result) {
    DRAKE_THROW_UNLESS(workspace_->info != nullptr);

    solver_details.iter = workspace_->info->iter;
    solver_details.status_val = workspace_->info->status_val;
    solver_details.primal_res = workspace_->info->pri_res;
    solver_details.dual_res = workspace_->info->dua_res;
    solver_details.setup_time = workspace_->info->setup_time;
    solver_details.solve_time = workspace_->info->solve_time;
    solver_details.polish_time = workspace_->info->polish_time;
    solver_details.run_time = workspace_->info->run_time;

    switch (workspace_->info->status_val) {
      case OSQP_SOLVED:
      case OSQP_SOLVED_INACCURATE: {
        const Eigen::Map<Eigen::Matrix<c_float, Eigen::Dynamic, 1>> osqp_sol(
            workspace_->solution->x, prog.num_vars());
        result->set_x_val(osqp_sol.cast<double>());
        result->set_optimal_cost(workspace_->info->obj_val +
                                 constant_cost_term);
        solver_details.y = Eigen::Map<Eigen::VectorXd>(workspace_->solution->y,
                                                       workspace_->data->m);
        solution_result = SolutionResult::kSolutionFound;
//        SetDualSolution(prog.linear_constraints(), solver_details.y,
//                        constraint_start_row, result);
//        SetDualSolution(prog.linear_equality_constraints(), solver_details.y,
//                        constraint_start_row, result);
//        SetDualSolution(prog.bounding_box_constraints(), solver_details.y,
//                        constraint_start_row, result);

        break;
      }
      case OSQP_PRIMAL_INFEASIBLE:
      case OSQP_PRIMAL_INFEASIBLE_INACCURATE: {
        solution_result = SolutionResult::kInfeasibleConstraints;
        result->set_optimal_cost(MathematicalProgram::kGlobalInfeasibleCost);
        break;
      }
      case OSQP_DUAL_INFEASIBLE:
      case OSQP_DUAL_INFEASIBLE_INACCURATE: {
        solution_result = SolutionResult::kDualInfeasible;
        break;
      }
      case OSQP_MAX_ITER_REACHED: {
        solution_result = SolutionResult::kIterationLimit;
        break;
      }
      default: {
        solution_result = SolutionResult::kUnknownError;
        break;
      }
    }
  }
  result->set_solution_result(solution_result.value());

//  osqp_cleanup(workspace_);
  c_free(P_csc->x);
  c_free(P_csc->i);
  c_free(P_csc->p);
  c_free(P_csc);
  c_free(A_csc->x);
  c_free(A_csc->i);
  c_free(A_csc->p);
  c_free(A_csc);
}

}  // namespace solvers
}  // namespace dairlib
