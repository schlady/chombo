#ifdef CH_LANG_CC
/*
*      _______              __
*     / ___/ /  ___  __ _  / /  ___
*    / /__/ _ \/ _ \/  V \/ _ \/ _ \
*    \___/_//_/\___/_/_/_/_.__/\___/
*    Please refer to Copyright.txt, in Chombo's root directory.
*/
#endif

#include "AMRPoissonOp.H"
#include "FORT_PROTO.H"
#include "AMRPoissonOpF_F.H"
#include "BoxIterator.H"
#include "AverageF_F.H"
#include "InterpF_F.H"
#include "LayoutIterator.H"
#include "FineInterp.H"
#include "Misc.H"

#include "NamespaceHeader.H"

void AMRPoissonOp::
createCoarsened(LevelData<FArrayBox>&       a_lhs,
                const LevelData<FArrayBox>& a_rhs,
                const int &                 a_refRat)
{
  int ncomp = a_rhs.nComp();
  IntVect ghostVect = a_rhs.ghostVect();

  DisjointBoxLayout dbl = a_rhs.disjointBoxLayout();
  CH_assert(dbl.coarsenable(a_refRat));

  //fill ebislayout
  DisjointBoxLayout dblCoarsenedFine;
  coarsen(dblCoarsenedFine, dbl, a_refRat);

  a_lhs.define(dblCoarsenedFine, ncomp, a_rhs.ghostVect());
}
/** full define function for AMRLevelOp with both coarser and finer levels */
void AMRPoissonOp::define(const DisjointBoxLayout& a_grids,
                          const DisjointBoxLayout& a_gridsFiner,
                          const DisjointBoxLayout& a_gridsCoarser,
                          const Real&              a_dxLevel,
                          int                      a_refRatio,
                          int                      a_refRatioFiner,
                          const ProblemDomain&     a_domain,
                          BCHolder                 a_bc,
                          const Copier&            a_exchange,
                          const CFRegion&          a_cfivs)
{
  this->define(a_grids, a_gridsCoarser, a_dxLevel, a_refRatio, a_domain, a_bc, 
               a_exchange, a_cfivs);
  m_refToFiner = a_refRatioFiner;
}

  /** full define function for AMRLevelOp<LevelData<FArrayBox> > with finer levels, but no coarser */
void AMRPoissonOp::define(const DisjointBoxLayout& a_grids,
                          const DisjointBoxLayout& a_gridsFiner,
                          const Real&              a_dxLevel,
                          int                      a_refRatio, // dummy argument
                          int                      a_refRatioFiner,
                          const ProblemDomain&     a_domain,
                          BCHolder                   a_bc,
                          const Copier&            a_exchange,
                          const CFRegion&          a_cfivs)
{
  CH_assert(a_refRatio == 1);
  this->define(a_grids, a_dxLevel, a_domain, a_bc, a_exchange, a_cfivs); //calls the MG version of define
  m_refToFiner = a_refRatioFiner;
}

void AMRPoissonOp::define(const DisjointBoxLayout& a_grids,
                          const DisjointBoxLayout& a_coarse,
                          const Real&              a_dxLevel,
                          int                      a_refRatio,
                          const ProblemDomain&     a_domain,
                          BCHolder                   a_bc,
                          const Copier&            a_exchange,
                          const CFRegion&          a_cfivs)
{
  this->define(a_grids, a_dxLevel, a_domain, a_bc, a_exchange, a_cfivs); 
  m_refToCoarser = a_refRatio;

  m_dxCrse = a_refRatio*a_dxLevel;
  m_refToFiner = 1;

  m_interpWithCoarser.define(a_grids, &a_coarse, a_dxLevel,
                             m_refToCoarser, 1, m_domain);  
}

void AMRPoissonOp::define(const DisjointBoxLayout& a_grids,
                          const Real&              a_dx,
                          const ProblemDomain&     a_domain, 
                          BCHolder                   a_bc,
                          const Copier&            a_exchange, 
                          const CFRegion&          a_cfivs)
{
  m_bc     = a_bc;
  m_domain = a_domain;
  m_dx     = a_dx;
  m_dxCrse = 2*a_dx;
  m_refToCoarser = 2; // redefined in AMRLevelOp<LevelData<FArrayBox> >::define virtual function.
  m_refToFiner   = 2;

  //these get set again after define is called
  m_alpha = 0.0;
  m_beta  = 1.0;
    

  m_exchangeCopier = a_exchange;
  //m_exchangeCopier.define(a_grids, a_grids, IntVect::Unit, true);
  //m_exchangeCopier.trimEdges(a_grids, IntVect::Unit);

  m_cfivs = a_cfivs;


}
void AMRPoissonOp::define(const DisjointBoxLayout& a_grids,
                          const Real&              a_dx,
                          const ProblemDomain&     a_domain, 
                          BCHolder                   a_bc)
{
  
  Copier copier;
  copier.define(a_grids, a_grids, IntVect::Unit, true);
  CFRegion cfivs(a_grids, a_domain);
  this->define(a_grids, a_dx, a_domain, a_bc, copier, cfivs);
}
// void AMRPoissonOp::residual(LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_phi,
//                             const LevelData<FArrayBox>& a_rhs, bool a_homogeneous)
// {
//   applyOp(a_lhs, a_phi, a_homogeneous);
//   incr(a_lhs, a_rhs, -1);
//   scale(a_lhs, -1.0);
// }

/**************************/
// this preconditioner first initializes phihat to (IA)phihat = rhshat
// (diagonalization of L -- A is the matrix version of L)
// then smooths with a couple of passes of levelGSRB
void AMRPoissonOp::preCond(LevelData<FArrayBox>& a_phi, const LevelData<FArrayBox>& a_rhs)
{
  // diagonal term of this operator is (alpha + 4*beta/h/h) in 2D, 
  // (alpha + 6*beta/h/h) in 3D,
  // so inverse of this is our initial multiplier
  Real mult = -m_dx*m_dx/(m_alpha + 2.0*m_beta*SpaceDim);
  Interval comps = a_phi.interval();
  CH_assert(a_phi.nComp() == a_rhs.nComp());

  // don't need to use a Copier -- plain copy will do
  DataIterator dit = a_phi.dataIterator();
  for (dit.begin(); dit.ok(); ++dit)
    {
      a_phi[dit].copy(a_rhs[dit]);

      a_phi[dit] *= mult;
    }

  relax(a_phi, a_rhs, 2);
}

void AMRPoissonOp::residual(LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_phi,
                            const LevelData<FArrayBox>& a_rhs, bool a_homogeneous)
{
  CH_TIME("AMRPoissonOp::residual");

  homogeneousCFInterp((LevelData<FArrayBox>&)a_phi);
  residualI(a_lhs,a_phi,a_rhs,a_homogeneous);
}

void AMRPoissonOp::residualI(LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_phi,
                             const LevelData<FArrayBox>& a_rhs, bool a_homogeneous)
{

  LevelData<FArrayBox>& phi = (LevelData<FArrayBox>&)a_phi;
  Real dx = m_dx;
  const DisjointBoxLayout& dbl = a_lhs.disjointBoxLayout();
  DataIterator dit = phi.dataIterator();
  for (dit.begin(); dit.ok(); ++dit)
    {
      m_bc(phi[dit], dbl[dit()],m_domain, dx, a_homogeneous);
    }
  
  phi.exchange(phi.interval(), m_exchangeCopier);

  for (dit.begin(); dit.ok(); ++dit)
    {
      const Box& region = dbl[dit()];
      FORT_OPERATORLAPRES(CHF_FRA(a_lhs[dit]),
                          CHF_CONST_FRA(phi[dit]),
                          CHF_CONST_FRA(a_rhs[dit]),
                          CHF_BOX(region),
                          CHF_CONST_REAL(dx),
                          CHF_CONST_REAL(m_alpha),
                          CHF_CONST_REAL(m_beta));
    }
}

void AMRPoissonOp::applyOp(LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_phi,
                           bool a_homogeneous )
{
  CH_TIME("AMRPoissonOp::applyOp");

  homogeneousCFInterp((LevelData<FArrayBox>&)a_phi);
  applyOpI(a_lhs,a_phi,a_homogeneous);
}

void AMRPoissonOp::applyOpI(LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_phi,
                           bool a_homogeneous )
{

  LevelData<FArrayBox>& phi = (LevelData<FArrayBox>&)a_phi;
  Real dx = m_dx;
  const DisjointBoxLayout& dbl = a_lhs.disjointBoxLayout();
  DataIterator dit = phi.dataIterator();
  for (dit.begin(); dit.ok(); ++dit)
    {
      m_bc(phi[dit], dbl[dit()],m_domain, dx, a_homogeneous);
    }

  phi.exchange(phi.interval(), m_exchangeCopier);

  for (dit.begin(); dit.ok(); ++dit)
    {
      const Box& region = dbl[dit()];
      FORT_OPERATORLAP(CHF_FRA(a_lhs[dit]),
                       CHF_CONST_FRA(phi[dit]),
                       CHF_BOX(region),
                       CHF_CONST_REAL(dx),
                       CHF_CONST_REAL(m_alpha),
                       CHF_CONST_REAL(m_beta));
    }
}

///compute norm over all cells on coarse not covered by finer
Real
AMRPoissonOp::AMRNorm(const LevelData<FArrayBox>& a_coarResid,
                      const LevelData<FArrayBox>& a_fineResid,
                      const int& a_refRat,
                      const int& a_ord)

{
  CH_TIME("AMRPoissonOp::AMRNorm");
  const DisjointBoxLayout& coarGrids = a_coarResid.disjointBoxLayout();
  const DisjointBoxLayout& fineGrids = a_fineResid.disjointBoxLayout();

  //create temp and zero out under finer grids
  LevelData<FArrayBox> coarTemp;
  m_levelOps.create(coarTemp, a_coarResid);
  m_levelOps.assign(coarTemp, a_coarResid);
  int ncomp = coarTemp.nComp();
  for(DataIterator dit = coarGrids.dataIterator(); dit.ok(); ++dit)
    {
      FArrayBox& coarTempFAB = coarTemp[dit()];
      LayoutIterator litFine = fineGrids.layoutIterator();
      for (litFine.reset(); litFine.ok(); ++litFine)
        {
          Box overlayBox = coarTempFAB.box();
          Box coarsenedGrid = coarsen(fineGrids[litFine()], a_refRat);

          overlayBox &= coarsenedGrid;

          if (!overlayBox.isEmpty())
            {
              coarTempFAB.setVal(0.0,overlayBox,0, ncomp);
            }
        }
    }
  //return norm of temp
  return norm(coarTemp, a_ord);
}
void AMRPoissonOp::create(    LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_rhs)
{
  m_levelOps.create(a_lhs, a_rhs);
}

void AMRPoissonOp::assign(    LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_rhs)
{
  CH_TIME("AMRPoissonOp::assign");
  m_levelOps.assign(a_lhs, a_rhs);
}

void AMRPoissonOp::assignLocal( LevelData<FArrayBox>& a_lhs, const  LevelData<FArrayBox>& a_rhs)
{
  CH_TIME("AMRPoissonOp::assignLocal");
  for(DataIterator dit= a_lhs.dataIterator(); dit.ok(); ++dit)
    {
      a_lhs[dit].copy(a_rhs[dit]);
    }
}
void AMRPoissonOp::buildCopier(Copier& a_copier, 
                               const  LevelData<FArrayBox>& a_lhs, 
                               const  LevelData<FArrayBox>& a_rhs)
{
  const DisjointBoxLayout& dbl=a_lhs.disjointBoxLayout();

  a_copier.define(a_rhs.disjointBoxLayout(), dbl, IntVect::Zero);

}
void AMRPoissonOp::assignCopier( LevelData<FArrayBox>& a_lhs, 
                                 const LevelData<FArrayBox>& a_rhs, 
                                 const Copier& a_copier)
{
  CH_TIME("AMRPoissonOp::assignCopier");
  a_rhs.copyTo(a_rhs.interval(), a_lhs, a_lhs.interval(), a_copier);
}

void AMRPoissonOp::zeroCovered( LevelData<FArrayBox>& a_lhs, 
                                LevelData<FArrayBox>& a_rhs, 
                                const Copier& a_copier)
{
  CH_TIME("AMRPoissonOp::zeroCovered");
  m_levelOps.copyToZero(a_lhs, a_copier);
}

Real AMRPoissonOp::dotProduct(const LevelData<FArrayBox>& a_1, const LevelData<FArrayBox>& a_2)
{
  CH_TIME("AMRPoissonOp::dotProduct");
  return m_levelOps.dotProduct(a_1, a_2);
}
void AMRPoissonOp::incr( LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_x, Real a_scale)
{
  m_levelOps.incr(a_lhs, a_x, a_scale);
}
void AMRPoissonOp::axby( LevelData<FArrayBox>& a_lhs, const LevelData<FArrayBox>& a_x,
                         const LevelData<FArrayBox>& a_y, Real a_a, Real a_b)
{
  m_levelOps.axby(a_lhs, a_x, a_y, a_a, a_b);
}
void AMRPoissonOp::scale(LevelData<FArrayBox>& a_lhs, const Real& a_scale)
{
  m_levelOps.scale(a_lhs, a_scale);
}
void AMRPoissonOp::setToZero(LevelData<FArrayBox>& a_lhs)
{
  m_levelOps.setToZero(a_lhs);
}

void AMRPoissonOp::relax(LevelData<FArrayBox>& a_e,
                         const LevelData<FArrayBox>& a_residual,
                         int a_iterations)
{
  for(int i=0; i<a_iterations; i++)
    {
      // this isn't necessary, since levelGSRB calls homogenousCFInterp
      //homogeneousCFInterp(a_e);
      //levelGSRB(a_e, a_residual);
      looseGSRB(a_e, a_residual);
      //overlapGSRB(a_e, a_residual);
      //levelGSRBLazy(a_e, a_residual);
      //levelJacobi(a_e, a_residual);
    }
}

void AMRPoissonOp::createCoarser(LevelData<FArrayBox>& a_coarse,
                                 const LevelData<FArrayBox>& a_fine,
                                 bool ghosted)
{
  // CH_assert(!ghosted);
  IntVect ghost = a_fine.ghostVect();
  DisjointBoxLayout dbl;
  CH_assert(dbl.coarsenable(2));
  coarsen(dbl, a_fine.disjointBoxLayout(), 2); //multigrid, so coarsen by 2
  a_coarse.define(dbl, a_fine.nComp(), ghost);
}

void AMRPoissonOp::restrictResidual(LevelData<FArrayBox>& a_resCoarse,
                                    LevelData<FArrayBox>& a_phiFine,
                                    const LevelData<FArrayBox>& a_rhsFine)
{
  homogeneousCFInterp(a_phiFine);
  const DisjointBoxLayout& dblFine = a_phiFine.disjointBoxLayout();
  for(DataIterator dit = a_phiFine.dataIterator(); dit.ok(); ++dit)
    {
      FArrayBox& phi = a_phiFine[dit];
      m_bc(phi,  dblFine[dit()], m_domain,  m_dx, true);
    }

  a_phiFine.exchange(a_phiFine.interval(), m_exchangeCopier);

  for(DataIterator dit = a_phiFine.dataIterator(); dit.ok(); ++dit)
    {
      FArrayBox& phi = a_phiFine[dit];
      const FArrayBox& rhs = a_rhsFine[dit];
      FArrayBox& res = a_resCoarse[dit];

      Box region = dblFine.get(dit());
      const IntVect& iv=region.smallEnd();
      IntVect civ = coarsen(iv, 2);
      res.setVal(0.0);
      FORT_RESTRICTRES(CHF_CONST_FRA_SHIFT(phi, iv),
                       CHF_CONST_FRA_SHIFT(rhs, iv),
                       CHF_FRA_SHIFT(res, civ),
                       CHF_BOX_SHIFT(region, iv),
                       CHF_CONST_REAL(m_dx),
                       CHF_CONST_REAL(m_alpha),
                       CHF_CONST_REAL(m_beta));

    }
}

Real AMRPoissonOp::norm(const LevelData<FArrayBox>& a_x, int a_ord)
{
  CH_TIME("AMRPoissonOp::norm");
  return CH_XD::norm(a_x, a_x.interval(), a_ord);
}

Real AMRPoissonOp::localMaxNorm(const LevelData<FArrayBox>& a_x)
{
  Real localMax = 0;
  int nComp=a_x.nComp();
  for(DataIterator dit=a_x.dataIterator(); dit.ok(); ++dit)
    {
      localMax = Max(localMax, a_x[dit].norm(a_x.box(dit()), 0, 0, nComp));
    }
  return localMax;
}
/***/

void AMRPoissonOp::prolongIncrement(LevelData<FArrayBox>& a_phiThisLevel,
                                    const LevelData<FArrayBox>& a_correctCoarse)
{
  DisjointBoxLayout dbl = a_phiThisLevel.disjointBoxLayout();
  int mgref = 2; //this is a multigrid func
  for(DataIterator dit = a_phiThisLevel.dataIterator(); dit.ok(); ++dit)
    {
      FArrayBox& phi =  a_phiThisLevel[dit];
      const FArrayBox& coarse = a_correctCoarse[dit];
      Box region = dbl.get(dit());
      const IntVect& iv = region.smallEnd();
      IntVect civ=coarsen(iv, 2);

      FORT_PROLONG(CHF_FRA_SHIFT(phi, iv),
                   CHF_CONST_FRA_SHIFT(coarse, civ),
                   CHF_BOX_SHIFT(region, iv),
                   CHF_CONST_INT(mgref));

    }
}

/***/
void AMRPoissonOp::
levelGSRB(LevelData<FArrayBox>&       a_phi,
          const LevelData<FArrayBox>& a_rhs)
{
  CH_assert(a_phi.isDefined());
  CH_assert(a_rhs.isDefined());
  CH_assert(a_phi.ghostVect() >= IntVect::Unit);
  CH_assert(a_phi.nComp() == a_rhs.nComp());
  const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();

  // do first red, then black passes
  for (int whichPass =0; whichPass <= 1; whichPass++)
    {

      homogeneousCFInterp(a_phi);

      //fill in intersection of ghostcells and a_phi's boxes
      a_phi.exchange(a_phi.interval(), m_exchangeCopier);

      // now step through grids...
      DataIterator dit = a_phi.dataIterator();
      for (dit.begin(); dit.ok(); ++dit)
        {
          // invoke physical BC's where necessary
          m_bc(a_phi[dit],  dbl[dit()], m_domain,  m_dx, true);
        }

      for (dit.begin(); dit.ok(); ++dit)
        {
          const Box& region = dbl.get(dit());
          if(m_alpha==0 && m_beta == 1){
            FORT_GSRBLAPLACIAN(CHF_FRA(a_phi[dit]),
                               CHF_CONST_FRA(a_rhs[dit]),
                               CHF_BOX(region),
                               CHF_CONST_REAL(m_dx),
                               CHF_CONST_INT(whichPass));
          } else {
            FORT_GSRBHELMHOLTZ(CHF_FRA(a_phi[dit]),
                               CHF_CONST_FRA(a_rhs[dit]),
                               CHF_BOX(region),
                               CHF_CONST_REAL(m_dx),
                               CHF_CONST_REAL(m_alpha),
                               CHF_CONST_REAL(m_beta),
                               CHF_CONST_INT(whichPass));
          }
        } // end loop through grids

    } // end loop through red-black
}


void AMRPoissonOp::looseGSRB(LevelData<FArrayBox>&       a_phi,
                             const LevelData<FArrayBox>& a_rhs)
{
  CH_TIMERS("AMRPoissonOp::looseGSRB");
  CH_TIMER("applyBC", tb);

  CH_assert(a_phi.isDefined());
  CH_assert(a_rhs.isDefined());
  CH_assert(a_phi.ghostVect() >= IntVect::Unit);
  CH_assert(a_phi.nComp() == a_rhs.nComp());
  const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();
   
  //fill in intersection of ghostcells and a_phi's boxes
  homogeneousCFInterp(a_phi);

  a_phi.exchange(a_phi.interval(), m_exchangeCopier);


  // now step through grids...
  DataIterator dit = a_phi.dataIterator();
  for (dit.begin(); dit.ok(); ++dit)
    {
      // invoke physical BC's where necessary
      CH_START(tb);
      m_bc(a_phi[dit],  dbl[dit()], m_domain,  m_dx, true);
      CH_STOP(tb);
      const Box& region = dbl.get(dit());
      if(m_alpha==0 && m_beta==1)
        {
          int whichPass = 0;
          FORT_GSRBLAPLACIAN(CHF_FRA(a_phi[dit]),
                        CHF_CONST_FRA(a_rhs[dit]),
                        CHF_BOX(region),
                        CHF_CONST_REAL(m_dx),
                             CHF_CONST_INT(whichPass));

//           CH_START(tb);
//           m_bc(a_phi[dit],  dbl[dit()], m_domain,  m_dx, true);
//           CH_STOP(tb);

          whichPass = 1;
          FORT_GSRBLAPLACIAN(CHF_FRA(a_phi[dit]),
                             CHF_CONST_FRA(a_rhs[dit]),
                             CHF_BOX(region),
                             CHF_CONST_REAL(m_dx),
                             CHF_CONST_INT(whichPass));
        } else {
        int whichPass = 0;
        FORT_GSRBHELMHOLTZ(CHF_FRA(a_phi[dit]),
                           CHF_CONST_FRA(a_rhs[dit]),
                           CHF_BOX(region),
                           CHF_CONST_REAL(m_dx),
                           CHF_CONST_REAL(m_alpha),
                           CHF_CONST_REAL(m_beta),
                           CHF_CONST_INT(whichPass));

//         CH_START(tb);
//         m_bc(a_phi[dit],  dbl[dit()], m_domain,  m_dx, true);
//         CH_STOP(tb);

        whichPass = 1;
        FORT_GSRBHELMHOLTZ(CHF_FRA(a_phi[dit]),
                           CHF_CONST_FRA(a_rhs[dit]),
                           CHF_BOX(region),
                           CHF_CONST_REAL(m_dx),
                           CHF_CONST_REAL(m_alpha),
                           CHF_CONST_REAL(m_beta),
                           CHF_CONST_INT(whichPass));
      }
      
    } // end loop through grids
      

}

void AMRPoissonOp::overlapGSRB(LevelData<FArrayBox>&      a_phi,
                             const LevelData<FArrayBox>& a_rhs)
{
  CH_TIMERS("AMRPoissonOp::overlapGSRB");
  CH_TIMER("applyBC", tb);

  CH_assert(a_phi.isDefined());
  CH_assert(a_rhs.isDefined());
  CH_assert(a_phi.ghostVect() >= IntVect::Unit);
  CH_assert(a_phi.nComp() == a_rhs.nComp());
  const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();
   


  if(dbl.size()==1 || m_alpha != 1 || m_beta != 0)
    looseGSRB(a_phi, a_rhs);


  a_phi.exchangeBegin(m_exchangeCopier);

  homogeneousCFInterp(a_phi);


  // now step through grids...
  DataIterator dit = a_phi.dataIterator();
  for (dit.begin(); dit.ok(); ++dit)
    {
      // invoke physical BC's where necessary
      CH_START(tb);
      m_bc(a_phi[dit],  dbl[dit()], m_domain,  m_dx, true);
      CH_STOP(tb);
      Box region = dbl.get(dit());
      region.grow(-1); // just do the interior on the first run through 
      int whichPass = 0;
      FORT_GSRBLAPLACIAN(CHF_FRA(a_phi[dit]),
                         CHF_CONST_FRA(a_rhs[dit]),
                         CHF_BOX(region),
                         CHF_CONST_REAL(m_dx),
                         CHF_CONST_INT(whichPass));


      whichPass = 1;
      FORT_GSRBLAPLACIAN(CHF_FRA(a_phi[dit]),
                         CHF_CONST_FRA(a_rhs[dit]),
                         CHF_BOX(region),
                         CHF_CONST_REAL(m_dx),
                         CHF_CONST_INT(whichPass));
    }

  a_phi.exchangeEnd();

  // now step through grid edges

  for (dit.begin(); dit.ok(); ++dit)
    {
 
      const Box& b = dbl.get(dit());
      for(int dir=0; dir<CH_SPACEDIM; ++dir){
        for(SideIterator side; side.ok(); ++side){
          int whichPass = 0;
          Box region = adjCellBox(b, dir, side(), 1);
          region.shift(dir, -sign(side()));
          for(int i=0; i<dir; i++) region.grow(i, -1);
          FORT_GSRBLAPLACIAN(CHF_FRA(a_phi[dit]),
                             CHF_CONST_FRA(a_rhs[dit]),
                             CHF_BOX(region),
                             CHF_CONST_REAL(m_dx),
                             CHF_CONST_INT(whichPass));


          whichPass = 1;
          FORT_GSRBLAPLACIAN(CHF_FRA(a_phi[dit]),
                             CHF_CONST_FRA(a_rhs[dit]),
                             CHF_BOX(region),
                             CHF_CONST_REAL(m_dx),
                             CHF_CONST_INT(whichPass));
        }
      }
    }
  
}

/***/
bool 
nextColorLoc(IntVect&       a_color,
             const IntVect& a_limit)
{
  a_color[0]++;

  for(int i=0; i<CH_SPACEDIM-1; ++i)
    {
      if(a_color[i] > a_limit[i])
        {
          a_color[i] = 0;
          a_color[i+1]++;
        }
    }
  if(a_color[CH_SPACEDIM-1] > a_limit[CH_SPACEDIM-1])
    {
      return false;
    }

  return true;
}
/***/
void AMRPoissonOp::
levelGSRBLazy(LevelData<FArrayBox>&       a_phi,
              const LevelData<FArrayBox>& a_rhs)
{

  CH_assert(a_phi.isDefined());
  CH_assert(a_rhs.isDefined());
  CH_assert(a_phi.ghostVect() >= IntVect::Unit);
  CH_assert(a_phi.nComp() == a_rhs.nComp());
  const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();

  IntVect color = IntVect::Zero;
  IntVect limit = IntVect::Unit;
  color[0]=-1;

  // Loop over all possibilities (in all dimensions)
  while (nextColorLoc(color, limit))
    {
      homogeneousCFInterp(a_phi);

      LevelData<FArrayBox> lphi;
      m_levelOps.create(lphi, a_rhs);
      //after this lphi = L(phi)
      //this call contains bcs and exchange
      applyOp(lphi, a_phi, true);

      for(DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit)
        {
          Box dblBox  = dbl.get(dit());

          IntVect loIV = dblBox.smallEnd();
          IntVect hiIV = dblBox.bigEnd();

          for (int idir = 0; idir < SpaceDim; idir++)
            {
              if (loIV[idir] % 2 != color[idir])
                {
                  loIV[idir]++;
                }
            }

          if (loIV <= hiIV)
            {
              Box coloredBox(loIV, hiIV);
              FORT_GSRBLAZY(CHF_FRA1(a_phi[dit], 0),
                            CHF_CONST_FRA1( lphi[dit], 0),
                            CHF_CONST_FRA1(a_rhs[dit], 0),
                            CHF_BOX(coloredBox),
                            CHF_REAL(m_alpha), 
                            CHF_REAL(m_beta), 
                            CHF_REAL(m_dx));
            }
        }
    } // end loop through red-black
}

void AMRPoissonOp::levelJacobi(LevelData<FArrayBox>& a_phi,
                               const LevelData<FArrayBox>& a_rhs)
{
  LevelData<FArrayBox> resid;
  create(resid, a_rhs);
  // Get the residual
  residual(resid,a_phi,a_rhs,true);

  // Create the weight
  Real weight = m_alpha;
  for (int idir = 0; idir < SpaceDim; idir++)
    {
      weight += -2.0 * m_beta/ (m_dx*m_dx);
    }
  // Do the Jacobi relaxation
  //Real one = 1.0;
  //axby(a_phi,a_phi,resid,one,0.5/weight);
  incr(a_phi, resid, 0.5/weight);
}

void
AMRPoissonOp::homogeneousCFInterp(LevelData<FArrayBox>& a_phif)
{

  CH_assert( a_phif.ghostVect() >= IntVect::Unit);

  if(a_phif.disjointBoxLayout().size() == 1)
    {
      singleBoxCFInterp(a_phif[a_phif.dataIterator()]);
    }
  else
    {
      DataIterator dit = a_phif.dataIterator();
      for(dit.begin(); dit.ok(); ++dit)
        {
          const DataIndex& datInd = dit();
          for(int idir = 0; idir < SpaceDim; idir++)
            {
              SideIterator sit;
              for(sit.begin(); sit.ok(); sit.next())
                {
                  homogeneousCFInterp(a_phif,datInd,idir,sit());
                }
            }
        }
    }
}


void
AMRPoissonOp::singleBoxCFInterp(FArrayBox& a_phi)
{
  Box region = a_phi.box();
  region.grow(-1);

  for(int idir = 0; idir < SpaceDim; idir++)
    {
      SideIterator sit;
      for(sit.begin(); sit.ok(); sit.next())
        {
          Box edge = adjCellBox(region, idir, sit(), 1);
          int ihiorlo = sign(sit());
          FORT_INTERPHOMO(CHF_FRA(a_phi),
                      CHF_BOX(edge),
                      CHF_CONST_REAL(m_dx),
                      CHF_CONST_REAL(m_dxCrse),
                      CHF_CONST_INT(idir),
                      CHF_CONST_INT(ihiorlo));
        }
    }
}


void
AMRPoissonOp::homogeneousCFInterp(LevelData<FArrayBox>& a_phif,
                                  const DataIndex& a_datInd,
                                  int a_idir,
                                  Side::LoHiSide a_hiorlo)
{

  CH_assert((a_idir >= 0) && (a_idir  < SpaceDim));
  CH_assert((a_hiorlo == Side::Lo )||(a_hiorlo == Side::Hi ));
  CH_assert( a_phif.ghostVect() >= IntVect::Unit);
  //  CH_assert (m_ncomp == a_phif.nComp());

  const CFIVS* cfivs_ptr = NULL;
  if(a_hiorlo == Side::Lo)
    cfivs_ptr = &(m_cfivs.loCFIVS(a_datInd, a_idir));
  else
    cfivs_ptr = &(m_cfivs.hiCFIVS(a_datInd, a_idir));

  if(cfivs_ptr->isPacked())
    {
      int ihiorlo = sign(a_hiorlo);
      FORT_INTERPHOMO(CHF_FRA(a_phif[a_datInd]),
                      CHF_BOX(cfivs_ptr->packedBox()),
                      CHF_CONST_REAL(m_dx),
                      CHF_CONST_REAL(m_dxCrse),
                      CHF_CONST_INT(a_idir),
                      CHF_CONST_INT(ihiorlo));
    }
  else
    {
      const IntVectSet& interp_ivs = cfivs_ptr->getFineIVS();
      if(!interp_ivs.isEmpty())
        {
          // Assuming homogenous, interpolate on fine ivs
          interpOnIVSHomo(a_phif, a_datInd, a_idir,
                          a_hiorlo, interp_ivs);
        }
    }
}

void
AMRPoissonOp::interpOnIVSHomo(LevelData<FArrayBox>& a_phif,
                              const DataIndex& a_datInd,
                              const int a_idir,
                              const Side::LoHiSide a_hiorlo,
                              const IntVectSet& a_interpIVS)
{

  CH_assert((a_idir >= 0) && (a_idir  < SpaceDim));
  CH_assert((a_hiorlo == Side::Lo )||(a_hiorlo == Side::Hi ));
  CH_assert( a_phif.ghostVect() >= IntVect::Unit);
  IVSIterator fine_ivsit(a_interpIVS);
  FArrayBox& a_phi = a_phif[a_datInd];

  // much of these scalar values can be precomputed and stored if
  // we ever need to speed-up this function (ndk)
  Real x1 = m_dx;
  Real x2 = 0.5*(3. * x1 + m_dxCrse);
  Real denom = 1.0-((x1+x2)/x1);
  Real idenom = 1/(denom); // divide is more expensive usually
  Real x = 2.*x1;
  Real xsquared = x*x;

  Real m1 = 1/(x1*x1);
  Real m2 = 1/(x1*(x1-x2));

  Real q1 = 1/(x1-x2);
  Real q2 = x1+x2;

  int ihilo = sign(a_hiorlo);
  Real pa,pb,a,b;
  IntVect ivf;
  for(fine_ivsit.begin(); fine_ivsit.ok(); ++fine_ivsit)
    {
      ivf = fine_ivsit();
      // quadratic interpolation
      for(int ivar = 0; ivar < a_phif.nComp(); ivar++)
        {
          ivf[a_idir]-=2*ihilo;
          pa = a_phi(ivf, ivar);
          ivf[a_idir]+=ihilo;
          pb = a_phi(ivf, ivar);

          a = ((pb-pa)*m1 - (pb)*m2)*idenom;
          b = (pb)*q1 - a*q2;

          ivf[a_idir]+=ihilo;
          a_phi(fine_ivsit(), ivar) = a*xsquared + b*x + pa;

        } //end loop over components
    } //end loop over fine intvects
}

void AMRPoissonOp::AMRResidualNF(LevelData<FArrayBox>& a_residual,
                                 const LevelData<FArrayBox>& a_phi,
                                 const LevelData<FArrayBox>& a_phiCoarse,
                                 const LevelData<FArrayBox>& a_rhs,
                                 bool a_homogeneousPhysBC)
{
  LevelData<FArrayBox>& phi = (LevelData<FArrayBox>&)a_phi;

  m_interpWithCoarser.coarseFineInterp(phi, a_phiCoarse);
  this->residualI(a_residual, a_phi, a_rhs, a_homogeneousPhysBC ); //apply boundary conditions
}



void AMRPoissonOp::AMROperatorNF(LevelData<FArrayBox>& a_LofPhi,
                                 const LevelData<FArrayBox>& a_phi,
                                 const LevelData<FArrayBox>& a_phiCoarse,
                                 bool a_homogeneousPhysBC)
{
  LevelData<FArrayBox>& phi = (LevelData<FArrayBox>&)a_phi;

  m_interpWithCoarser.coarseFineInterp(phi, a_phiCoarse);
  //apply physical boundary conditions in applyOp
  this->applyOpI(a_LofPhi, a_phi, a_homogeneousPhysBC ); 
}

void AMRPoissonOp::getFlux(FArrayBox&       a_flux,
                           const FArrayBox& a_data,
			   const Box&       a_edgebox,
                           int              a_dir,
                           int ref) const
{
// In this version of getFlux, the edgebox is passed in, and the flux array
// is already defined.

  CH_TIME("AMRPoissonOp::getFlux");
  CH_assert(a_dir >= 0);
  CH_assert(a_dir <  SpaceDim);
  CH_assert(!a_data.box().isEmpty());
  // if this fails, the data box was too small (one cell wide, in fact)
  CH_assert(!a_edgebox.isEmpty());
  Real beta_dx = m_beta*ref/(m_dx);

  FORT_NEWGETFLUX(CHF_FRA(a_flux),
                  CHF_CONST_FRA(a_data),
                  CHF_BOX(a_edgebox),
                  CHF_CONST_REAL(beta_dx),
                  CHF_CONST_INT(a_dir));
}

void AMRPoissonOp::getFlux(FArrayBox&       a_flux,
                           const FArrayBox& a_data,
                           int              a_dir,
                           int ref) const
{
  CH_TIME("AMRPoissonOp::getFlux");
  CH_assert(a_dir >= 0);
  CH_assert(a_dir <  SpaceDim);
  CH_assert(!a_data.box().isEmpty());

  Box edgebox = surroundingNodes(a_data.box(),a_dir);
  edgebox.grow(a_dir, -1);

  // if this fails, the data box was too small (one cell wide, in fact)
  CH_assert(!edgebox.isEmpty());

  a_flux.resize(edgebox, a_data.nComp());

  //FArrayBox fflux(edgebox, a_data.nComp());
  Real beta_dx = m_beta*ref/(m_dx);

#if 1
  FORT_NEWGETFLUX(CHF_FRA(a_flux),
                  CHF_CONST_FRA(a_data),
                  CHF_BOX(edgebox),
                  CHF_CONST_REAL(beta_dx),
                  CHF_CONST_INT(a_dir));
#else
  Real dx(m_dx);
  dx /= ref;

  BoxIterator bit(edgebox);
  for( bit.begin(); bit.ok(); bit.next())
    {
      IntVect iv = bit();
      IntVect shiftiv = BASISV(a_dir);
      IntVect ivlo = iv - shiftiv;
      IntVect ivhi = iv;

      CH_assert(a_data.box().contains(ivlo));
      CH_assert(a_data.box().contains(ivhi));

      for (int ivar = 0; ivar < a_data.nComp(); ivar++)
        {
          Real phihi = a_data(ivhi,ivar);
          Real philo = a_data(ivlo,ivar);
          Real gradphi =(phihi-philo)/dx;

          a_flux(iv,ivar) = m_beta*gradphi;
 
        }
    }
#endif

  // check code for debugging Fortran.  keep around for a little while.
//   for( bit.begin(); bit.ok(); bit.next())
//     {
//       IntVect iv = bit();
//       if(fflux(iv, 0) != a_flux(iv, 0))
//         {
//           MayDay::Error("Fortran and C++ disagree");
//         }
//     }
}

void AMRPoissonOp::reflux(const LevelData<FArrayBox>& a_phiFine,
                          const LevelData<FArrayBox>& a_phi,
                          LevelData<FArrayBox>& residual,
                          AMRLevelOp<LevelData<FArrayBox> >* a_finerOp)
{
  CH_TIMERS("AMRPoissonOp::reflux");
 



  int ncomp = 1;
  ProblemDomain fineDomain = refine(m_domain, m_refToFiner);

  CH_TIMER("build flux register", t1); 
  CH_START(t1);
//   LevelFluxRegister levfluxreg(a_phiFine.disjointBoxLayout(),
//                                  a_phi.disjointBoxLayout(),
//                                  fineDomain,
//                                  m_refToFiner,
//                                  ncomp);
  if(!levfluxreg.isDefined()){
    levfluxreg.define(a_phiFine.disjointBoxLayout(),
                      a_phi.disjointBoxLayout(),
                      fineDomain,
                      m_refToFiner,
                      ncomp);
  }
  CH_STOP(t1);
  

  levfluxreg.setToZero();
  Interval interv(0,a_phi.nComp()-1);

  CH_TIMER("increment coarse", t2);
  CH_START(t2);
  DataIterator dit = a_phi.dataIterator();
  for (dit.reset(); dit.ok(); ++dit)
    {
      const FArrayBox& coarfab = a_phi[dit];

      for (int idir = 0; idir < SpaceDim; idir++)
        {
          FArrayBox coarflux;
          getFlux(coarflux, coarfab, idir);

          Real scale = 1.0;
          levfluxreg.incrementCoarse(coarflux, scale,dit(), 
                                     interv,interv,idir);
        }
    }
  CH_STOP(t2);

  LevelData<FArrayBox>& p = ( LevelData<FArrayBox>&)a_phiFine;

  AMRPoissonOp* finerAMRPOp = (AMRPoissonOp*) a_finerOp;
  QuadCFInterp& quadCFI = finerAMRPOp->m_interpWithCoarser;
  
  quadCFI.coarseFineInterp(p, a_phi);
  // p.exchange(a_phiFine.interval()); //I'm pretty sure this is not necessary. bvs
  IntVect phiGhost = p.ghostVect();
  int ncomps = a_phiFine.nComp();
  CH_TIMER("increment fine", t3);
  CH_START(t3);
  DataIterator ditf = a_phiFine.dataIterator();
  const  DisjointBoxLayout& dblFine = a_phiFine.disjointBoxLayout();
  for (ditf.reset(); ditf.ok(); ++ditf)
    {
      const FArrayBox& phifFab = a_phiFine[ditf];
      const Box& gridbox = dblFine.get(ditf());
      for (int idir = 0; idir < SpaceDim; idir++)
        {
          //int normalGhost = phiGhost[idir];
          SideIterator sit;
          for (sit.begin(); sit.ok(); sit.next())
            {
              Side::LoHiSide hiorlo = sit();
	      Box fluxBox = bdryBox(gridbox,idir,hiorlo,1);

              FArrayBox fineflux(fluxBox,ncomps);
              getFlux(fineflux, phifFab, fluxBox, idir, m_refToFiner);

              Real scale = 1.0;
              levfluxreg.incrementFine(fineflux, scale, ditf(),
                                       interv, interv, idir, hiorlo);
            }
        }
    }
  CH_STOP(t3);
  Real scale =  1.0/m_dx;

  
  levfluxreg.reflux(residual, scale);
}

void AMRPoissonOp::AMRResidual(LevelData<FArrayBox>& a_residual,
                               const LevelData<FArrayBox>& a_phiFine,
                               const LevelData<FArrayBox>& a_phi,
                               const LevelData<FArrayBox>& a_phiCoarse,
                               const LevelData<FArrayBox>& a_rhs,
                               bool a_homogeneousPhysBC,
                               AMRLevelOp<LevelData<FArrayBox> >* a_finerOp)
{
  AMROperator(a_residual, a_phiFine, a_phi, a_phiCoarse,
              a_homogeneousPhysBC, a_finerOp);
  
  incr(a_residual, a_rhs, -1.0);
  // residual is rhs - L(phi)
  scale (a_residual, -1.0);
}


void AMRPoissonOp::AMROperator(LevelData<FArrayBox>& a_LofPhi,
                               const LevelData<FArrayBox>& a_phiFine,
                               const LevelData<FArrayBox>& a_phi,
                               const LevelData<FArrayBox>& a_phiCoarse,
                               bool a_homogeneousPhysBC,
                               AMRLevelOp<LevelData<FArrayBox> >* a_finerOp)
{
  LevelData<FArrayBox>& phi = (LevelData<FArrayBox>&)a_phi;

  m_interpWithCoarser.coarseFineInterp(phi, a_phiCoarse);
  applyOpI(a_LofPhi, a_phi, a_homogeneousPhysBC);
  reflux(a_phiFine, a_phi,  a_LofPhi, a_finerOp);
}

void AMRPoissonOp::AMRResidualNC(LevelData<FArrayBox>& a_residual,
                                 const LevelData<FArrayBox>& a_phiFine,
                                 const LevelData<FArrayBox>& a_phi,
                                 const LevelData<FArrayBox>& a_rhs,
                                 bool a_homogeneousPhysBC,
                                 AMRLevelOp<LevelData<FArrayBox> >* a_finerOp)
{

  AMROperatorNC(a_residual, a_phiFine, a_phi, 
                a_homogeneousPhysBC, a_finerOp);

  axby(a_residual, a_residual, a_rhs, -1.0, 1.0);
}

void AMRPoissonOp::AMROperatorNC(LevelData<FArrayBox>& a_LofPhi,
                                 const LevelData<FArrayBox>& a_phiFine,
                                 const LevelData<FArrayBox>& a_phi,
                                 bool a_homogeneousPhysBC,
                                 AMRLevelOp<LevelData<FArrayBox> >* a_finerOp)
{
  //no coarse-fine interpolation here
  applyOp(a_LofPhi, a_phi, a_homogeneousPhysBC);
  reflux(a_phiFine, a_phi,  a_LofPhi, a_finerOp);
}

void AMRPoissonOp::AMRRestrict(LevelData<FArrayBox>& a_resCoarse,
                               const LevelData<FArrayBox>& a_residual,
                               const LevelData<FArrayBox>& a_correction,
                               const LevelData<FArrayBox>& a_coarseCorrection)
{
  CH_TIMERS("AMRPoissonOp::AMRRestrict");
  LevelData<FArrayBox> r;
  create(r, a_residual);
  AMRRestrictS(a_resCoarse, a_residual, a_correction, a_coarseCorrection, r);
}
void AMRPoissonOp::AMRRestrictS(LevelData<FArrayBox>& a_resCoarse,
                                const LevelData<FArrayBox>& a_residual,
                                const LevelData<FArrayBox>& a_correction,
                                const LevelData<FArrayBox>& a_coarseCorrection,
                                LevelData<FArrayBox>& r)
{
  CH_TIMERS("AMRPoissonOp::AMRRestrictS");

  AMRResidualNF(r, a_correction, a_coarseCorrection, a_residual, true);
  DisjointBoxLayout dblCoar = a_resCoarse.disjointBoxLayout(); 
  DataIterator dit = a_residual.dataIterator();
  for (dit.begin(); dit.ok(); ++dit)
    {
      FArrayBox& coarse = a_resCoarse[dit];
      const FArrayBox& fine = r[dit];
      const Box& b = dblCoar.get(dit());
      Box refbox(IntVect::Zero,
                 (m_refToCoarser-1)*IntVect::Unit);
      FORT_AVERAGE( CHF_FRA(coarse),
                    CHF_CONST_FRA(fine),
                    CHF_BOX(b),
                    CHF_CONST_INT(m_refToCoarser),
                    CHF_BOX(refbox)
                    );
    }
}

/** a_correction += I[2h->h](a_coarseCorrection) */
void AMRPoissonOp::AMRProlong(LevelData<FArrayBox>& a_correction,
                              const LevelData<FArrayBox>& a_coarseCorrection)
{
  DisjointBoxLayout c;
  coarsen(c,  a_correction.disjointBoxLayout(), m_refToCoarser);
  LevelData<FArrayBox> eCoar(c, a_correction.nComp(),a_coarseCorrection.ghostVect());
  a_coarseCorrection.copyTo(eCoar.interval(), eCoar, eCoar.interval());

  DisjointBoxLayout dbl = a_correction.disjointBoxLayout();
  for(DataIterator dit = a_correction.dataIterator(); dit.ok(); ++dit)
    {
      FArrayBox& phi =  a_correction[dit];
      const FArrayBox& coarse = eCoar[dit];
      Box region = dbl.get(dit());
      const IntVect& iv =  region.smallEnd();
      IntVect civ= coarsen(iv, m_refToCoarser);
      FORT_PROLONG(CHF_FRA_SHIFT(phi, iv),
                   CHF_CONST_FRA_SHIFT(coarse, civ),
                   CHF_BOX_SHIFT(region, iv),
                   CHF_CONST_INT(m_refToCoarser));

    }
}

void AMRPoissonOp::AMRProlongS(LevelData<FArrayBox>& a_correction,
                               const LevelData<FArrayBox>& a_coarseCorrection,
                               LevelData<FArrayBox>& eCoar,
                               const Copier& a_copier)
{
  a_coarseCorrection.copyTo(eCoar.interval(), eCoar, eCoar.interval(), a_copier);
  
  DisjointBoxLayout dbl = a_correction.disjointBoxLayout();
  for(DataIterator dit = a_correction.dataIterator(); dit.ok(); ++dit)
    {
      FArrayBox& phi =  a_correction[dit];
      const FArrayBox& coarse = eCoar[dit];
      Box region = dbl.get(dit());
      const IntVect& iv =  region.smallEnd();
      IntVect civ= coarsen(iv, m_refToCoarser);
      FORT_PROLONG(CHF_FRA_SHIFT(phi, iv),
                   CHF_CONST_FRA_SHIFT(coarse, civ),
                   CHF_BOX_SHIFT(region, iv),
                   CHF_CONST_INT(m_refToCoarser));

    }
}
                            
void AMRPoissonOp::AMRUpdateResidual(LevelData<FArrayBox>& a_residual,
                                     const LevelData<FArrayBox>& a_correction,
                                     const LevelData<FArrayBox>& a_coarseCorrection)
{
//   LevelData<FArrayBox> r;
//   this->create(r, a_residual);
//   this->AMRResidualNF(r, a_correction, a_coarseCorrection, a_residual, true);
//   this->assign(a_residual, r);
  this->AMRResidualNF(a_residual, a_correction, a_coarseCorrection, a_residual, true);
}

//==========================================================
//
//==========================================================

// MultiGrid define function

void AMRPoissonOpFactory::define(const ProblemDomain& a_domain,
                                 const DisjointBoxLayout& a_grid,
                                 const Real& a_dx,
                                 BCHolder a_bc,
                                 int maxDepth,
                                 Real a_alpha,
                                 Real a_beta)
{

  Vector<DisjointBoxLayout> grids(1);
  grids[0] = a_grid;
  Vector<int> refRatio(1, 2);
  define(a_domain, grids, refRatio, a_dx, a_bc, a_alpha, a_beta);
}

AMRPoissonOp*
AMRPoissonOpFactory::MGnewOp(const ProblemDomain& a_indexSpace,
                             int depth,
                             bool homoOnly)
{
  // CH_assert(m_boxes.size()>depth);
  CH_TIME("AMRPoissonOpFactory::MGnewOp");
  int ref=0;
  for(;ref< m_domains.size(); ref++)
    {
      if(a_indexSpace.domainBox() == m_domains[ref].domainBox()) break;
    }
  CH_assert(ref !=  m_domains.size()); // didn't find domain
  Real dxCrse = -1;
  if(ref > 0)
    {
      dxCrse = m_dx[ref-1];
    }

  //DisjointBoxLayout layout(m_boxes[ref]);
  ProblemDomain domain(m_domains[ref]);
  Real dx = m_dx[ref];
  int coarsening = 1;
  for(int i=0; i<depth; i++)
    {
      coarsening*=2;
      domain.coarsen(2);
    }


  if(coarsening > 1 && !m_boxes[ref].coarsenable(coarsening*2)) return NULL;

  dx*=coarsening;
  DisjointBoxLayout layout;
  coarsen_dbl(layout, m_boxes[ref], coarsening);
  Copier ex = m_exchangeCopiers[ref];
  CFRegion cfivs = m_cfivs[ref];
  if(coarsening > 1){
    ex.coarsen(coarsening);
    cfivs.coarsen(coarsening);
  }
  AMRPoissonOp* newOp = new AMRPoissonOp;
  newOp->define(layout, dx, domain, m_bc, ex, cfivs);
  newOp->m_alpha   = m_alpha;
  newOp->m_beta    = m_beta;
  newOp->m_dxCrse  = dxCrse;
  return newOp;
}

AMRPoissonOp* AMRPoissonOpFactory::AMRnewOp(const ProblemDomain& a_indexSpace)
{
  CH_TIME("AMRPoissonOpFactory::AMRnewOp");

  AMRPoissonOp* newOp = new AMRPoissonOp;
  int ref = 0;
  Real dxCrse = -1.0;
  for(;ref< m_domains.size(); ref++)
    {
      if(a_indexSpace.domainBox() == m_domains[ref].domainBox()) break;
    }

  if(ref == 0) 
    {
      // coarsest AMR level
      if(m_domains.size() == 1)
        {
          //no finer level
          newOp->define(m_boxes[0],  m_dx[0],
                        a_indexSpace, m_bc, m_exchangeCopiers[0], m_cfivs[0]);
        }
      else
        {
          //finer level exists but no coarser 
          int dummyRat = 1;  //argument so compiler can find right function
          int refToFiner = m_refRatios[0]; //actual refinement ratio
          newOp->define(m_boxes[0],  m_boxes[1], m_dx[0],
                        dummyRat, refToFiner,  
                        a_indexSpace, m_bc,m_exchangeCopiers[0], m_cfivs[0]);
        }
    }
  else if (ref ==  m_domains.size()-1)
    {
      dxCrse = m_dx[ref-1];
      // finest AMR level
      newOp->define(m_boxes[ref], m_boxes[ref-1], m_dx[ref], 
                    m_refRatios[ref-1],
                    a_indexSpace, m_bc, m_exchangeCopiers[ref], m_cfivs[ref]);
    }
  else if ( ref == m_domains.size())
    {
      MayDay::Error("Did not find a domain to match AMRnewOp(const ProblemDomain& a_indexSpace)");

    }
  else
    {
      dxCrse = m_dx[ref-1];
      // intermediate AMR level, full define
      newOp->define(m_boxes[ref], m_boxes[ref+1], m_boxes[ref-1], m_dx[ref], 
                    m_refRatios[ref-1], m_refRatios[ref], a_indexSpace, m_bc, 
                    m_exchangeCopiers[ref], m_cfivs[ref]);
    }

  newOp->m_alpha = m_alpha;
  newOp->m_beta  = m_beta;
  newOp->m_dxCrse = dxCrse;
  return newOp;
}

//  AMR Factory define function
void AMRPoissonOpFactory::define(const ProblemDomain& a_coarseDomain,
                                 const Vector<DisjointBoxLayout>& a_grids,
                                 const Vector<int>& a_refRatios,
                                 const Real& a_coarsedx,
                                 BCHolder a_bc,
                                 Real a_alpha,
                                 Real a_beta)
{
  CH_TIME("AMRPoissonOpFactory::define");
  m_alpha = a_alpha;
  m_beta = a_beta;
  m_domains.resize(a_grids.size());
  m_exchangeCopiers.resize(a_grids.size());
  m_cfivs.resize(a_grids.size());
  m_boxes=a_grids;
  m_refRatios=a_refRatios;
  m_dx.resize(a_grids.size());
  m_bc = a_bc;
  m_domains[0] = a_coarseDomain;
  m_dx[0] = a_coarsedx;
  for(int i=1; i<a_grids.size(); i++)
    {
      m_dx[i] = m_dx[i-1]/m_refRatios[i] ;
      m_domains[i] = m_domains[i-1];
      m_domains[i].refine(m_refRatios[i-1]);
      m_exchangeCopiers[i].define(a_grids[i], a_grids[i], IntVect::Unit, true);
      m_exchangeCopiers[i].trimEdges(a_grids[i], IntVect::Unit);
      m_cfivs[i].define(a_grids[i], m_domains[i]);

    }
  m_exchangeCopiers[0].define(a_grids[0], a_grids[0], IntVect::Unit, true);
  m_exchangeCopiers[0].trimEdges(a_grids[0], IntVect::Unit);
  m_cfivs[0].define(a_grids[0], m_domains[0]);

}

int AMRPoissonOpFactory::refToFiner(const ProblemDomain& a_domain) const

{

  int retval = -1;
  bool found = false;
  for(int ilev = 0; ilev < m_domains.size(); ilev++)
    {
      if(m_domains[ilev].domainBox() == a_domain.domainBox())
        {
          retval = m_refRatios[ilev];
          found = true;
        }
    }
  if(!found)
    {
      MayDay::Error("Domain not found in AMR hierarchy");
    }
  return retval;
}
#include "NamespaceFooter.H"
