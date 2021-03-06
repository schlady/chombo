#ifdef CH_LANG_CC
/*
*      _______              __
*     / ___/ /  ___  __ _  / /  ___
*    / /__/ _ \/ _ \/  V \/ _ \/ _ \
*    \___/_//_/\___/_/_/_/_.__/\___/
*    Please refer to Copyright.txt, in Chombo's root directory.
*/
#endif

#include <iomanip>

#include "parstream.H"
#include "ParmParse.H"
#include "Box.H"
#include "Vector.H"
#include "IntVectSet.H"
#include "FArrayBox.H"
#include "DisjointBoxLayout.H"
#include "LayoutIterator.H"
#include "SPMD.H"
#include "LoadBalance.H"
#include "LevelFluxRegister.H"
#include "ProblemDomain.H"
#include "BoxIterator.H"
#include "computeSum.H"
#include "CH_HDF5.H"
#include "AMRIO.H"
#include "AMRLevel.H"
#include "scalarDirichletBC.H"
#include "LoHiCenter.H"
#include "PoissonBC.H"
#include "AMRLevelMHD.H"
#include "GradDivOp.H"
#include "GhostBC.H"
#include "HOExtrapBC.H"
#include "ExtrapBC.H"

#include "MHDPhysics.H"
#include "MHDPhysicsF_F.H"

// Constructor
AMRLevelMHD::AMRLevelMHD()
{
  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD default constructor" << endl;
  }

  m_gdnvPhysics = NULL;
  m_paramsDefined = false;
}

// Destructor
AMRLevelMHD::~AMRLevelMHD()
{
  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD destructor" << endl;
  }

  if (m_gdnvPhysics != NULL)
  {
    delete m_gdnvPhysics;
    m_gdnvPhysics = NULL;
  }

  m_paramsDefined = false;
}

void AMRLevelMHD::defineParams(const Real&                 a_cfl,
                               const Real&                 a_domainLength,
                               const int&                  a_verbosity,
                               const Real&                 a_refineThresh,
                               const int&                  a_tagBufferSize,
                               const Real&                 a_initialDtMultiplier,
                               const GodunovPhysics* const a_godunovPhysics,
                               const int&                  a_normalPredOrder,
                               const bool&                 a_useFourthOrderSlopes,
                               const bool&                 a_usePrimLimiting,
                               const bool&                 a_useCharLimiting,
                               const bool&                 a_useFlattening,
                               const bool&                 a_useArtificialViscosity,
                               const Real&                 a_artificialViscosity,                               
                               const bool&                 a_doFilterBField,
                               const bool&                 a_projectB)

{
  // Set the CFL number
  m_cfl = a_cfl;

  // Set the physical dimension of the longest side of the domain
  m_domainLength = a_domainLength;

  verbosity(a_verbosity);

  // Set the refinement threshold
  m_refineThresh = a_refineThresh;

  // Set the tag buffer size
  m_tagBufferSize = a_tagBufferSize;

  initialDtMultiplier(a_initialDtMultiplier);

  if (m_gdnvPhysics != NULL)
  {
    delete m_gdnvPhysics;
    m_gdnvPhysics = NULL;
  }

  m_gdnvPhysics = a_godunovPhysics->new_godunovPhysics();

  m_normalPredOrder = a_normalPredOrder;

  // Store the slope computation parameters
  m_useFourthOrderSlopes = a_useFourthOrderSlopes;
  m_usePrimLimiting      = a_usePrimLimiting;
  m_useCharLimiting      = a_useCharLimiting;
  m_useFlattening        = a_useFlattening;

  // Artificial viscosity coefficient must be greater than zero
  CH_assert(!a_useArtificialViscosity || (a_artificialViscosity >= 0.0));

  // Store the artificial viscosity flag and coefficient
  m_useArtificialViscosity = a_useArtificialViscosity;
  m_artificialViscosity    = a_artificialViscosity;

  // Filter B Field
  m_doFilterBField = a_doFilterBField;

  // project B field
  m_projectB = a_projectB;

  m_paramsDefined = true;
}

// This instance should never get called - historical
void AMRLevelMHD::define(AMRLevel*  a_coarserLevelPtr,
                         const Box& a_problemDomain,
                         int        a_level,
                         int        a_refRatio)
{
  ProblemDomain physdomain(a_problemDomain);

  MayDay::Error("AMRLevelMHD::define -\n\tShould never be called with a Box for a problem domain");
}

// Define new AMR level
void AMRLevelMHD::define(AMRLevel*            a_coarserLevelPtr,
                         const ProblemDomain& a_problemDomain,
                         int                  a_level,
                         int                  a_refRatio)
{
  CH_assert(m_paramsDefined);

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::define " << a_level << endl;
  }

  // Call inherited define
  AMRLevel::define(a_coarserLevelPtr,
                   a_problemDomain,
                   a_level,
                   a_refRatio);

  // Get setup information from the next coarser level
  if (a_coarserLevelPtr != NULL)
  {
    AMRLevelMHD* amrGodPtr = dynamic_cast<AMRLevelMHD*>(a_coarserLevelPtr);

    if (amrGodPtr != NULL)
    {
      m_cfl           = amrGodPtr->m_cfl;
      m_domainLength  = amrGodPtr->m_domainLength;
      m_refineThresh  = amrGodPtr->m_refineThresh;
      m_tagBufferSize = amrGodPtr->m_tagBufferSize;
    }
    else
    {
      MayDay::Error("AMRLevelMHD::define: a_coarserLevelPtr is not castable to AMRLevelMHD*");
    }
  }

  // Compute the grid spacing
  m_dx = m_domainLength / a_problemDomain.domainBox().longside();

  // Nominally, one layer of ghost cells is maintained permanently and
  // individual computations may create local data with more
  m_numGhost = 4;

  CH_assert(m_gdnvPhysics != NULL);

  MHDPhysics* mhdPhysics = dynamic_cast<MHDPhysics*>(m_gdnvPhysics);
  if (mhdPhysics == NULL)
  {
    MayDay::Error("AMRLevelMHD::define: m_gdnvPhysics is not castable to MHDPhysics*");
  }

  mhdPhysics->define(m_problem_domain,m_dx);

  //Domain BC for Projection solve.
  DomainGhostBC dombc;
  for (int dir=0; dir<SpaceDim; dir++)
    {
      SideIterator sit;
      for (sit.reset(); sit.ok(); ++sit)
        {
          NeumannBC thisBC(dir,sit());
          dombc.setBoxGhostBC(thisBC);
        }
    }
  m_poissonOp.setDomainGhostBC(dombc);

  // Get additional information from the patch integrator
  m_numStates =  mhdPhysics->numConserved();
  m_numPrims =  mhdPhysics->numPrimitives();
  m_numFluxes = mhdPhysics->numFluxes();
  m_stateNames = mhdPhysics->stateNames();
  m_plotNames = mhdPhysics->plotNames();
}

// Advance by one timestep
Real AMRLevelMHD::advance()
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::advance level " << m_level << " to time " << m_time + m_dt << endl;
  }

  // Copy the new to the old
  for (DataIterator dit = m_UNew.dataIterator(); dit.ok(); ++dit)
  {
    m_UOld[dit()].copy(m_UNew[dit()]);
  }

  Real newDt = 0.0;

  // Set up arguments to LevelGodunov::step based on whether there are
  // coarser and finer levels

  // Undefined flux register in case we need it
  LevelFluxRegister dummyFR;

  // Undefined leveldata in case we need it
  const LevelData<FArrayBox> dummyData;

  // Set arguments to dummy values and then fix if real values are available
  LevelFluxRegister* coarserFR = &dummyFR;
  LevelFluxRegister* finerFR   = &dummyFR;

  const LevelData<FArrayBox>* coarserDataOld = &dummyData;
  const LevelData<FArrayBox>* coarserDataNew = &dummyData;

  Real tCoarserOld = 0.0;
  Real tCoarserNew = 0.0;

  // A coarser level exists
  if (m_hasCoarser)
  {
    AMRLevelMHD* coarserPtr = getCoarserLevel();

    // Recall that my flux register goes between my level and the next
    // finer level
    coarserFR = &coarserPtr->m_fluxRegister;

    coarserDataOld = &coarserPtr->m_UOld;
    coarserDataNew = &coarserPtr->m_UNew;

    tCoarserNew = coarserPtr->m_time;
    tCoarserOld = tCoarserNew - coarserPtr->m_dt;
  }

  // A finer level exists
  if (m_hasFiner)
  {
    // Recall that my flux register goes between my level and the next
    // finer level
    finerFR = &m_fluxRegister;
  }

   m_levelGodunov.computeWHalf(m_whalf,
                               m_UNew,
                               dummyData,
                               *coarserDataOld,
                               tCoarserOld,
                               *coarserDataNew,
                               tCoarserNew,
                               m_time,
                               m_dt);

   // Projection step
   // First compute the divergence

   if (m_projectB)
     {
       DivergenceB(m_rhs, m_whalf);
       bool initializePhiToZero = true;
       if (m_hasCoarser)
         {
           AMRLevelMHD* coarserPtr = getCoarserLevel();
           const LevelData<FArrayBox>& coarserPhi = coarserPtr->m_Phi;
           
           m_levelSolver.levelSolve(m_Phi,&coarserPhi,m_rhs,initializePhiToZero);
           
           QuadCFInterp quadCFI;
           quadCFI.define(m_grids,&(coarserPtr->m_UNew.disjointBoxLayout()),m_dx,
                          m_ref_ratio,1,m_problem_domain);
           quadCFI.coarseFineInterp(m_Phi,coarserPhi);
         }
       else 
         {
           m_levelSolver.levelSolve(m_Phi,NULL,m_rhs,initializePhiToZero);
         }
       
       ProjectBField(m_whalf,m_Phi);
       DivergenceB(m_rhs, m_whalf);
     } // end if we're projecting B

   // dU is the increment which will be applied to U
   // dU is -dt/dx*div(hyperbolic fluxes)

   LevelData<FArrayBox> dU(m_grids,m_numStates);

   newDt = m_levelGodunov.computeUpdate(dU,
                                        *finerFR,
                                        *coarserFR,
                                        m_UNew,
                                        m_whalf,
                                        m_time,
                                        m_dt);

  for (DataIterator dit = m_UNew.dataIterator(); dit.ok(); ++dit)
  {
    m_UNew[dit()]+=dU[dit()];
  }

  m_UNew.exchange(Interval(0,m_numStates-1));

  if (m_doFilterBField)
  {
     FilterBField(m_UNew, *finerFR, *coarserFR);
  }

  // Update the time and store the new timestep
  m_time += m_dt;
  Real returnDt = m_cfl * newDt;

  m_dtNew = returnDt;

  return returnDt;
}

// Things to do after a timestep
void AMRLevelMHD::postTimeStep()
{
  CH_assert(allDefined());

  // Used for conservation tests
  static Real orig_integral = 0.0;
  static Real last_integral = 0.0;
  static bool first = true;

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::postTimeStep " << m_level << endl;
  }

  if (m_hasFiner)
  {
    // Reflux
    Real scale = -1.0/m_dx;
    m_fluxRegister.reflux(m_UNew,scale);

    // Average from finer level data
    AMRLevelMHD* amrGodFinerPtr = getFinerLevel();

    amrGodFinerPtr->m_coarseAverage.averageToCoarse(m_UNew,
                                                    amrGodFinerPtr->m_UNew);
  }

  if (s_verbosity >= 2 && m_level == 0)
  {
    int nRefFine = 1;

    pout() << "AMRLevelMHD::postTimeStep:" << endl;
    pout() << "  Sums:" << endl;
    for (int comp = 0; comp < m_numStates; comp++)
    {
      Interval curComp(comp,comp);
      Real integral = computeSum(m_UNew,NULL,nRefFine,m_dx,curComp);

      pout() << "    " << setw(23)
             << setprecision(16)
             << setiosflags(ios::showpoint)
             << setiosflags(ios::scientific)
             << integral
             << " --- " << m_stateNames[comp];

      if (comp == 0 && !first)
      {
        pout() << " (" << setw(23)
               << setprecision(16)
               << setiosflags(ios::showpoint)
               << setiosflags(ios::scientific)
               << (integral-last_integral)
               << " " << setw(23)
               << setprecision(16)
               << setiosflags(ios::showpoint)
               << setiosflags(ios::scientific)
               << (integral-orig_integral)
               << ")";
      }

      pout() << endl;

      if (comp == 0)
      {
        if (first)
        {
          orig_integral = integral;
          first = false;
        }

        last_integral = integral;
      }
    }
  }

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::postTimeStep " << m_level << " finished" << endl;
  }
}

// Create tags for regridding
void AMRLevelMHD::tagCells(IntVectSet& a_tags)
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::tagCells " << m_level << endl;
  }

  // Since tags are calculated using only current time step data, use
  // the same tagging function for initialization and for regridding.
  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::tagCellsInit " << m_level << endl;
  }

  // Create tags based on undivided gradient of density
  const DisjointBoxLayout& levelDomain = m_UNew.disjointBoxLayout();
  IntVectSet localTags;

  // If there is a coarser level interpolate undefined ghost cells
  if (m_hasCoarser)
  {
    const AMRLevelMHD* amrGodCoarserPtr = getCoarserLevel();

    PiecewiseLinearFillPatch pwl(levelDomain,
                                 amrGodCoarserPtr->m_UNew.disjointBoxLayout(),
                                 m_numStates,
                                 amrGodCoarserPtr->m_problem_domain,
                                 amrGodCoarserPtr->m_ref_ratio,
                                 1);

    pwl.fillInterp(m_UNew,
                   amrGodCoarserPtr->m_UNew,
                   amrGodCoarserPtr->m_UNew,
                   1.0,
                   0,
                   0,
                   m_numStates);
  }
  m_UNew.exchange(Interval(0,m_numStates-1));

  // Compute relative gradient
  DataIterator dit = levelDomain.dataIterator();
  for (dit.begin(); dit.ok(); ++dit)
  {
    const Box& b = levelDomain[dit()];
    FArrayBox gradFab(b,SpaceDim);
    const FArrayBox& UFab = m_UNew[dit()];

    for (int dir = 0; dir < SpaceDim; ++dir)
    {
      const Box bCenter = b & grow(m_problem_domain,-BASISV(dir));

      const Box bLo     = b & adjCellLo(bCenter,dir);
      const int hasLo = ! bLo.isEmpty();

      const Box bHi     = b & adjCellHi(bCenter,dir);
      const int hasHi = ! bHi.isEmpty();

      FORT_GETRELGRADF(CHF_FRA1(gradFab,dir),
                       CHF_CONST_FRA1(UFab,0),
                       CHF_CONST_INT(dir),
                       CHF_BOX(bLo),
                       CHF_CONST_INT(hasLo),
                       CHF_BOX(bHi),
                       CHF_CONST_INT(hasHi),
                       CHF_BOX(bCenter));
    }

    FArrayBox gradMagFab(b,1);
    FORT_MAGNITUDEF(CHF_FRA1(gradMagFab,0),
                    CHF_CONST_FRA(gradFab),
                    CHF_BOX(b));

    // Tag where gradient exceeds threshold
    BoxIterator bit(b);
    for (bit.begin(); bit.ok(); ++bit)
    {
      const IntVect& iv = bit();

      if (gradMagFab(iv) >= m_refineThresh)
      {
        localTags |= iv;
      }
    }
  }

  localTags.grow(m_tagBufferSize);

  // Need to do this in two steps unless a IntVectSet::operator &=
  // (ProblemDomain) operator is defined
  Box localTagsBox = localTags.minBox();
  localTagsBox &= m_problem_domain;
  localTags &= localTagsBox;

  a_tags = localTags;

  /*
    Vector<IntVectSet> allTags;

    const int destProc = uniqueProc(SerialTask::compute);

    gather(allTags,localTags,destProc);

    if (procID() == uniqueProc(SerialTask::compute))
    {
    for (int i = 0; i < allTags.size(); ++i)
    {
    a_tags |= allTags[i];
    }
    }
  */
}

// Create tags at initialization
void AMRLevelMHD::tagCellsInit(IntVectSet& a_tags)
{
  CH_assert(allDefined());

  tagCells(a_tags);
}

// Set up data on this level after regridding
void AMRLevelMHD::regrid(const Vector<Box>& a_newGrids)
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::regrid " << m_level << endl;
  }

  // Save original grids and load balance
  m_level_grids = a_newGrids;
  m_grids = loadBalance(a_newGrids);

  if (s_verbosity >= 4)
  {
    // Indicate/guarantee that the indexing below is only for reading
    // otherwise an error/assertion failure occurs
    const DisjointBoxLayout& constGrids = m_grids;

    pout() << "new grids: " << endl;

    for (LayoutIterator lit = constGrids.layoutIterator(); lit.ok(); ++lit)
    {
      pout() << constGrids[lit()] << endl;
    }
  }

  // Save data for later

  for (DataIterator dit = m_UNew.dataIterator(); dit.ok(); ++dit)
  {
    m_UOld[dit()].copy(m_UNew[dit()]);
  }

  // Reshape state with new grids
  IntVect ivGhost = m_numGhost * IntVect::Unit;
  m_UNew.define(m_grids,m_numStates,ivGhost);
  m_source.define(m_grids,m_numStates,ivGhost);
  for(int i=0;i<SpaceDim;i++){
      m_flux[i].define(m_grids,m_numFluxes,ivGhost);
  }
  //  cout << "AMRLevelMHD::regrid U done" << endl;
  m_Phi.define(m_grids,1,ivGhost);
  m_rhs.define(m_grids,1,ivGhost);
  
  m_whalf.define(m_grids);
  for (DataIterator dit = m_whalf.dataIterator(); dit.ok(); ++dit)
  {
    m_whalf[dit()].define(m_grids.get(dit),m_numPrims);
  }

  // Set up data structures
  levelSetup();

  // Interpolate from coarser level
  if (m_hasCoarser)
  {
    AMRLevelMHD* amrGodCoarserPtr = getCoarserLevel();
    m_fineInterp.interpToFine(m_UNew,amrGodCoarserPtr->m_UNew);
  }

  // Copy from old state
  m_UOld.copyTo(m_UOld.interval(),
                m_UNew,
                m_UNew.interval());

  m_UOld.define(m_grids,m_numStates,ivGhost);
}

// Initialize grids
void AMRLevelMHD::initialGrid(const Vector<Box>& a_newGrids)
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::initialGrid " << m_level << endl;
  }

  // Save original grids and load balance
  m_level_grids = a_newGrids;
  m_grids = loadBalance(a_newGrids);

  if (s_verbosity >= 4)
  {
    // Indicate/guarantee that the indexing below is only for reading
    // otherwise an error/assertion failure occurs
    const DisjointBoxLayout& constGrids = m_grids;

    pout() << "new grids: " << endl;
    for (LayoutIterator lit = constGrids.layoutIterator(); lit.ok(); ++lit)
    {
      pout() << constGrids[lit()] << endl;
    }
  }

  // Define old and new state data structures
  IntVect ivGhost = m_numGhost*IntVect::Unit;
  m_UNew.define(m_grids,m_numStates,ivGhost);
  m_UOld.define(m_grids,m_numStates,ivGhost);

  m_source.define(m_grids,m_numStates,ivGhost);
  for(int i=0;i<SpaceDim;i++){
      m_flux[i].define(m_grids,m_numFluxes,ivGhost);
  }
  m_Phi.define(m_grids,1,ivGhost);
  m_rhs.define(m_grids,1,ivGhost);
  m_whalf.define(m_grids);
  for (DataIterator dit = m_whalf.dataIterator(); dit.ok(); ++dit)
  {
    m_whalf[dit()].define(m_grids.get(dit),m_numPrims);
  }
  // Set up data structures
  levelSetup();
}

// Initialize data
void AMRLevelMHD::initialData()
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::initialData " << m_level << endl;
  }

  PhysIBC* physIBCPtr = m_gdnvPhysics->getPhysIBC();
  physIBCPtr->initialize(m_UNew);
}

// Things to do after initialization
void AMRLevelMHD::postInitialize()
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::postInitialize " << m_level << endl;
  }
}

#ifdef CH_USE_HDF5

// Write checkpoint header
void AMRLevelMHD::writeCheckpointHeader(HDF5Handle& a_handle) const
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::writeCheckpointHeader" << endl;
  }

  // Setup the number of components
  HDF5HeaderData header;
  header.m_int["num_components"] = m_numStates;

  // Setup the component names
  char compStr[30];
  for (int comp = 0; comp < m_numStates; ++comp)
  {
    sprintf(compStr,"component_%d",comp);
    header.m_string[compStr] = m_stateNames[comp];
  }

  // Write the header
  header.writeToFile(a_handle);

  if (s_verbosity >= 3)
  {
    pout() << header << endl;
  }
}

// Write checkpoint data for this level
void AMRLevelMHD::writeCheckpointLevel(HDF5Handle& a_handle) const
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::writeCheckpointLevel" << endl;
  }

  // Setup the level string
  char levelStr[20];
  sprintf(levelStr,"%d",m_level);
  const std::string label = std::string("level_") + levelStr;

  a_handle.setGroup(label);

  // Setup the level header information
  HDF5HeaderData header;

  header.m_int ["ref_ratio"]       = m_ref_ratio;
  header.m_int ["tag_buffer_size"] = m_tagBufferSize;
  header.m_real["dx"]              = m_dx;
  header.m_real["dt"]              = m_dt;
  header.m_real["time"]            = m_time;
  header.m_box ["prob_domain"]     = m_problem_domain.domainBox();

  // Setup the periodicity info
  D_TERM(if (m_problem_domain.isPeriodic(0))
         {
           header.m_int ["is_periodic_0"] = 1;
         }
         else
         {
           header.m_int ["is_periodic_0"] = 0;
         } ,

         if (m_problem_domain.isPeriodic(1))
         {
           header.m_int ["is_periodic_1"] = 1;
         }
         else
         {
           header.m_int ["is_periodic_1"] = 0;
         } ,

         if (m_problem_domain.isPeriodic(2))
         {
           header.m_int ["is_periodic_2"] = 1;
         }
         else
         {
           header.m_int ["is_periodic_2"] = 0;
         } );

  // Write the header for this level
  header.writeToFile(a_handle);

  if (s_verbosity >= 3)
  {
    pout() << header << endl;
  }

  // Write the data for this level
  write(a_handle,m_UNew.boxLayout());
  write(a_handle,m_UNew,"data");
}

// Read checkpoint header
void AMRLevelMHD::readCheckpointHeader(HDF5Handle& a_handle)
{
  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::readCheckpointHeader" << endl;
  }

  // Reader the header
  HDF5HeaderData header;
  header.readFromFile(a_handle);

  if (s_verbosity >= 3)
  {
    pout() << "hdf5 header data:" << endl;
    pout() << header << endl;
  }

  // Get the number of components
  if (header.m_int.find("num_components") == header.m_int.end())
  {
    MayDay::Error("AMRLevelMHD::readCheckpointHeader: checkpoint file does not have num_components");
  }

  int numStates = header.m_int["num_components"];
  if (numStates != m_numStates)
  {
    MayDay::Error("AMRLevelMHD::readCheckpointHeader: num_components in checkpoint file does not match solver");
  }

  // Get the component names
  std::string stateName;
  char compStr[60];
  for (int comp = 0; comp < m_numStates; ++comp)
  {
    sprintf(compStr,"component_%d",comp);
    if (header.m_string.find(compStr) == header.m_string.end())
    {
      MayDay::Error("AMRLevelMHD::readCheckpointHeader: checkpoint file does not have enough component names");
    }

    stateName = header.m_string[compStr];
    if (stateName != m_stateNames[comp])
    {
      MayDay::Error("AMRLevelMHD::readCheckpointHeader: state_name in checkpoint does not match solver");
    }
  }
}

// Read checkpoint data for this level
void AMRLevelMHD::readCheckpointLevel(HDF5Handle& a_handle)
{
  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::readCheckpointLevel" << endl;
  }

  // Setup the level string
  char levelStr[20];
  sprintf(levelStr,"%d",m_level);
  const std::string label = std::string("level_") + levelStr;

  // Read the header for this level
  a_handle.setGroup(label);

  HDF5HeaderData header;
  header.readFromFile(a_handle);

  if (s_verbosity >= 3)
  {
    pout() << "hdf5 header data:" << endl;
    pout() << header << endl;
  }

  // Get the refinement ratio
  if (header.m_int.find("ref_ratio") == header.m_int.end())
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain ref_ratio");
  }
  m_ref_ratio = header.m_int["ref_ratio"];

  if (s_verbosity >= 2)
  {
    pout() << "read ref_ratio = " << m_ref_ratio << endl;
  }

  // Get the tag buffer size
  if (header.m_int.find("tag_buffer_size") == header.m_int.end())
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain tag_buffer_size");
  }
  m_tagBufferSize = header.m_int["tag_buffer_size"];

  if (s_verbosity >= 2)
  {
    pout() << "read tag_buffer_size = " << m_tagBufferSize << endl;
  }

  // Get dx
  if (header.m_real.find("dx") == header.m_real.end())
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain dx");
  }
  m_dx = header.m_real["dx"];

  if (s_verbosity >= 2)
  {
    pout() << "read dx = " << m_dx << endl;
  }

  // Get dt
  if (header.m_real.find("dt") == header.m_real.end())
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain dt");
  }
  m_dt = header.m_real["dt"];

  if (s_verbosity >= 2)
  {
    pout() << "read dt = " << m_dt << endl;
  }

  // Get time
  if (header.m_real.find("time") == header.m_real.end())
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain time");
  }
  m_time = header.m_real["time"];

  if (s_verbosity >= 2)
  {
    pout() << "read time = " << m_time << endl;
  }

  // Get the problem domain
  if (header.m_box.find("prob_domain") == header.m_box.end())
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain prob_domain");
  }

  Box domainBox = header.m_box["prob_domain"];

  // Get the periodicity info -- this is more complicated than it really
  // needs to be in order to preserve backward compatibility
  bool isPeriodic[SpaceDim];
  D_TERM(if (!(header.m_int.find("is_periodic_0") == header.m_int.end()))
         {
           isPeriodic[0] =  (header.m_int["is_periodic_0"] == 1);
         }
         else
         {
           isPeriodic[0] = false;
         } ,

         if (!(header.m_int.find("is_periodic_1") == header.m_int.end()))
         {
           isPeriodic[1] =  (header.m_int["is_periodic_1"] == 1);
         }
         else
         {
           isPeriodic[1] = false;
         } ,

         if (!(header.m_int.find("is_periodic_2") == header.m_int.end()))
         {
           isPeriodic[2] =  (header.m_int["is_periodic_2"] == 1);
         }
         else
         {
           isPeriodic[2] = false;
         } );

  m_problem_domain = ProblemDomain(domainBox,isPeriodic);

  // Get the grids
  Vector<Box> grids;
  const int gridStatus = read(a_handle,grids);

  if (gridStatus != 0)
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain a Vector<Box>");
  }

  // Create level domain
  m_grids = loadBalance(grids);

  // Indicate/guarantee that the indexing below is only for reading
  // otherwise an error/assertion failure occurs
  const DisjointBoxLayout& constGrids = m_grids;

  LayoutIterator lit = constGrids.layoutIterator();
  for (lit.begin(); lit.ok(); ++lit)
  {
    const Box& b = constGrids[lit()];
    m_level_grids.push_back(b);
  }

  if (s_verbosity >= 4)
  {
    pout() << "read level domain: " << endl;
    LayoutIterator lit = m_grids.layoutIterator();
    for (lit.begin(); lit.ok(); ++lit)
    {
      const Box& b = m_grids[lit()];
      pout() << lit().intCode() << ": " << b << endl;
    }
    pout() << endl;
  }

  // Reshape state with new grids
  m_UNew.define(m_grids,m_numStates);
  const int dataStatus = read<FArrayBox>(a_handle,
                                         m_UNew,
                                         "data",
                                         m_grids);

  if (dataStatus != 0)
  {
    MayDay::Error("AMRLevelMHD::readCheckpointLevel: file does not contain state data");
  }
  //  m_UOld.define(m_grids,m_numStates);
  IntVect ivGhost = m_numGhost * IntVect::Unit;
  m_UOld.define(m_grids,m_numStates,ivGhost);
  m_source.define(m_grids,m_numStates,ivGhost);
  for(int i=0;i<SpaceDim;i++){
      m_flux[i].define(m_grids,m_numFluxes,ivGhost);
  }
  //  cout << "AMRLevelMHD::regrid U done" << endl;
  m_Phi.define(m_grids,1,ivGhost);
  m_rhs.define(m_grids,1,ivGhost);
  m_whalf.define(m_grids);
  for (DataIterator dit = m_whalf.dataIterator(); dit.ok(); ++dit)
  {
    m_whalf[dit()].define(m_grids.get(dit),m_numPrims);
  }
  // Set up data structures
  levelSetup();
}

// Write plotfile header
void AMRLevelMHD::writePlotHeader(HDF5Handle& a_handle) const
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::writePlotHeader" << endl;
  }

  // Setup the number of components
  HDF5HeaderData header;
  header.m_int["num_components"] = m_numStates;

  // Setup the component names
  char compStr[30];
  for (int comp = 0; comp < m_numStates; ++comp)
  {
    sprintf(compStr,"component_%d",comp);
    header.m_string[compStr] = m_plotNames[comp];
  }

  // Write the header
  header.writeToFile(a_handle);

  if (s_verbosity >= 3)
  {
    pout() << header << endl;
  }
}

// Write plotfile data for this level
void AMRLevelMHD::writePlotLevel(HDF5Handle& a_handle) const
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::writePlotLevel" << endl;
  }

  // Setup the level string
  char levelStr[20];
  sprintf(levelStr,"%d",m_level);
  const std::string label = std::string("level_") + levelStr;

  a_handle.setGroup(label);

  // Setup the level header information
  HDF5HeaderData header;

  header.m_int ["ref_ratio"]   = m_ref_ratio;
  header.m_real["dx"]          = m_dx;
  header.m_real["dt"]          = m_dt;
  header.m_real["time"]        = m_time;
  header.m_box ["prob_domain"] = m_problem_domain.domainBox();

  // Write the header for this level
  header.writeToFile(a_handle);

  if (s_verbosity >= 3)
  {
    pout() << header << endl;
  }
  // Plot
  LevelData<FArrayBox> UPlot(m_grids,m_numStates);
  // Beginning of loop through patches/grids.
  for (DataIterator dit = m_grids.dataIterator(); dit.ok(); ++dit)
  {
    // The current box
    Box curBox = m_grids.get(dit());
    FArrayBox& PlotU = UPlot[dit()];
    const FArrayBox& ConsU = m_UNew[dit()];
     FORT_CONSTOPRIMF(CHF_FRA(PlotU),
                   CHF_CONST_FRA(ConsU),
                   CHF_BOX(curBox));
  }
  
  // Write the data for this level
  //  write(a_handle,m_UNew.boxLayout());
  //  write(a_handle,m_UNew,"data");
    write(a_handle,UPlot.boxLayout());
    write(a_handle,UPlot,"data");
}

#endif

// Returns the dt computed earlier for this level
Real AMRLevelMHD::computeDt()
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::computeDt " << m_level << endl;
  }

  Real newDt;
  newDt = m_dtNew;

  return newDt;
}

// Compute dt using initial data
Real AMRLevelMHD::computeInitialDt()
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::computeInitialDt " << m_level << endl;
  }

  Real newDT = m_initial_dt_multiplier * m_dx / m_levelGodunov.getMaxWaveSpeed(m_UNew);

  return newDT;
}

const LevelData<FArrayBox>& AMRLevelMHD::getStateNew() const
{
  CH_assert(allDefined());

  return m_UNew;
}

const LevelData<FArrayBox>& AMRLevelMHD::getStateOld() const
{
  CH_assert(allDefined());

  return m_UOld;
}

bool AMRLevelMHD::allDefined() const
{
  return isDefined()     &&
         m_paramsDefined ;
}

// Create a load-balanced DisjointBoxLayout from a collection of Boxes
DisjointBoxLayout AMRLevelMHD::loadBalance(const Vector<Box>& a_grids)
{
  CH_assert(allDefined());

  // Load balance and create boxlayout
  Vector<int> procMap;
  //if (procID() == uniqueProc(SerialTask::compute))
  //{
  //  LoadBalance(procMap,a_grids);
  //}
  //broadcast(procMap,uniqueProc(SerialTask::compute));

  // appears to be faster for all procs to do the loadbalance (ndk)
  LoadBalance(procMap,a_grids);

  if (s_verbosity >= 4)
  {
    pout() << "AMRLevelMHD::loadBalance: procesor map: " << endl;

    for (int igrid = 0; igrid < a_grids.size(); ++igrid)
    {
      pout() << igrid << ": " << procMap[igrid] << "  " << endl;
    }

    pout() << endl;
  }

  DisjointBoxLayout dbl(a_grids,procMap,m_problem_domain);
  dbl.close();

  return dbl;
}

// Setup menagerie of data structures
void AMRLevelMHD::levelSetup()
{
  CH_assert(allDefined());

  if (s_verbosity >= 3)
  {
    pout() << "AMRLevelMHD::levelSetup " << m_level << endl;
  }

  AMRLevelMHD* amrGodCoarserPtr = getCoarserLevel();
  AMRLevelMHD* amrGodFinerPtr   = getFinerLevel();

  m_hasCoarser = (amrGodCoarserPtr != NULL);
  m_hasFiner   = (amrGodFinerPtr   != NULL);

  if (m_hasCoarser)
  {
    int nRefCrse = m_coarser_level_ptr->refRatio();

    m_coarseAverage.define(m_grids,
                           m_numStates,
                           nRefCrse);

    m_fineInterp.define(m_grids,
                        m_numStates,
                        nRefCrse,
                        m_problem_domain);

    const DisjointBoxLayout& coarserLevelDomain = amrGodCoarserPtr->m_grids;

    // Maintain levelGodunov
    m_levelGodunov.define(m_grids,
                          coarserLevelDomain,
                          m_problem_domain,
                          nRefCrse,
                          m_dx,
                          m_gdnvPhysics,
                          m_normalPredOrder,
                          m_useFourthOrderSlopes,
                          m_usePrimLimiting,
                          m_useCharLimiting,
                          m_useFlattening,
                          m_useArtificialViscosity,
                          m_artificialViscosity,
                          m_hasCoarser,
                          m_hasFiner);

    m_levelSolver.define(m_grids,
                         &coarserLevelDomain,
                         m_problem_domain,
                         (double)m_dx,
                         nRefCrse,
                         &m_poissonOp,
                         1);

    // This may look twisted but you have to do this this way because the
    // coarser levels get setup before the finer levels so, since a flux
    // register lives between this level and the next FINER level, the finer
    // level has to do the setup because it is the only one with the
    // information at the time of construction.

    // Maintain flux registers
    amrGodCoarserPtr->m_fluxRegister.define(m_grids,
                                            amrGodCoarserPtr->m_grids,
                                            m_problem_domain,
                                            amrGodCoarserPtr->m_ref_ratio,
                                            m_numStates);
    amrGodCoarserPtr->m_fluxRegister.setToZero();
  }
  else
  {
    m_levelGodunov.define(m_grids,
                          DisjointBoxLayout(),
                          m_problem_domain,
                          m_ref_ratio,
                          m_dx,
                          m_gdnvPhysics,
                          m_normalPredOrder,
                          m_useFourthOrderSlopes,
                          m_usePrimLimiting,
                          m_useCharLimiting,
                          m_useFlattening,
                          m_useArtificialViscosity,
                          m_artificialViscosity,
                          m_hasCoarser,
                          m_hasFiner);
    
    m_levelSolver.define(m_grids,
                         NULL,
                         m_problem_domain,
                         (double)m_dx,
                         m_ref_ratio,
                         &m_poissonOp,
                         1);
  } 
  bool solverVerbose = (s_verbosity > 3);
  m_levelSolver.setVerbose(solverVerbose);
  m_levelSolver.setMaxIter(100);
}

// Get the next coarser level
AMRLevelMHD* AMRLevelMHD::getCoarserLevel() const
{
  CH_assert(allDefined());

  AMRLevelMHD* amrGodCoarserPtr = NULL;

  if (m_coarser_level_ptr != NULL)
  {
    amrGodCoarserPtr = dynamic_cast<AMRLevelMHD*>(m_coarser_level_ptr);

    if (amrGodCoarserPtr == NULL)
    {
      MayDay::Error("AMRLevelMHD::getCoarserLevel: dynamic cast failed");
    }
  }

  return amrGodCoarserPtr;
}

// Get the next finer level
AMRLevelMHD* AMRLevelMHD::getFinerLevel() const
{
  CH_assert(allDefined());

  AMRLevelMHD* amrGodFinerPtr = NULL;

  if (m_finer_level_ptr != NULL)
  {
    amrGodFinerPtr = dynamic_cast<AMRLevelMHD*>(m_finer_level_ptr);

    if (amrGodFinerPtr == NULL)
    {
      MayDay::Error("AMRLevelMHD::getFinerLevel: dynamic cast failed");
    }
  }

  return amrGodFinerPtr;
}

void AMRLevelMHD::DivergenceB(LevelData<FArrayBox> &a_divb,
                              const LayoutData<FluxBox>&  a_WHalf)
{
  // Beginning of loop through patches/grids.
  for (DataIterator dit = m_grids.dataIterator(); dit.ok(); ++dit)
  {
    // The current box
    Box curBox = m_grids.get(dit());

    // The current grid of primitive variables extrapolated to faces and
    // half a time step
    const FluxBox& curWHalf = a_WHalf[dit()];

    // The current grid of div B
    FArrayBox& curDivB = a_divb[dit()];
    curDivB.setVal(0.0);

    for (int dir = 0; dir < SpaceDim; dir++)
      {
        const FArrayBox& WHalf = curWHalf[dir];
        FORT_DIVERGENCEBF(CHF_FRA(curDivB),
                          CHF_CONST_FRA(WHalf),
                          CHF_CONST_REAL(m_dx),
                          CHF_CONST_INT(dir),
                          CHF_BOX(curBox));
      }
  }
}

void AMRLevelMHD::ProjectBField(LayoutData<FluxBox>&  a_WHalf,
                                const LevelData<FArrayBox> &a_phi)
{
  // Beginning of loop through patches/grids.
  for (DataIterator dit = m_grids.dataIterator(); dit.ok(); ++dit)
  {
    // The current box
    Box curBox = m_grids.get(dit());

    // The current grid of primitive variables extrapolated to faces and
    // half a time step
    FluxBox& curWHalf = a_WHalf[dit()];

    // The current grid of div B
    const FArrayBox& curPhi = a_phi[dit()];

    // Boxes for face centered state 
    Box faceBox[SpaceDim];

//     // Boxes for cell centered state - used for the updatePrim() calls
//     Box ccBox[SpaceDim];

    for (int dir = 0; dir < SpaceDim; dir++)
    {
      faceBox[dir] = curBox;
      faceBox[dir].grow(dir,1);
      faceBox[dir] &= m_problem_domain;
      faceBox[dir].grow(dir,-1);
      faceBox[dir].surroundingNodes(dir);

      FArrayBox& WHalf = curWHalf[dir];
      FORT_PROJECTBFIELDF(CHF_FRA(WHalf),
                          CHF_CONST_FRA(curPhi),
                          CHF_CONST_REAL(m_dx),
                          CHF_CONST_INT(dir),
                          CHF_BOX(faceBox[dir]));
    }
  }
}

void AMRLevelMHD::FilterBFieldOld(LevelData<FArrayBox> &a_U)
{
  // Beginning of loop through patches/grids.
  for (DataIterator dit = m_grids.dataIterator(); dit.ok(); ++dit)
  {
    // The current box
    Box curBox = m_grids.get(dit());

    // The current grid of primitive variables extrapolated to faces and
    // half a time step
    FArrayBox& curU = a_U[dit()];
    FArrayBox BFld(curU.box(),1);

    FArrayBox divgradBT(curU.box(),3);
    divgradBT.setVal(0.0);
    // This loop computes d/dx_dir1 
    for (int dir1 = 0; dir1 < SpaceDim; dir1++)
      {
        int hasLo,hasHi;
        Box loBox,hiBox,centerBox,domain;
        Box inBox(curBox);
        inBox.grow(dir1,1);
        loHiCenter(loBox,hasLo,hiBox,hasHi,centerBox,domain,
                   inBox,m_problem_domain,dir1);
        //      Box gradBBox;
        //      gradBBox=curBox.grow(dir1,1);
        FArrayBox gradB(curU.box(),3);
        // This loop computes d/dx_dir2 of the B_dir1 component 
        for (int dir2 = 0; dir2 < SpaceDim; dir2++)
          {
            int hasLo,hasHi;
            Box loBox,hiBox,centerBox,domain;
            Box inBox(curBox);
            //      inBox.grow(dir1,2);
            inBox.grow(2);
            loHiCenter(loBox,hasLo,hiBox,hasHi,centerBox,domain,
                       inBox,m_problem_domain,dir2);
            FORT_GRADIENTBF(CHF_FRA(gradB),
                            CHF_CONST_FRA(BFld),
                            CHF_CONST_REAL(m_dx),
                            CHF_CONST_INT(dir1),
                            CHF_CONST_INT(dir2),
                            CHF_BOX(loBox),
                            CHF_CONST_INT(hasLo),
                            CHF_BOX(hiBox),
                            CHF_CONST_INT(hasHi),
                            CHF_BOX(centerBox));
          }
        FORT_DIVGRADBTF(CHF_FRA(divgradBT),
                        CHF_CONST_FRA(gradB),
                        CHF_CONST_REAL(m_dx),
                        CHF_CONST_INT(dir1),
                        CHF_BOX(loBox),
                        CHF_CONST_INT(hasLo),
                        CHF_BOX(hiBox),
                        CHF_CONST_INT(hasHi),
                        CHF_BOX(centerBox));

      }
    //Change B by someCoeff*div(gradB)^T
    FORT_BFILTERF(CHF_FRA(BFld),
                 CHF_CONST_FRA(divgradBT),
                 CHF_CONST_REAL(m_dx),
                 CHF_CONST_REAL(m_dt),
                 CHF_BOX(curBox));
    //Copy Bfld to curU
    curU.copy(BFld,0,4,3);
  }
        
}


//BFilter
void AMRLevelMHD::FilterBField(LevelData<FArrayBox> &a_U,
                               LevelFluxRegister&          a_finerFluxRegister,
                               LevelFluxRegister&          a_coarserFluxRegister)
{
  LevelData<FArrayBox> BFld(m_grids,SpaceDim,IntVect::Unit);
  LevelData<FArrayBox> GradDivBFld(m_grids,SpaceDim,IntVect::Zero);
  a_U.copyTo(Interval(4,4+SpaceDim-1),BFld,Interval(0,SpaceDim-1));

  // create physical BC's here
  DomainGhostBC domghostBC;
  {

    Interval BCcomps(0,SpaceDim-1);
    // for now, assume Dirichlet BC's
    for (int idir=0; idir<SpaceDim; idir++) 
      {
        // lo bc
        {
          //DirichletBC thisbc(idir, Side::Lo, BCcomps);
          HOExtrapBC thisbc(idir, Side::Lo, BCcomps);
          //NoOpBC thisbc(idir, Side::Lo, BCcomps);
          domghostBC.setBoxGhostBC(thisbc);
        }
            
        // hi bc
        {
          //DirichletBC thisbc(idir, Side::Hi, BCcomps);
          HOExtrapBC thisbc(idir, Side::Hi, BCcomps);
          //NoOpBC thisbc(idir, Side::Hi, BCcomps);
          domghostBC.setBoxGhostBC(thisbc);
        }
      }
  }
      
  // BCs for tangential gradients
  DomainGhostBC tanGradBC;
  {

    Interval BCcomps(0,(SpaceDim*SpaceDim-1));
    // for now, assume Dirichlet BC's
    for (int idir=0; idir<SpaceDim; idir++) 
      {
        // lo bc
        {
          //HOExtrapBC thisbc(idir, Side::Lo, BCcomps);
          ExtrapBC thisbc(idir, Side::Lo, BCcomps);
          //DirichletBC thisbc(idir, Side::Lo, BCcomps);
          //NoOpBC thisbc(idir, Side::Lo, BCcomps);
          tanGradBC.setBoxGhostBC(thisbc);
        }
      
        // hi bc
        {
          //DirichletBC thisbc(idir, Side::Hi, BCcomps);
          //HOExtrapBC thisbc(idir, Side::Hi, BCcomps);
          ExtrapBC thisbc(idir, Side::Hi, BCcomps);
          //scalarDirichletBC thisbc(idir, Side::Hi, BCcomps);
          //thisbc.setVal(1.0);
          //NoOpBC thisbc(idir, Side::Hi, BCcomps);
          tanGradBC.setBoxGhostBC(thisbc);
        }
      }
  }

  LevelData<FArrayBox>* nullLDFptr = NULL;
  GradDivOp graddivOp;
  graddivOp.setDomainGhostBC(domghostBC);
  graddivOp.setTanGradBC(tanGradBC);
  if(m_hasCoarser){
    AMRLevelMHD* coarserPtr = getCoarserLevel();
    const LevelData<FArrayBox>* coarserData = &coarserPtr->m_UNew;
    LevelData<FArrayBox> BFldCoarser(coarserPtr->m_UNew.disjointBoxLayout(),SpaceDim,IntVect::Unit);
    //    LevelData<FArrayBox> GradDivBFldCoarser(coarserPtr->m_UNew.disjointBoxLayout(),SpaceDim,IntVect::Unit);
    coarserData->copyTo(Interval(4,4+SpaceDim-1),BFldCoarser,Interval(0,SpaceDim-1));
    graddivOp.define(m_grids, &coarserPtr->m_UNew.disjointBoxLayout(), m_dx, m_ref_ratio, m_problem_domain, false, SpaceDim);
    graddivOp.applyOpI(GradDivBFld,BFld,&BFldCoarser);
  }
  else
    {
      graddivOp.define(m_grids, NULL, m_dx, m_ref_ratio, m_problem_domain, false, SpaceDim);
    graddivOp.applyOpI(GradDivBFld,BFld,nullLDFptr);
    }



  // Beginning of loop through patches/grids.
  for (DataIterator dit = m_grids.dataIterator(); dit.ok(); ++dit)
  {
    // The current box
    Box curBox = m_grids.get(dit());

    // The current grid of primitive variables extrapolated to faces and
    // half a time step
    FArrayBox& curU = a_U[dit()];
    FArrayBox& curBFld = BFld[dit()];
    FArrayBox& divgradBT = GradDivBFld[dit()];

//     FluxBox filterBFlux;
//     filterBFlux.resize(curBox,3);
//     filterBFlux.setVal(0.0);

//     FArrayBox divgradBT(curU.box(),3);
//     divgradBT.setVal(0.0);
//     //
//     for (int dir1 = 0; dir1 < SpaceDim; dir1++)
//       {
//      FArrayBox& filterBFluxDir=filterBFlux[dir1];
//      int hasLo,hasHi;
//      Box loBox,hiBox,centerBox,domain;
//      Box inBox(curBox);
//      inBox.grow(dir1,1);
//      loHiCenterFace(loBox,hasLo,hiBox,hasHi,centerBox,domain,
//                 inBox,m_problem_domain,dir1);
//      pout() << "FILTER BOXES Dir" <<  dir1 << endl;
//      pout() << "FILTER BOXES curBox" <<  curBox << endl;
//      pout() << "FILTER BOXES filterBFluxDir" <<  filterBFluxDir.box() << endl;
//      if(hasLo)
//      pout() << "FILTER BOXES loBox" <<  loBox << endl;
//      if(hasHi)
//      pout() << "FILTER BOXES hiBox" <<  hiBox << endl;
//      pout() << "FILTER BOXES centerBox" <<  centerBox << endl;
//      int dir2=dir1+(dir1+1)%CH_SPACEDIM;
//      //      Box gradBBox;
//      //      gradBBox=curBox.grow(dir1,1);
//      FArrayBox gradB(curU.box(),3);
//      // This loop computes d/dx_dir2 of the B_dir1 component 
// //   for (int dir2 = 0; dir2 < SpaceDim; dir2++)
// //     {
// //       int hasLo,hasHi;
// //       Box loBox,hiBox,centerBox,domain;
// //       Box inBox(curBox);
// //       //      inBox.grow(dir1,2);
// //               inBox.grow(2);
// //       loHiCenter(loBox,hasLo,hiBox,hasHi,centerBox,domain,
// //                  inBox,m_problem_domain,dir2);
// //       FORT_GRADIENTBF(CHF_FRA(gradB),
// //                       CHF_CONST_FRA(BFld),
// //                       CHF_CONST_REAL(m_dx),
// //                       CHF_CONST_INT(dir1),
// //                       CHF_CONST_INT(dir2),
// //                       CHF_BOX(loBox),
// //                       CHF_CONST_INT(hasLo),
// //                       CHF_BOX(hiBox),
// //                       CHF_CONST_INT(hasHi),
// //                       CHF_BOX(centerBox));
// //     }
// //   FORT_DIVGRADBTF(CHF_FRA(divgradBT),
// //                   CHF_CONST_FRA(gradB),
// //                   CHF_CONST_REAL(m_dx),
// //                   CHF_CONST_INT(dir1),
// //                   CHF_BOX(loBox),
// //                   CHF_CONST_INT(hasLo),
// //                   CHF_BOX(hiBox),
// //                   CHF_CONST_INT(hasHi),
// //                   CHF_BOX(centerBox));

//       }
     FORT_BFILTERF(CHF_FRA(curBFld),
                 CHF_CONST_FRA(divgradBT),
                 CHF_CONST_REAL(m_dx),
                 CHF_CONST_REAL(m_dt),
                 CHF_BOX(curBox));
     //Copy Bfld to curU
     curU.copy(curBFld,0,4,SpaceDim);
  }
        
}
