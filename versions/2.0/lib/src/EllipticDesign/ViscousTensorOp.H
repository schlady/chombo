#ifdef CH_LANG_CC
/*
 *      _______              __
 *     / ___/ /  ___  __ _  / /  ___
 *    / /__/ _ \/ _ \/  V \/ _ \/ _ \
 *    \___/_//_/\___/_/_/_/_.__/\___/
 *    Please refer to Copyright.txt, in Chombo's root directory.
 */
#endif

// dtg 2007

#ifndef _ViscousTensor_H_
#define _ViscousTensor_H_

#include "AMRMultiGrid.H"
#include "REAL.H"
#include "Box.H"
#include "LevelDataOps.H"
#include "BCFunc.H"
#include "FArrayBox.H"
#include "FluxBox.H"
#include "CFIVS.H"
#include "TensorCFInterp.H"
#include "CoarseAverage.H"
#include "LevelFluxRegister.H"
#include "NamespaceHeader.H"

///
/**
   operator is alpha I + (divF) = alpha I + beta*div(eta(grad B - grad B^T) + alpha I div B )
   beta and eta are incorporated into the flux F
*/
class ViscousTensorOp : public AMRLevelOp<LevelData<FArrayBox> >
{
public:

  ///
  /**
   */
  virtual ~ViscousTensorOp() { ; }


  ///
  /** 
   */
  ViscousTensorOp(const DisjointBoxLayout&                    a_grids,
                  const DisjointBoxLayout&                    a_gridsFine,
                  const DisjointBoxLayout&                    a_gridsCoar,
                  const RefCountedPtr<LevelData<FluxBox> >&   a_eta,
                  const RefCountedPtr<LevelData<FluxBox> >&   a_lambda,
                  Real                                        a_alpha, 
                  const RefCountedPtr<LevelData<FArrayBox> >& a_beta,
                  int                                         a_refToFine,
                  int                                         a_refToCoar,
                  const ProblemDomain&                        a_domain,
                  const Real&                                 a_dxLevel,
                  const Real&                                 a_dxCoar,
                  BCFunc                                      a_bc);             

  virtual void residual(  LevelData<FArrayBox>& a_lhs,
                          const LevelData<FArrayBox>& a_phi,
                          const LevelData<FArrayBox>& a_rhs,
                          bool a_homogeneous = false);

  virtual void preCond(   LevelData<FArrayBox>& a_correction,
                          const LevelData<FArrayBox>& a_residual);

  virtual void applyOp(   LevelData<FArrayBox>& a_lhs,
                          const LevelData<FArrayBox>& a_phi,
                          bool a_homogeneous = false);
  virtual void create(    LevelData<FArrayBox>& a_lhs,
                          const LevelData<FArrayBox>& a_rhs);
  virtual void createCoarsened(    LevelData<FArrayBox>& a_lhs,
                                   const LevelData<FArrayBox>& a_rhs,
                                   const int& a_refRat);

  void reflux(const LevelData<FArrayBox>& a_phiFine,
              const LevelData<FArrayBox>& a_phi,
              LevelData<FArrayBox>& residual,
              AMRLevelOp<LevelData<FArrayBox> >* a_finerOp);

  void getFlux(FArrayBox&       a_flux,
               const FArrayBox& a_data,
               const FArrayBox& a_gradData,
               const FArrayBox& a_etaFace,
               const FArrayBox& a_lambdaFace,
               const Box&       a_facebox,
               int              a_dir,
               int ref = 1);

  /// utility function which computes operator after all bc's have been set
  void computeOperatorNoBCs(LevelData<FArrayBox>& a_lhs,
                            const LevelData<FArrayBox>& a_phi);

  virtual void assign(    LevelData<FArrayBox>& a_lhs,
                          const LevelData<FArrayBox>& a_rhs) ;
  virtual Real dotProduct(const LevelData<FArrayBox>& a_1,
                          const LevelData<FArrayBox>& a_2) ;
  virtual void incr( LevelData<FArrayBox>& a_lhs,
                     const LevelData<FArrayBox>& a_x,
                     Real a_scale) ;
  virtual void axby( LevelData<FArrayBox>& a_lhs, 
                     const LevelData<FArrayBox>& a_x,
                     const LevelData<FArrayBox>& a_y,
                     Real a, Real b) ;

  virtual void scale(LevelData<FArrayBox>& a_lhs, const Real& a_scale) ;

  virtual Real norm(const LevelData<FArrayBox>& a_x, int a_ord);

  virtual void setToZero( LevelData<FArrayBox>& a_x);
  /*@}*/

  /**
     \name MGLevelOp functions */
  /*@{*/

  virtual void relax(LevelData<FArrayBox>& a_e,
                     const LevelData<FArrayBox>& a_residual,
                     int iterations);

  virtual void createCoarser(LevelData<FArrayBox>& a_coarse,
                             const LevelData<FArrayBox>& a_fine,
                             bool ghosted);
  /**
     calculate restricted residual
     a_resCoarse[2h] = I[h->2h] (rhsFine[h] - L[h](phiFine[h])
  */
  virtual void restrictResidual(LevelData<FArrayBox>& a_resCoarse,
                                LevelData<FArrayBox>& a_phiFine,
                                const LevelData<FArrayBox>& a_rhsFine);

  /**
     correct the fine solution based on coarse correction
     a_phiThisLevel += I[2h->h](a_correctCoarse)
  */
  virtual void prolongIncrement(LevelData<FArrayBox>& a_phiThisLevel,
                                const LevelData<FArrayBox>& a_correctCoarse);

  /*@}*/

  /**
     \name AMRLevelOp functions */
  /*@{*/

  /** returns 1 when there are no coarser AMRLevelOp objects */
  virtual int refToCoarser(){ return m_refToCoar; }

  /** a_residual = a_rhs - L(a_phi, a_phiFine, a_phiCoarse) */
  virtual void AMRResidual(LevelData<FArrayBox>& a_residual,
                           const LevelData<FArrayBox>& a_phiFine,
                           const LevelData<FArrayBox>& a_phi,
                           const LevelData<FArrayBox>& a_phiCoarse,
                           const LevelData<FArrayBox>& a_rhs,
                           bool a_homogeneousPhysBC,
                           AMRLevelOp<LevelData<FArrayBox> >* a_finerOp);

  /** residual assuming no more coarser AMR levels */

  virtual void AMRResidualNC(LevelData<FArrayBox>& a_residual,
                             const LevelData<FArrayBox>& a_phiFine,
                             const LevelData<FArrayBox>& a_phi,
                             const LevelData<FArrayBox>& a_rhs,
                             bool a_homogeneousPhysBC,
                             AMRLevelOp<LevelData<FArrayBox> >* a_finerOp);

  /** a_residual = a_rhs - L(a_phi, a_phiCoarse)  */
  virtual void AMRResidualNF(LevelData<FArrayBox>& a_residual,
                             const LevelData<FArrayBox>& a_phi,
                             const LevelData<FArrayBox>& a_phiCoarse,
                             const LevelData<FArrayBox>& a_rhs,
                             bool a_homogeneousPhysBC);

  /** a_resCoarse = I[h-2h]( a_residual - L(a_correction, a_coarseCorrection))
      it is assumed that a_resCoarse has already been filled in with the coarse
      version of AMRResidualNF and that this operation is free to overwrite
      in the overlap regions.
  */

  virtual void AMRRestrict(LevelData<FArrayBox>& a_resCoarse,
                           const LevelData<FArrayBox>& a_residual,
                           const LevelData<FArrayBox>& a_correction,
                           const LevelData<FArrayBox>& a_coarseCorrection);

  /** a_correction += I[h->h](a_coarseCorrection) */
  virtual void AMRProlong(LevelData<FArrayBox>& a_correction,
                          const LevelData<FArrayBox>& a_coarseCorrection);

  /** a_residual = a_residual - L(a_correction, a_coarseCorrection) */
  virtual void AMRUpdateResidual(LevelData<FArrayBox>& a_residual,
                                 const LevelData<FArrayBox>& a_correction,
                                 const LevelData<FArrayBox>& a_coarseCorrection);

  ///
  /**
     compute norm over all cells on coarse not covered by finer
  */
  virtual Real AMRNorm(const LevelData<FArrayBox>& a_coarseResid,
                       const LevelData<FArrayBox>& a_fineResid,
                       const int&                  a_refRat,
                       const int&                  a_ord);

  void homogeneousCFInterp(LevelData<FArrayBox>& a_phif);
  ///
  /**
     does homogeneous coarse/fine interpolation for phi
  */
  void
  homogeneousCFInterpPhi(LevelData<FArrayBox>& a_phif, 
                         const DataIndex& a_datInd, 
                         int a_idir, 
                         Side::LoHiSide a_hiorlo);

  ///
  /** does homogeneous coarse/fine interpolation for tangential gradient
      (needs phi ghost cells to be filled in first, so should call 
      homogeneousCFInterpPhi first)
  */
  void homogeneousCFInterpTanGrad(LevelData<FArrayBox>& a_tanGrad,
                                  const LevelData<FArrayBox>& a_phi,
                                  const DataIndex& a_datInd,
                                  int a_idir,
                                  Side::LoHiSide a_hiorlo);

  void interpOnIVSHomo(LevelData<FArrayBox>& a_phif,
                       const DataIndex& a_datInd,
                       const int a_idir,
                       const Side::LoHiSide a_hiorlo,
                       const IntVectSet& a_interpIVS);
  ///
  /**
     Apply the AMR operator, including coarse-fine matching
  */
  void AMROperator(      LevelData<FArrayBox>& a_LofPhi, 
                         const LevelData<FArrayBox>& a_phiFine, 
                         const LevelData<FArrayBox>& a_phi,
                         const LevelData<FArrayBox>& a_phiCoarse, 
                         bool a_homogeneousDomBC,
                         AMRLevelOp<LevelData<FArrayBox> >*  a_finerOp);

  void AMROperatorNF(      LevelData<FArrayBox>& a_LofPhi, 
                           const LevelData<FArrayBox>& a_phi, 
                           const LevelData<FArrayBox>& a_phiCoarse,
                           bool a_homogeneousBC);

  virtual void AMROperatorNC(      LevelData<FArrayBox>& a_LofPhi, 
                                   const LevelData<FArrayBox>& a_phiFine, 
                                   const LevelData<FArrayBox>& a_phi,
                                   bool a_homogeneousBC,
                                   AMRLevelOp<LevelData<FArrayBox> >* a_finerOp);

  void cellGrad(FArrayBox&             a_gradPhi,
                const  FArrayBox&      a_phi,
                const Box&             a_grid);

  void cfinterp(const LevelData<FArrayBox>& a_phiFine,
                const LevelData<FArrayBox>& a_phiCoarse);

  void fillGrad(const LevelData<FArrayBox>& a_phiFine);

  void loHiCenterFace(Box&                 a_loBox,
                      int&                 a_hasLo,
                      Box&                 a_hiBox,
                      int&                 a_hasHi,
                      Box&                 a_centerBox,
                      const ProblemDomain& a_eblg,
                      const Box&           a_inBox,
                      const int&           a_dir);
  
  void getFaceDivAndGrad(FArrayBox&             a_faceDiv,
                         FArrayBox&             a_faceGrad,
                         const FArrayBox&       a_data,
                         const FArrayBox&       a_gradData,
                         const ProblemDomain&   a_domain,
                         const Box&             a_faceBox,
                         const int&             a_faceDir,
                         const Real             a_dx);

  ///take cell centered divergence of the inputs.
  /** 
      not part of the operator evaluation.  this is to 
      test whether the divergence of b converges at h^2 
      as b does if b is analytically divergence-free.
  */
  void divergenceCC(LevelData<FArrayBox>&       a_div, 
                    const LevelData<FArrayBox>& a_phi,
                    const LevelData<FArrayBox>* a_phiC);
protected:
  void defineLambda();
  RefCountedPtr<LevelData<FluxBox> >         m_eta;             
  RefCountedPtr<LevelData<FluxBox> >         m_lambda;             
  RefCountedPtr<LevelData<FArrayBox> >       m_beta;
  Real                                       m_alpha;           
  int                                        m_refToCoar;       
  int                                        m_refToFine;       
  BCFunc                                     m_bc;             
  
  //b is a scalar in 2d, vector in 3d
  int m_ncomp;
  Real                    m_dx;
  Real                    m_dxCrse;
  ProblemDomain           m_domain;
  //relaxation coef
  LevelData<FArrayBox>    m_relaxCoef;
  LevelData<FArrayBox>    m_grad;
  LevelDataOps<FArrayBox> m_levelOps;
  Copier                  m_exchangeCopier;
  TensorCFInterp          m_interpWithCoarser;

  LayoutData<CFIVS> m_loCFIVS[SpaceDim];
  LayoutData<CFIVS> m_hiCFIVS[SpaceDim];
  // will need these to do tangential gradient computations
  LayoutData<TensorFineStencilSet> m_hiTanStencilSets[SpaceDim];
  LayoutData<TensorFineStencilSet> m_loTanStencilSets[SpaceDim];

  bool 
  nextColor(IntVect&       a_color,
            const IntVect& a_limit);
private:
  ///weak construction is bad
  ViscousTensorOp()
  {
    MayDay::Error("invalid operator");
  }
  //copy constructor and operator= disallowed for all the usual reasons
  ViscousTensorOp(const ViscousTensorOp& a_opin)
  {
    MayDay::Error("invalid operator");
  }
  void operator=(const ViscousTensorOp& a_opin)
  {
    MayDay::Error("invalid operator");
  }
};

///
/**
   Factory to create ViscousTensorOps
*/
class ViscousTensorOpFactory: public AMRLevelOpFactory<LevelData<FArrayBox> >
{
public:
  virtual ~ViscousTensorOpFactory(){;}

  ViscousTensorOpFactory(const Vector<DisjointBoxLayout>&                     a_grids,
                         const Vector<RefCountedPtr<LevelData<FluxBox> > >&   a_eta,
                         const Vector<RefCountedPtr<LevelData<FluxBox> > >&   a_lambda,
                         Real                                                 a_alpha, 
                         const Vector<RefCountedPtr<LevelData<FArrayBox> > >& a_beta,
                         const Vector<int>&                                   a_refRatios,
                         const ProblemDomain&                                 a_domainCoar,
                         const Real&                                          a_dxCoar,
                         BCFunc                                               a_bc);

  ///
  virtual ViscousTensorOp*
  MGnewOp(const ProblemDomain& a_FineindexSpace,
          int depth,
          bool homoOnly = true);

  ///
  virtual  ViscousTensorOp* AMRnewOp(const ProblemDomain& a_indexSpace);

  ///
  virtual int refToFiner(const ProblemDomain&) const;

private:
  Vector<RefCountedPtr<LevelData<FluxBox> > >    m_eta;
  Vector<RefCountedPtr<LevelData<FluxBox> > >    m_lambda;
  Vector<RefCountedPtr<LevelData<FArrayBox> > >  m_beta;
  LevelData<FArrayBox>                           m_phic;
  Vector<ProblemDomain>                          m_domains;
  Vector<DisjointBoxLayout>                      m_boxes;
  Vector<Real>                                   m_dx;
  // refinement to next coarser level
  Vector<int>                                    m_refRatios; 
  BCFunc  m_bc;
  Real m_alpha;
  
  ///weak construction is bad
  ViscousTensorOpFactory()
  {
    MayDay::Error("invalid operator");
  }
  //copy constructor and operator= disallowed for all the usual reasons
  ViscousTensorOpFactory(const ViscousTensorOpFactory& a_opin)
  {
    MayDay::Error("invalid operator");
  }

  void operator=(const ViscousTensorOpFactory& a_opin)
  {
    MayDay::Error("invalid operator");
  }
};

extern void  
coarsenStuff(LevelData<FluxBox> &          a_etaCoar, 
             LevelData<FluxBox> &          a_lambdaCoar, 
             LevelData<FArrayBox> &        a_betaCoar, 
             const LevelData<FluxBox> &    a_etaFine, 
             const LevelData<FluxBox> &    a_lambdaFine, 
             const LevelData<FArrayBox>&   a_betaFine, 
             const int &                   a_refToDepth);

#include "NamespaceFooter.H"
#endif
