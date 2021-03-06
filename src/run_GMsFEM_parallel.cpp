#include "elastic_wave.hpp"
#include "parameters.hpp"
#include "receivers.hpp"
#include "utilities.hpp"

#include <float.h>

#ifdef MFEM_USE_MPI

using namespace std;
using namespace mfem;



static void par_time_step(HypreParMatrix &M, HypreParMatrix &S,
                          const Vector &b, double timeval, double dt,
                          Vector &U_0, Vector &U_1, Vector &U_2, ostream &log)
{
  HypreSmoother M_prec;
  CGSolver M_solver(M.GetComm());

  M_prec.SetType(HypreSmoother::Jacobi);
  M_solver.SetPreconditioner(M_prec);
  M_solver.SetOperator(M);

  M_solver.iterative_mode = false;
  M_solver.SetRelTol(1e-9);
  M_solver.SetAbsTol(0.0);
  M_solver.SetMaxIter(100);
  M_solver.SetPrintLevel(0);

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, U_0.Norml2(), M.GetComm()); log << "||U_0|| = " << norm << endl; }
  { double norm = GlobalLpNorm(2, U_1.Norml2(), M.GetComm()); log << "||U_1|| = " << norm << endl; }
  { double norm = GlobalLpNorm(2, U_2.Norml2(), M.GetComm()); log << "||U_2|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector y = U_1; y *= 2.0; y -= U_2;        // y = 2*u_1 - u_2

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, y.Norml2(), M.GetComm()); log << "||y|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector z0 = U_0;                           // z0 = M * (2*u_1 - u_2)
  M.Mult(y, z0);

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, z0.Norml2(), M.GetComm()); log << "||z0|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector z1 = U_0;                           // z1 = S * u_1
  S.Mult(U_1, z1);

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, z1.Norml2(), M.GetComm()); log << "||z1|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector z2 = b; z2 *= timeval; // z2 = timeval*source

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, z2.Norml2(), M.GetComm()); log << "||z2|| = " << norm << endl; }
#endif // MFEM_DEBUG

  // y = dt^2 * (S*u_1 - timeval*source), where it can be
  // y = dt^2 * (S*u_1 - ricker*pointforce) OR
  // y = dt^2 * (S*u_1 - gaussfirstderivative*momenttensor)
  y = z1; y -= z2; y *= dt*dt;

  // RHS = M*(2*u_1-u_2) - dt^2*(S*u_1-timeval*source)
  Vector RHS(z0); RHS = z0; RHS -= y;

  MPI_Barrier(MPI_COMM_WORLD);
  M_solver.Mult(RHS, U_0);

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, U_0.Norml2(), M.GetComm()); log << "||U_0|| = " << norm << endl; }
#endif // MFEM_DEBUG

  U_2 = U_1;
  U_1 = U_0;
}



static void par_time_step(HypreParMatrix &A, HypreParMatrix &M, HypreParMatrix &S,
                          const Vector &b, double timeval, double dt,
                          Vector &U_0, Vector &U_1, Vector &U_2, ostream &log)
{
#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, U_0.Norml2(), M.GetComm()); log << "||U_0|| = " << norm << endl; }
  { double norm = GlobalLpNorm(2, U_1.Norml2(), M.GetComm()); log << "||U_1|| = " << norm << endl; }
  { double norm = GlobalLpNorm(2, U_2.Norml2(), M.GetComm()); log << "||U_2|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector y = U_1; y *= 2.0; y -= U_2;        // y = 2*u_1 - u_2

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, y.Norml2(), M.GetComm()); log << "||y|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector z0 = U_0;                           // z0 = M * (2*u_1 - u_2)
  M.Mult(y, z0);

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, z0.Norml2(), M.GetComm()); log << "||z0|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector z1 = U_0;                           // z1 = S * u_1
  S.Mult(U_1, z1);

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, z1.Norml2(), M.GetComm()); log << "||z1|| = " << norm << endl; }
#endif // MFEM_DEBUG

  Vector z2 = b; z2 *= timeval; // z2 = timeval*source

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, z2.Norml2(), M.GetComm()); log << "||z2|| = " << norm << endl; }
#endif // MFEM_DEBUG

  // y = dt^2 * (S*u_1 - timeval*source), where it can be
  // y = dt^2 * (S*u_1 - ricker*pointforce) OR
  // y = dt^2 * (S*u_1 - gaussfirstderivative*momenttensor)
  y = z1; y -= z2; y *= dt*dt;

  // RHS = M*(2*u_1-u_2) - dt^2*(S*u_1-timeval*source)
  Vector RHS(z0); RHS = z0; RHS -= y;

  A.Mult(RHS, U_0);

#ifdef MFEM_DEBUG
  { double norm = GlobalLpNorm(2, U_0.Norml2(), M.GetComm()); log << "||U_0|| = " << norm << endl; }
#endif // MFEM_DEBUG

  U_2 = U_1;
  U_1 = U_0;
}



static void print_par_matrix_matlab(HypreParMatrix &A, const string &filename)
{
  int myid;
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  // This call works because HypreParMatrix implicitly converts to hypre_ParCSRMatrix*
  hypre_CSRMatrix* A_serial = hypre_ParCSRMatrixToCSRMatrixAll(A);

  // This "views" the hypre_CSRMatrix as an mfem::SparseMatrix
  mfem::SparseMatrix A_sparse(
        hypre_CSRMatrixI(A_serial), hypre_CSRMatrixJ(A_serial), hypre_CSRMatrixData(A_serial),
        hypre_CSRMatrixNumRows(A_serial), hypre_CSRMatrixNumCols(A_serial),
        false, false, true);

  // Write to file from root process
  if (myid == 0)
  {
    ofstream out(filename.c_str());
    MFEM_VERIFY(out, "Cannot open file " << filename);
    A_sparse.PrintMatlab(out);
  }

  // Cleanup, since the hypre call creates a new matrix on each process
  hypre_CSRMatrixDestroy(A_serial);
}



void ElasticWave::run_GMsFEM_parallel() const
{
  MFEM_VERIFY(param.mesh, "The serial mesh is not initialized");
  MFEM_VERIFY(param.par_mesh, "The parallel mesh is not initialized");

  int myid, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  string filelog = string(param.output.directory) + "/outputlog." + d2s(myid);
  ofstream log(filelog.c_str());
  MFEM_VERIFY(log, "Cannot open file " << filelog);

  StopWatch chrono;
  chrono.Start();

  const int dim = param.dimension;

  log << "FE space generation..." << flush;
  DG_FECollection fec(param.method.order, dim);
  ParFiniteElementSpace fespace(param.par_mesh, &fec, dim);
  log << "done. Time = " << chrono.RealTime() << " sec" << endl;

  const HYPRE_Int n_dofs = fespace.GlobalTrueVSize();
  log << "Number of unknowns: " << n_dofs << endl;

  CWConstCoefficient rho_coef(param.media.rho_array, false);
  CWConstCoefficient lambda_coef(param.media.lambda_array, false);
  CWConstCoefficient mu_coef(param.media.mu_array, false);

  log << "Fine scale stif matrix..." << flush;
  chrono.Clear();
  ParBilinearForm stif_fine(&fespace);
  stif_fine.AddDomainIntegrator(new ElasticityIntegrator(lambda_coef, mu_coef));
  stif_fine.AddInteriorFaceIntegrator(
     new DGElasticityIntegrator(lambda_coef, mu_coef,
                                param.method.dg_sigma, param.method.dg_kappa));
  stif_fine.AddBdrFaceIntegrator(
     new DGElasticityIntegrator(lambda_coef, mu_coef,
                                param.method.dg_sigma, param.method.dg_kappa));
  stif_fine.Assemble();
  stif_fine.Finalize();
  HypreParMatrix *S_fine = stif_fine.ParallelAssemble();
  log << "done. Time = " << chrono.RealTime() << " sec" << endl;


  log << "Fine scale mass matrix..." << flush;
  chrono.Clear();
  ParBilinearForm mass_fine(&fespace);
  mass_fine.AddDomainIntegrator(new VectorMassIntegrator(rho_coef));
  mass_fine.Assemble();
  mass_fine.Finalize();
  HypreParMatrix *M_fine = mass_fine.ParallelAssemble();
  log << "done. Time = " << chrono.RealTime() << " sec" << endl;

  log << "System matrix..." << flush;
  chrono.Clear();
  ParBilinearForm sys_fine(&fespace);
  sys_fine.AddDomainIntegrator(new VectorMassIntegrator(rho_coef));
  sys_fine.Assemble();
  sys_fine.Finalize();
  HypreParMatrix *Sys_fine = sys_fine.ParallelAssemble();
  log << "done. Time = " << chrono.RealTime() << " sec" << endl;

  //HypreParMatrix SysFine((hypre_ParCSRMatrix*)(*M_fine));
  //SysFine = 0.0;
  //SysFine += D;
  //SysFine += M_fine;


  log << "Fine scale RHS vector... " << flush;
  chrono.Clear();
  ParLinearForm b_fine(&fespace);
  if (param.source.plane_wave)
  {
    PlaneWaveSource plane_wave_source(dim, param);
    b_fine.AddDomainIntegrator(new VectorDomainLFIntegrator(plane_wave_source));
    b_fine.Assemble();
  }
  else
  {
    if (!strcmp(param.source.type, "pointforce"))
    {
      VectorPointForce vector_point_force(dim, param);
      b_fine.AddDomainIntegrator(new VectorDomainLFIntegrator(vector_point_force));
      b_fine.Assemble();
    }
    else if (!strcmp(param.source.type, "momenttensor"))
    {
      MomentTensorSource momemt_tensor_source(dim, param);
      b_fine.AddDomainIntegrator(new VectorDomainLFIntegrator(momemt_tensor_source));
      b_fine.Assemble();
    }
    else MFEM_ABORT("Unknown source type: " + string(param.source.type));
  }
  HypreParVector *b = b_fine.ParallelAssemble();
  const double b_fine_norm = GlobalLpNorm(2, b_fine.Norml2(), MPI_COMM_WORLD);
  log << "||b_h||_L2 = " << b_fine_norm << endl
      << "done. Time = " << chrono.RealTime() << " sec" << endl;

  vector<int> my_cells_dofs;
  my_cells_dofs.reserve(fespace.GetNE() * 10);
  for (int el = 0; el < fespace.GetNE(); ++el)
  {
    const int cellID = fespace.GetAttribute(el) - 1;
    Array<int> vdofs;
    fespace.GetElementVDofs(el, vdofs);
    my_cells_dofs.push_back(-cellID);
    my_cells_dofs.push_back(vdofs.Size());
    for (int d = 0; d < vdofs.Size(); ++d) {
      const int globTDof = fespace.GetGlobalTDofNumber(vdofs[d]);
      my_cells_dofs.push_back(globTDof);
    }
  }

#ifdef MFEM_DEBUG
  log << "my_cells_dofs:\n";
  for (size_t i = 0; i < my_cells_dofs.size(); ++i) {
    if (my_cells_dofs[i] < 0)
      log << endl;
    log << my_cells_dofs[i] << " ";
  }
#endif // MFEM_DEBUG

  if (myid == 0)
  {
    MPI_Status status;
    for (int rank = 1; rank < size; ++rank)
    {
      int ncell_dofs;
      MPI_Recv(&ncell_dofs, 1, MPI_INT, rank, 101, MPI_COMM_WORLD, &status);
      vector<int> cell_dofs(ncell_dofs);
      MPI_Recv(&cell_dofs[0], ncell_dofs, MPI_INT, rank, 102, MPI_COMM_WORLD, &status);
      my_cells_dofs.reserve(my_cells_dofs.size() + ncell_dofs);
      my_cells_dofs.insert(my_cells_dofs.end(), cell_dofs.begin(), cell_dofs.end());
    }
  }
  else
  {
    int my_ncell_dofs = my_cells_dofs.size();
    MPI_Send(&my_ncell_dofs, 1, MPI_INT, 0, 101, MPI_COMM_WORLD);
    MPI_Send(&my_cells_dofs[0], my_ncell_dofs, MPI_INT, 0, 102, MPI_COMM_WORLD);
  }

  int nglob_cell_dofs = my_cells_dofs.size();
  MPI_Bcast(&nglob_cell_dofs, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (myid != 0)
    my_cells_dofs.resize(nglob_cell_dofs);
  MPI_Bcast(&my_cells_dofs[0], nglob_cell_dofs, MPI_INT, 0, MPI_COMM_WORLD);

  const int globNE = param.mesh->GetNE();
  log << "globNE " << globNE << endl;
  vector<vector<int> > map_cell_dofs(globNE);
  for (int el = 0, k = 0; el < globNE; ++el)
  {
    MFEM_ASSERT(k < (int)my_cells_dofs.size(), "k is out of range");
    int cellID = my_cells_dofs[k++];
    MFEM_ASSERT(cellID <= 0, "Incorrect cellID");
    cellID = -cellID;
    const int ndofs = my_cells_dofs[k++];
    if (!(cellID >= 0 && cellID < globNE))
      log << "el " << el << " cellID " << cellID << endl;
    MFEM_ASSERT(cellID >= 0 && cellID < globNE, "cellID is out of range");
    MFEM_ASSERT(map_cell_dofs[cellID].empty(), "This cellID has been already added");
    map_cell_dofs[cellID].resize(ndofs);
    for (int i = 0; i < ndofs; ++i)
      map_cell_dofs[cellID][i] = my_cells_dofs[k++];
  }

#ifdef MFEM_DEBUG
  log << "map_cell_dofs:\n";
  for (size_t i = 0; i < map_cell_dofs.size(); ++i) {
    log << i << " ";
    for (size_t j = 0; j < map_cell_dofs[i].size(); ++j)
      log << map_cell_dofs[i][j] << " ";
    log << endl;
  }

  log << "map_coarse_cell_fine_cells:\n";
  for (size_t i = 0; i < param.map_coarse_cell_fine_cells.size(); ++i) {
    log << i << " ";
    for (size_t j = 0; j < param.map_coarse_cell_fine_cells[i].size(); ++j)
      log << param.map_coarse_cell_fine_cells[i][j] << " ";
    log << endl;
  }
#endif // MFEM_DEBUG

  if (myid == 0)
    cout << "Computation of basis functions..." << endl;
  chrono.Clear();
  vector<vector<int> > local2global;
  vector<DenseMatrix> R;
  compute_R_matrices(log, param.map_coarse_cell_fine_cells, map_cell_dofs,
                     local2global, R);
  if (myid == 0)
    cout << "Computation of basis functions is done. Time = "
         << chrono.RealTime() << " sec" << endl;

  // my portion of the global sparse R matrix
  int my_nrows = 0;
  int my_ncols = 0;
  int my_nnonzero = 0;
  for (size_t i = 0; i < R.size(); ++i)
  {
    const int h = R[i].Height();
    const int w = R[i].Width();
    my_nrows += w; // transpose
    my_ncols += h; // transpose
    my_nnonzero += h * w;
  }

  int glob_nrows = 0;
  int glob_ncols = 0;

  MPI_Allreduce(&my_nrows, &glob_nrows, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&my_ncols, &glob_ncols, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  log << "\nmy_nrows " << my_nrows << " my_ncols " << my_ncols << endl;
  log << "glob_nrows " << glob_nrows << " glob_ncols " << glob_ncols << endl;

  int *Rrows = new int[size + 1];
  if (myid == 0)
  {
    Rrows[0] = 0;
    Rrows[1] = my_nrows;
    MPI_Status status;
    for (int rank = 1; rank < size; ++rank)
    {
      int nrows;
      MPI_Recv(&nrows, 1, MPI_INT, rank, 103, MPI_COMM_WORLD, &status);
      Rrows[rank + 1] = Rrows[rank] + nrows;
    }
  }
  else
  {
    MPI_Send(&my_nrows, 1, MPI_INT, 0, 103, MPI_COMM_WORLD);
  }
  MPI_Bcast(Rrows, size + 1, MPI_INT, 0, MPI_COMM_WORLD);

#ifdef MFEM_DEBUG
  log << "\nRrows: ";
  for (int i = 0; i < size + 1; ++i)
    log << Rrows[i] << " ";
  log << endl;
#endif // MFEM_DEBUG

  const int start_row = Rrows[myid];
  const int end_row   = Rrows[myid + 1];
  MFEM_VERIFY(my_nrows == end_row - start_row, "Number of rows mismatch");

#ifdef MFEM_DEBUG
  const int *S_row_starts = S_fine->RowPart();
  const int *S_col_starts = S_fine->ColPart();
  const int *M_row_starts = M_fine->RowPart();
  const int *M_col_starts = M_fine->ColPart();
  log << "S_row_starts: " << S_row_starts[0] << " " << S_row_starts[1] << endl;
  log << "S_col_starts: " << S_col_starts[0] << " " << S_col_starts[1] << endl;
  log << "M_row_starts: " << M_row_starts[0] << " " << M_row_starts[1] << endl;
  log << "M_col_starts: " << M_col_starts[0] << " " << M_col_starts[1] << endl;
#endif // MFEM_DEBUG

  int *Ri = new int[my_nrows + 1];
  int *Rj = new int[my_nnonzero];
  double *Rdata = new double[my_nnonzero];

  {
    Ri[0] = 0;
    int k = 0;
    int p = 0;
    for (size_t r = 0; r < R.size(); ++r)
    {
      const int h = R[r].Height();
      const int w = R[r].Width();
      for (int i = 0; i < w; ++i)
      {
        Ri[k+1] = Ri[k] + h;
        ++k;

        for (int j = 0; j < h; ++j)
        {
          Rj[p] = local2global[r][j];
          Rdata[p] = R[r](j, i);
          ++p;
        }
      }
    }
  }

  // if HYPRE_NO_GLOBAL_PARTITION is ON (it's default)
  int myRrows[] = { start_row, end_row };

  /** Creates a general parallel matrix from a local CSR matrix on each
      processor described by the I, J and data arrays. The local matrix should
      be of size (local) nrows by (global) glob_ncols. The new parallel matrix
      contains copies of all input arrays (so they can be deleted). */
  HypreParMatrix R_global(MPI_COMM_WORLD, my_nrows, glob_nrows, glob_ncols,
                          Ri, Rj, Rdata, myRrows, S_fine->RowPart());

  HypreParMatrix *R_global_T = R_global.Transpose();

  delete[] Rrows;
  delete[] Rdata;
  delete[] Rj;
  delete[] Ri;

  HypreParMatrix *M_coarse = RAP(M_fine, R_global_T);
  HypreParMatrix *S_coarse = RAP(S_fine, R_global_T);

  chrono.Clear();
  log << "Invert system matrix A..." << endl;
  HypreParMatrix *A_coarse = RAP(M_fine, R_global_T); // system matrix

  // check that the matrix is block diagonal
  SparseMatrix A_coarse_offdiag_blocks;
  int *cmap;
  A_coarse->GetOffd(A_coarse_offdiag_blocks, cmap);
  MFEM_VERIFY(A_coarse_offdiag_blocks.GetI()[A_coarse_offdiag_blocks.Height()] == 0,
              "The off diagonal block is not zero!");

  // inversion of the blocks
  hypre_ParCSRMatrix *ACoarse = *A_coarse;
  hypre_CSRMatrix *Adiag = hypre_ParCSRMatrixDiag(ACoarse);
  const int *Adiag_I = hypre_CSRMatrixI(Adiag);
  const int *Adiag_J = hypre_CSRMatrixJ(Adiag);
  double *Adiag_data = hypre_CSRMatrixData(Adiag);

  int offset_J = 0, offset_data = 0;
  for (size_t r = 0; r < R.size(); ++r)
  {
    DenseMatrix block(R[r].Width());
#ifdef MFEM_DEBUG
    // check that all blocks are on diagonal
    for (int i = 0; i < block.Height(); ++i) {
      vector<int> diag_J(&Adiag_J[offset_data + i * block.Width()],
                         &Adiag_J[offset_data + (i + 1) * block.Width()]);
      sort(diag_J.begin(), diag_J.end());
      for (int j = 0; j < block.Width(); ++j)
        MFEM_VERIFY(diag_J[j] == offset_J + j, "Adiag_J has unexpected value");
    }
#endif // MFEM_DEBUG
    // extract the block
    for (int i = 0, k = 0; i < block.Height(); ++i) {
      for (int j = 0; j < block.Width(); ++j) {
        MFEM_ASSERT(Adiag_J[offset_data + k] - offset_J >= 0 &&
                    Adiag_J[offset_data + k] - offset_J < block.Width(), "Out of range");
        block(i, Adiag_J[offset_data + k] - offset_J) = Adiag_data[offset_data + k];
        ++k;
      }
    }

    block.Invert();

    // put the inverted block back
    for (int i = 0, k = 0; i < block.Height(); ++i) {
      for (int j = 0; j < block.Width(); ++j) {
        Adiag_data[offset_data + k] = block(i, Adiag_J[offset_data + k] - offset_J);
        ++k;
      }
    }

    offset_J += block.Width();
    offset_data += block.Height() * block.Width();
    MFEM_VERIFY(offset_data <= Adiag_I[hypre_CSRMatrixNumRows(Adiag)], "Incorrect diag block");
  }
  log << "done. Time = " << chrono.RealTime() << " sec" << endl;


  if (param.output.print_matrices)
  {
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output R local matrices..." << flush;
      for (size_t r = 0; r < R.size(); ++r) {
        const string fname = string(param.output.directory) + "/r" + d2s(r) + "_local_par.dat." + d2s(myid);
        ofstream mout(fname.c_str());
        MFEM_VERIFY(mout, "Cannot open file " << fname);
        R[r].PrintMatlab(mout);
      }
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output R_global matrix..." << flush;
      const string fname = string(param.output.directory) + "/r_global_par.dat";
      print_par_matrix_matlab(R_global, fname);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output R_global_T matrix..." << flush;
      const string fname = string(param.output.directory) + "/r_global_t_par.dat";
      print_par_matrix_matlab(*R_global_T, fname);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output M_fine matrix..." << flush;
      const string fname = string(param.output.directory) + "/m_fine_par.dat";
      print_par_matrix_matlab(*M_fine, fname);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output S_fine matrix..." << flush;
      const string fname = string(param.output.directory) + "/s_fine_par.dat";
      print_par_matrix_matlab(*S_fine, fname);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output M_coarse matrix..." << flush;
      const string fname = string(param.output.directory) + "/m_coarse_par.dat";
      print_par_matrix_matlab(*M_coarse, fname);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output S_coarse matrix..." << flush;
      const string fname = string(param.output.directory) + "/s_coarse_par.dat";
      print_par_matrix_matlab(*S_coarse, fname);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output A_coarse matrix..." << flush;
      const string fname = string(param.output.directory) + "/a_coarse_par.dat";
      print_par_matrix_matlab(*A_coarse, fname);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output A_coarse diag blocks..." << flush;
      const string fname = string(param.output.directory) + "/a_coarse_diags.dat." + d2s(myid);
      ofstream mout(fname.c_str());
      MFEM_VERIFY(mout, "Cannot open file " << fname);
      SparseMatrix A_coarse_diag_blocks;
      A_coarse->GetDiag(A_coarse_diag_blocks);
      A_coarse_diag_blocks.PrintMatlab(mout);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
    {
      chrono.Clear();
      if (myid == 0)
        cout << "Output A_coarse offdiag blocks..." << flush;
      const string fname = string(param.output.directory) + "/m_coarse_offdiags.dat." + d2s(myid);
      ofstream mout(fname.c_str());
      MFEM_VERIFY(mout, "Cannot open file " << fname);
      A_coarse_offdiag_blocks.PrintMatlab(mout);
      if (myid == 0)
        cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
    }
  }

  HypreParVector b_coarse(*M_coarse);
  R_global.Mult(*b, b_coarse);
  {
    const double norm = GlobalLpNorm(2, b_coarse.Norml2(), MPI_COMM_WORLD);
    log << "||b_H|| = " << norm << endl;
  }

  const string method_name = "parGMsFEM_";

  ofstream *seisU; // for displacement
  ofstream *seisV; // for velocity
  if (myid == 0)
  {
    chrono.Clear();
    cout << "Open seismograms files..." << flush;
    open_seismo_outs(seisU, seisV, param, method_name);
    cout << "done. Time = " << chrono.RealTime() << " sec" << endl;
  }

  HypreParVector U_0(*M_coarse); U_0 = 0.0;
  HypreParVector U_1(*M_coarse); U_1 = 0.0;
  HypreParVector U_2(*M_coarse); U_2 = 0.0;
  ParGridFunction u_0(&fespace);
  HypreParVector u_fine(&fespace);

  const int n_time_steps = param.T / param.dt + 0.5; // nearest integer
  const int tenth = 0.1 * n_time_steps;

  vector<int> loc2globSerial;
  if (param.output.serial_solution)
  {
    FiniteElementSpace fespace_serial(param.mesh, &fec, dim);
    loc2globSerial.resize(fespace.GetVSize(), -1);
    for (int el = 0; el < fespace.GetNE(); ++el)
    {
      const int glob_cell = fespace.GetAttribute(el) - 1;
      Array<int> loc_vdofs, glob_vdofs;
      fespace.GetElementVDofs(el, loc_vdofs);
      fespace_serial.GetElementVDofs(glob_cell, glob_vdofs);
      MFEM_ASSERT(loc_vdofs.Size() == glob_vdofs.Size(), "Number of dofs mismatches");
      for (int d = 0; d < loc_vdofs.Size(); ++d)
        loc2globSerial[loc_vdofs[d]] = glob_vdofs[d];
    }
#ifdef MFEM_DEBUG
    log << "loc2globSerial" << endl;
    for (size_t i = 0; i < loc2globSerial.size(); ++i) {
      log << i << " " << loc2globSerial[i] << endl;
      MFEM_ASSERT(loc2globSerial[i] >= 0, "Local index " << i << " is not "
                  "mapped to a global one");
    }
#endif // MFEM_DEBUG
  }

  if (param.output.cells_containing_receivers)
  {
    for (size_t r = 0; r < param.sets_of_receivers.size(); ++r) {
      const ReceiversSet *set = param.sets_of_receivers[r];
      const vector<int> &ser_cells = set->get_cells_containing_receivers();
      const vector<int> &par_cells = set->get_par_cells_containing_receivers();
      const vector<Vertex> &receivers = set->get_receivers();
      log << "receivers set " << r << endl;
      for (int p = 0; p < set->n_receivers(); ++p) {
        log << p << " ";
        for (int d = 0; d < param.dimension; ++d)
          log << receivers[p](d) << " ";
        log << " : ser_cell " << ser_cells[p] << " par_cell  " << par_cells[p] << endl;
      }
    }
  }

  if (myid == 0) {
    cout << "N time steps = " << n_time_steps
         << "\nTime loop..." << endl;
  }

  // the values of the time-dependent part of the source
  vector<double> time_values(n_time_steps);
  for (int time_step = 1; time_step <= n_time_steps; ++time_step)
  {
    const double cur_time = time_step * param.dt;
    time_values[time_step-1] = RickerWavelet(param.source, cur_time - param.dt);
  }

  const string name = method_name + param.output.extra_string;
  const string pref_path = string(param.output.directory) + "/" + SNAPSHOTS_DIR;
  VisItDataCollection visit_dc(name.c_str(), param.par_mesh);
  if (param.step_snap <= n_time_steps) {
    R_global_T->Mult(U_0, u_fine);
    if (param.output.snap_format_VisIt()) {
      visit_dc.SetPrefixPath(pref_path.c_str());
      visit_dc.RegisterField("GMs_U", &u_0);
      visit_dc.SetCycle(0);
      visit_dc.SetTime(0.0);
      u_0 = u_fine;
      visit_dc.Save();
    }
    if (param.output.snap_format_GLVis()) {
      ostringstream mesh_name;
      mesh_name << param.output.directory << "/" << param.output.extra_string << "_mesh." << setfill('0') << setw(6) << myid;
      ofstream mesh_ofs(mesh_name.str().c_str());
      mesh_ofs.precision(8);
      param.par_mesh->Print(mesh_ofs);

      ostringstream sol_name;
      sol_name << param.output.directory << "/" << param.output.extra_string << "_sol_t0." << setfill('0') << setw(6) << myid;
      ofstream sol_ofs(sol_name.str().c_str());
      sol_ofs.precision(8);
      ParGridFunction x(&fespace, &u_fine);
      x.Save(sol_ofs);
    }
  }

  StopWatch time_loop_timer;
  time_loop_timer.Start();
  double time_of_snapshots = 0.;
  double time_of_seismograms = 0.;


  for (int t_step = 1; t_step <= n_time_steps; ++t_step)
  {
    par_time_step(*A_coarse, *M_coarse, *S_coarse, b_coarse, time_values[t_step-1],
                  param.dt, U_0, U_1, U_2, log);

    // Compute and print the L^2 norm of the error
    if (t_step % tenth == 0) {
      const double glob_norm = GlobalLpNorm(2, U_0.Norml2(), MPI_COMM_WORLD);
      if (myid == 0) {
        cout << "step " << t_step << " / " << n_time_steps
             << " ||U||_{L^2} = " << glob_norm << endl;
      }
    }

    if (t_step % param.step_snap == 0) {
      StopWatch timer;
      timer.Start();
      R_global_T->Mult(U_0, u_fine);
#ifdef MFEM_DEBUG
      const double norm = GlobalLpNorm(2, u_fine.Norml2(), MPI_COMM_WORLD);
      log << "t_step " << t_step << " ||u_h|| = " << norm << endl;
#endif // MFEM_DEBUG
      if (param.output.snap_format_VisIt()) {
        visit_dc.SetCycle(t_step);
        visit_dc.SetTime(t_step*param.dt);
        u_0 = u_fine;
        visit_dc.Save();
      }
      if (param.output.snap_format_GLVis()) {
        ostringstream sol_name;
        sol_name << param.output.directory << "/" << param.output.extra_string << "_sol_t" << t_step << "." << setfill('0') << setw(6) << myid;
        ofstream sol_ofs(sol_name.str().c_str());
        sol_ofs.precision(8);
        ParGridFunction x(&fespace, &u_fine);
        x.Save(sol_ofs);
      }
      timer.Stop();
      time_of_snapshots += timer.RealTime();
    }

    if (t_step % param.step_seis == 0) {
      StopWatch timer;
      timer.Start();
      if (t_step % param.step_snap != 0)
        R_global_T->Mult(U_0, u_fine);
      par_output_seismograms(param, fespace, u_fine, seisU);
      timer.Stop();
      time_of_seismograms += timer.RealTime();
    }

    if (param.output.serial_solution && t_step % param.step_seis == 0) {
      vector<double> glob_U(n_dofs, 0.);
      vector<double> glob_solution(n_dofs, 0.);

      HypreParVector u_tmp(&fespace);
      R_global_T->Mult(U_0, u_tmp);
      for (int i = 0; i < u_tmp.Size(); ++i)
        glob_U[loc2globSerial[i]] = u_tmp(i);

      MPI_Reduce(&glob_U[0], &glob_solution[0], n_dofs, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

      if (myid == 0) {
        const string fname = string(param.output.directory) + "/" +
                             param.output.extra_string + "_sol_t" + d2s(t_step) +
                             ".bin";
        ofstream bin_sol(fname.c_str(), std::ios::binary);
        MFEM_VERIFY(bin_sol, "Cannot open file " << fname);
        for (int i = 0; i < n_dofs; ++i) {
          float val = glob_solution[i];
          bin_sol.write(reinterpret_cast<char*>(&val), sizeof(val));
        }
      }
    }

    if (t_step % param.step_seis == 0 || t_step % param.step_snap == 0) {
      int loc_bad = CheckFinite(u_fine.GetData(), u_fine.Size());
      int glob_bad = 0;
      MPI_Reduce(&loc_bad, &glob_bad, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
      if (myid == 0)
        MFEM_VERIFY(glob_bad == 0, "The solution has non-finite numbers");

      const double norm = GlobalLpNorm(2, u_fine.Norml2(), MPI_COMM_WORLD);
      if (myid == 0)
        MFEM_VERIFY(norm < DIVERGENCE_LIMIT, "The solution diverges (blows up)");
    }
  }

  time_loop_timer.Stop();

  if (myid == 0) {
    delete[] seisU;
    delete[] seisV;
  }

  if (myid == 0) {
    cout << "Time loop is over\n\tpure time = " << time_loop_timer.UserTime()
         << "\n\ttime of snapshots = " << time_of_snapshots
         << "\n\ttime of seismograms = " << time_of_seismograms << endl;
  }

  delete R_global_T;

  delete A_coarse;
  delete S_coarse;
  delete M_coarse;

  delete b;
  delete Sys_fine;
  delete M_fine;
  delete S_fine;
}


#endif // MFEM_USE_MPI
