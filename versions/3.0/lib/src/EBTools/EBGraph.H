#ifdef CH_LANG_CC
/*
*      _______              __
*     / ___/ /  ___  __ _  / /  ___
*    / /__/ _ \/ _ \/  V \/ _ \/ _ \
*    \___/_//_/\___/_/_/_/_.__/\___/
*    Please refer to Copyright.txt, in Chombo's root directory.
*/
#endif

//  ANAG, LBNL

#ifndef _EBGRAPH_H_
#define _EBGRAPH_H_

#include "REAL.H"
#include "IntVect.H"
#include "IntVectSet.H"
#include "VolIndex.H"
#include "Interval.H"
#include "FaceIndex.H"
#include "IntVectSet.H"
#include "RealVect.H"
#include "BaseFab.H"
#include "SPMD.H"
#include "IrregNode.H"
#include "GraphNode.H"
#include "RefCountedPtr.H"
#include "FaceIterator.H"
#include "VoFIterator.H"
#include <map>
#include "HDF5Portable.H"
#include "NamespaceHeader.H"

class EBGraph;
/// Geometric description within a box
/**
   EBGraphImplem represents the geometric information
   of the domain at a given refinement
   within the boundaries of a particular box.
*/
class EBGraphImplem
{
public:
  ///
  /**
     The EBGraphImplem can be all regular or all
     covered or contain irregular cells.
  */
  enum TAG {AllRegular = 0, AllCovered, HasIrregular};

  ///
  /**
      Makes an EBGraphImplem whose contents are undefined.
  */
  EBGraphImplem();

  ///
  ~EBGraphImplem();

  ///
  /**
     If the graph has irregular cells
     get a mask over the size of the graph.
     -1 for covered cells or multivalued cells, 0 for singlevalued irregular, and 1 for regular.
     If the graph is all regular, regIrregCovered returns 1 and
     the mask is undefined.
     If the graph is all regular, regIrregCovered returns -1 and
     the mask is undefined.
   */
  const BaseFab<int>& getMask(int& a_regIrregCovered) const;

  ///
  /**
     Return the VoFs in the cell.
  */
  Vector<VolIndex> getVoFs(const IntVect& a_iv) const;

  ///
  int numVoFs(const IntVect& a_iv) const;

  ///
  int numVoFs(const Box& a_box) const ;

  ///
  int numFaces(const Box& a_box, int a_dir) const ;

  ///
  /**
     Return true if every cell in the EBGraphImplem is a regular cell.
  */
  bool isAllRegular() const;

  ///
  /**
     Return true neither all regular nor all covered
  */
  bool hasIrregular() const;

  ///
  /**
     Return true if  every cell in the EBGraphImplem is a covered cell.
  */
  bool isAllCovered() const;

  ///
  /**
     Return true if a_iv is an irregular cell.
  */
  bool isIrregular(const IntVect& a_iv) const;

  ///
  /**
     Return true if every cell in a_box is a
     covered cell.
  */
  bool isCovered(const Box& a_box) const;

  ///
  /**
     Return true if every cell in a_box is a
     regular cell.
  */
  bool isRegular(const Box& a_box) const;

  ///
  bool isRegular(const IntVect& a_iv) const;

  ///
  bool isCovered(const IntVect& a_iv) const;

  ///
  bool isConnected(const VolIndex& a_vof1,
                   const VolIndex& a_vof2) const;
  ///
  /**
     Return the faces on the side and direction
     of the input VoF.
  */
  Vector<FaceIndex> getFaces(const VolIndex& a_vof,
                             const int& a_idir,
                             const Side::LoHiSide& a_sd) const;

  ///
  Vector<Vector<VolIndex> >  getVoFSets(const Box& a_region) const;

  ///
  /**
     Returns the corresponding set of VoFs from the next finer
     EBGraphImplem (factor of two refinement).  The result is only
     defined if this {\tt EBGraphImplem} was defined by coarsening.
  */
  Vector<VolIndex> refine(const VolIndex& a_coarVoF) const;

  ///
  /**
     Returns the corresponding set of faces from the next finer
     EBGraphImplem (factor of two refinement).  The result is only
     defined if this {\tt EBGraphImplem} was defined by coarsening.
  */
  Vector<FaceIndex> refine(const FaceIndex& a_coarFace, const EBGraphImplem& a_fineGraph) const;

  ///
  /**
     Returns the corresponding face from the next coarser
     EBGraphImplem (factor of two refinement).  This only makes
     sense if there is a coarser level.
  */
  FaceIndex coarsen(const FaceIndex& a_coarFace) const;

  ///
  /**
     Returns the corresponding  VoF from the next coarser
     EBGraphImplem (same solution location, different index space, factor
     of two refinement ratio).
  */
  VolIndex coarsen(const VolIndex& a_fineVoF) const;

  ///
  /**
     Completely construct the graph from IrregNodes.
     This makes a DENSE representation of the graph.  To make
     sparse representations of all regular or all covered graphs,
     use setToAllRegular or setToAllCovered.\\
     irregGraph contains a complete list of irregular vofs. \\
     validRegion is the the region of the graph \\
     domain is the domain of compuation \\
     regIrregCovered = 1 for regular, 0 for irregular, -1 for covered.
     regIrregCovered must contain the valid region grown by 1. \\
     In chombospeak, \\
     regIrregCovered.box().contains(grow(validRegion, 1) & domain); \\
     otherwise an error will result
   */
  void
  buildGraph(const BaseFab<int>&      a_regIrregCovered,
             const Vector<IrregNode>& a_irregGraph,
             const Box&               a_validRegion,
             const ProblemDomain&     a_domain);

  ///check if  a box worth of graph is self consistent
  static void
  checkGraph(const BaseFab<int>&      a_regIrregCovered,
             const Vector<IrregNode>& a_irregGraph,
             const Box&               a_validRegion,
             const ProblemDomain&     a_domain)  ;

  ///
  /**
     Set the graph to all regular cells (sparse);
   */
  void setToAllRegular();

  ///
  /**
     Set the graph to all covered cells (sparse);
   */
  void setToAllCovered();

  ///
  /**
     Define an all-regular EBGraphImplem with the input box as its
     region.
  */
  void define(const Box& box);

  EBGraphImplem(const Box& box);

  ///
  /**
     Copy the information from a_source to the over the intersection
     of the box a_region, the box of the current EBGraphImplem and
     the box of a_source.  The Interval arguments are ignored.
     This function is required by LevelData.
  */
  void copy(const Box& a_regionFrom,
            const Interval& Cd,
            const Box& a_regionto,
            const EBGraphImplem& a_source,
            const Interval& Cs);

  ///
  const Box& getRegion() const;

  ///
  const ProblemDomain& getDomain() const;

  ///
  /**
     This stuff required by LevelData in parallel:
  */
  int size(const Box& R, const Interval& comps) const;

  ///
  void linearOut(void* buf, const Box& R, const Interval& comps) const;

  ///
  void linearIn(void* buf, const Box& R, const Interval& comps);

  ///
  static int preAllocatable()
  {
    return 2; // dyanmic allocatable.
  }

  ///
  bool isDefined() const;

  ///
  bool isDomainSet() const;

  //stuff below is not part of the public API

  //define vofs to be the coarsened vofs of the inputs
  //this must go first in coarsening.  defines happen here
  void coarsenVoFs(const EBGraphImplem&   a_fineGraph,
                   const Box& a_coarRegion);

  //define the faces to be the coarsened faces of the input
  void coarsenFaces(const EBGraphImplem& a_coarGhostGraph,
                    const EBGraphImplem& a_fineEBIS);

  //fix fineVoF->coarseVoF information of the input
  void fixFineToCoarse(EBGraphImplem& a_fineEBIS) const;

  //don't ask
  Vector<int>
  coarsenFaces(const VolIndex&       a_coarVoF,
               const EBGraphImplem&        a_coarGhostGraph,
               const EBGraphImplem&        a_fineGraph,
               const int&            a_idir,
               const Side::LoHiSide& a_sd);

  ///
  void setDomain(const ProblemDomain& a_domain);

  ///
  IntVectSet getIrregCells(const Box& a_subbox) const;

  ///
  IntVectSet getMultiCells(const Box& a_subbox) const;

  ///
  Vector<FaceIndex>
  getAllFaces(const IntVect& a_iv,
              const int& a_idir,
              const Side::LoHiSide& a_sd) const;

  Vector<FaceIndex>
  getIrregFaces(const Box& a_box, int a_dir) const;


  void fillMask(BaseFab<char>& a_mask) const;

  ///
  /**
    1 = regular
    0 = irregular
    -1 = covered
   */
  void fillIntMask(BaseFab<int>& a_mask) const;


  ///
  void addFullIrregularVoFs(const IntVectSet& a_vofsToChange,
                            const EBGraph&    a_ghostGraph);

  ///
  void
  getRegNextToMultiValued(IntVectSet&      a_vofsToChange,
                          const EBGraph&   a_ghostGraph) const;

  //bool operator<(const EBGraphImplem& ebiin) const;

  const FaceIterator& FaceIteratorCache(const Box& a_region, int a_direction,
                                        FaceStop::WhichFaces a_loc, const EBGraph& parent) const;

  const FaceIterator& FaceIteratorCache(const IntVectSet& ivs, int a_direction,
                                        FaceStop::WhichFaces a_loc, const EBGraph& parent) const;

  const VoFIterator& VoFIteratorCache(const IntVectSet& ivs,
                                      const EBGraph& parent) const;

private:

  ///box over which this graph is defined
  Box m_region;

  ///computational domain at this level of refinement
  ProblemDomain m_domain;

  ///
  /**
     If this is allregular or allcovered,
     the BaseFab below is undefined.
  */
  TAG m_tag;

  ///
  /**
     the graph description of the box.
     If the pointer GraphNode.m_thisNode == 0, the cell is covered.
     If the pointer GraphNode.m_thisNode == 1, the cell is regular.
     Otherwise, the pointer goes to the node of the graph.
  */
  BaseFab<GraphNode>  m_graph;

  ///
  bool m_isDefined;

  ///
  bool m_isDomainSet;

  ///
  IntVectSet* m_irregIVS;

  ///
  IntVectSet* m_multiIVS;

  static bool s_verbose;

  mutable std::map<Box, FaceIterator> m_boxFaceCache[2*CH_SPACEDIM];
  mutable std::map<IntVectSet, FaceIterator> m_faceCache[FaceStop::NUMTYPES*CH_SPACEDIM];
  mutable std::map<IntVectSet, VoFIterator> m_vofIteratorCache;

  //mutable because it gets built when needed
  mutable BaseFab<int> m_mask;
  mutable bool m_isMaskBuilt;
private:

  void operator=(const EBGraphImplem& ebiin)
  {
    MayDay::Error("invalid operator");
  }
  EBGraphImplem(const EBGraphImplem& ebiin)
  {
    MayDay::Error("invalid operator");
  }
};

/// Geometric description within a box
/**
   EBGraph represents the geometric information
   of the domain at a given refinement
   within the boundaries of a particular box.
   This is a refcounted object with all
   that implies.
*/
class EBGraph
{
public:
  ///
  /**
     The EBGraph can be all regular or all
     covered or contain irregular cells.
  */
  enum TAG {AllRegular = 0, AllCovered, HasIrregular};

  ///
  /**
      Makes an EBGraph whose contents are undefined.
  */
  EBGraph();

  ///
  ~EBGraph();

  ///
  /**
     If the graph has irregular cells
     get a mask over the size of the graph.
     -1 for covered cells or multivalued cells, 0 for singlevalued irregular, and 1 for regular.
     If the graph is all regular, regIrregCovered returns 1 and
     the mask is undefined.
     If the graph is all regular, regIrregCovered returns -1 and
     the mask is undefined.
   */
  const  BaseFab<int>& getMask(int& a_regIrregCovered) const;

  ///
  /**
     Return the VoFs in the cell.
  */
  Vector<VolIndex> getVoFs(const IntVect& a_iv) const;

  ///
  int numVoFs(const IntVect& a_iv) const;

  ///
  /**
     Return true if every cell in the EBGraph is a regular cell.
  */
  bool isAllRegular() const;

  ///
  /**
     Return true neither all regular nor all covered
  */
  bool hasIrregular() const;

  ///
  /**
     Return true if  every cell in the EBGraph is a covered cell.
  */
  bool isAllCovered() const;

  ///
  /**
     Return true if a_iv is an irregular cell.
  */
  bool isIrregular(const IntVect& a_iv) const;

  ///
  /**
     Return true if every cell in a_box is a
     covered cell.
  */
  bool isCovered(const Box& a_box) const;

  ///
  /**
     Return true if every cell in a_box is a
     regular cell.
  */
  bool isRegular(const Box& a_box) const;

  ///
  bool isRegular(const IntVect& a_iv) const;

  ///
  bool isCovered(const IntVect& a_iv) const;

  ///
  bool isConnected(const VolIndex& a_vof1,
                   const VolIndex& a_vof2) const;
  ///
  /**
     Return the faces on the side and direction
     of the input VoF.
  */
  Vector<FaceIndex> getFaces(const VolIndex& a_vof,
                             const int& a_idir,
                             const Side::LoHiSide& a_sd) const;

  ///
  Vector<Vector<VolIndex> >  getVoFSets(const Box& a_region) const;
  ///
  /**
     Returns the corresponding set of VoFs from the next finer
     EBGraph (factor of two refinement).  The result is only
     defined if this {\tt EBGraph} was defined by coarsening.
  */
  Vector<VolIndex> refine(const VolIndex& a_coarVoF) const;

  ///
  /**
     Returns the corresponding set of faces from the next finer
     EBGraph (factor of two refinement).  The result is only
     defined if this {\tt EBGraph} was defined by coarsening.
  */
  Vector<FaceIndex> refine(const FaceIndex& a_coarFace, const EBGraph& a_fineGraph) const;

  ///
  /**
     Returns the corresponding face from the next coarser
     EBGraph (factor of two refinement).  This only makes
     sense if there is a coarser level.
  */
  FaceIndex coarsen(const FaceIndex& a_coarFace) const;

  ///
  /**
     Returns the corresponding  VoF from the next coarser
     EBGraph (same solution location, different index space, factor
     of two refinement ratio).
  */
  VolIndex coarsen(const VolIndex& a_fineVoF) const;

  ///
  /**
     Define an all-regular EBGraph with the input box as its
     region.  The component argument is ignored.
     This function is required by LevelData.
  */
  EBGraph(const Box& a_box, int a_comps);

  ///
  /**
     Completely construct the graph from IrregNodes.
     This makes a DENSE representation of the graph.  To make
     sparse representations of all regular or all covered graphs,
     use setToAllRegular or setToAllCovered.\\
     irregGraph contains a complete list of irregular vofs. \\
     validRegion is the the region of the graph \\
     domain is the domain of compuation \\
     regIrregCovered = 1 for regular, 0 for irregular, -1 for covered.
     regIrregCovered must contain the valid region grown by 1. \\
     In chombospeak, \\
     regIrregCovered.box().contains(grow(validRegion, 1) & domain); \\
     otherwise an error will result
   */
  void
  buildGraph(const BaseFab<int>&      a_regIrregCovered,
             const Vector<IrregNode>& a_irregGraph,
             const Box&               a_validRegion,
             const ProblemDomain&     a_domain);

  ///
  /**
     Set the graph to all regular cells (sparse);
   */
  void setToAllRegular();

  ///
  /**
     Set the graph to all covered cells (sparse);
   */
  void setToAllCovered();

  ///
  /**
     Define an all-regular EBGraph with the input box as its
     region.
  */
  void define(const Box& box);

  ///
  /**
     Copy the information from a_source to the over the intersection
     of the box a_region, the box of the current EBGraph and
     the box of a_source.  The Interval arguments are ignored.
     This function is required by LevelData.
  */
  void copy(const Box& a_regionFrom,
            const Interval& Cd,
            const Box& a_regionto,
            const EBGraph& a_source,
            const Interval& Cs);

  ///
  const Box& getRegion() const;

  ///
  const ProblemDomain& getDomain() const;

  ///
  bool isDefined() const;

  ///
  bool isDomainSet() const;

  //stuff below is not part of the public API

  //define vofs to be the coarsened vofs of the inputs
  //this must go first in coarsening.  defines happen here
  void coarsenVoFs(const EBGraph&   a_fineGraph,
                   const Box& a_coarRegion);

  //define the faces to be the coarsened faces of the input
  void coarsenFaces(const EBGraph& a_coarGhostGraph,
                    const EBGraph& a_fineEBIS);

  //fix fineVoF->coarseVoF information of the input
  void fixFineToCoarse(EBGraph& a_fineEBIS) const;

  //don't ask
  Vector<int>
  coarsenFaces(const VolIndex&       a_coarVoF,
               const EBGraph&        a_coarGhostGraph,
               const EBGraph&        a_fineGraph,
               const int&            a_idir,
               const Side::LoHiSide& a_sd);

  ///
  void setDomain(const ProblemDomain& a_domain);

  ///
  IntVectSet getIrregCells(const Box& a_subbox) const;

  ///
  IntVectSet getMultiCells(const Box& a_subbox) const;

  ///
  EBGraph& operator=(const EBGraph& ebiin);

  ///
  EBGraph(const EBGraph& a_ebiin);

  ///
  Vector<FaceIndex>
  getAllFaces(const IntVect& a_iv,
              const int& a_idir,
              const Side::LoHiSide& a_sd) const;

  ///
  Vector<FaceIndex>
  getIrregFaces(const Box& a_box, int a_dir) const;

  void fillMask(BaseFab<char>& a_mask) const;

  ///
  /**
     1 = regular
     0 = irregular
     -1 = covered
   */
  void fillIntMask(BaseFab<int>& a_mask) const;


  ///
  EBGraph(const Box& box);
  ///pointer comparison
  bool operator==(const EBGraph& a_ebiin);

  /// pointer comparison
  bool operator<(const EBGraph& a_ebiin) const;

  ///
  /**
     This stuff required by LevelData in parallel:
  */
  int size(const Box& R, const Interval& comps) const;

  ///
  void linearOut(void* buf, const Box& R, const Interval& comps) const;

  ///
  void linearIn(void* buf, const Box& R, const Interval& comps);

  ///
  static int preAllocatable()
  {
    return 2; // dyanmic allocatable.
  }

  ///
  void addFullIrregularVoFs(const IntVectSet& a_vofsToChange,
                            const EBGraph&    a_ghostGraph);

  ///
  void
  getRegNextToMultiValued(IntVectSet&    a_vofsToChange,
                          const EBGraph& a_ghostGraph) const;

  int numVoFs(const Box& a_box) const ;

  int numFaces(const Box& a_box, int a_dir) const ;

  const FaceIterator& FaceIteratorCache(const IntVectSet& ivs, int a_direction,
                                        FaceStop::WhichFaces a_loc) const;

  const FaceIterator& FaceIteratorCache(const Box& a_region, int a_direction,
                                        FaceStop::WhichFaces a_loc) const;

  const VoFIterator& VoFIteratorCache(const IntVectSet& ivs) const;

private:

  ///
  RefCountedPtr<EBGraphImplem> m_implem;
};

// new code for binary portable IO of EBGraph objects.  bvs

template <>
void dataTypes(Vector<hid_t>& a_types, const EBGraph& a_dummySpecializationArg);

template <>
void dataSize(const EBGraph& item, Vector<int>& a_sizes,
              const Box& box, const Interval& comps);

template <>
void write(const EBGraph& item, Vector<void*>& a_allocatedBuffers,
           const Box& box, const Interval& comps);

template <>
void read(EBGraph& item, Vector<void*>& a_allocatedBuffers, const Box& box,
          const Interval& comps);

template <>
const char* name(const EBGraph& a_dummySpecializationArg);


/*******************************/
/*  inline EBGraph functions */
/*******************************/

inline
const BaseFab<int>&
EBGraph::getMask(int& a_regIrregCovered) const
{
  return m_implem->getMask(a_regIrregCovered);
}

inline void
EBGraph::buildGraph(const BaseFab<int>&      a_regIrregCovered,
                    const Vector<IrregNode>& a_irregGraph,
                    const Box&               a_validRegion,
                    const ProblemDomain&     a_domain)
{
  m_implem->buildGraph(a_regIrregCovered, a_irregGraph, a_validRegion, a_domain);
}
/*******************************/
inline void EBGraph::copy(const Box& a_regionFrom,
                          const Interval& Cd,
                          const Box& a_regionTo,
                          const EBGraph& a_source,
                          const Interval& Cs)
{
  m_implem->copy(a_regionFrom, Cd, a_regionTo,
                 *(a_source.m_implem), Cs);
}
/*******************************/
inline bool EBGraph::isDefined() const
{
  return m_implem->isDefined();
}
/*******************************/
inline bool EBGraph::isConnected(const VolIndex& a_vof1,
                                 const VolIndex& a_vof2) const
{
  return m_implem->isConnected(a_vof1, a_vof2);
}
/*******************************/
inline const Box& EBGraph::getRegion() const
{
  return m_implem->getRegion();
}
/*******************************/
inline EBGraph& EBGraph::operator=(const EBGraph& a_ebiin)
{
  if(&a_ebiin != this)
    {
      m_implem = a_ebiin.m_implem;
    }
  return *this;
}

/*******************************/
inline IntVectSet EBGraph::getMultiCells(const Box& a_subbox) const
{
  return m_implem->getMultiCells(a_subbox);
}
/*******************************/
inline IntVectSet EBGraph::getIrregCells(const Box& a_subbox) const
{
  return m_implem->getIrregCells(a_subbox);
}
/*******************************/
inline Vector<VolIndex> EBGraph::getVoFs(const IntVect& a_iv) const
{
  return m_implem->getVoFs(a_iv);
}
/*******************************/
inline bool EBGraph::isRegular(const IntVect& a_iv) const
{
  return m_implem->isRegular(a_iv);
}
/*******************************/
inline bool EBGraph::isAllRegular() const
{
  return m_implem->isAllRegular();
}
/*******************************/
inline bool EBGraph::isAllCovered() const
{
  return m_implem->isAllCovered();
}
/*******************************/
inline bool EBGraph::isIrregular(const IntVect& a_iv) const
{
  return m_implem->isIrregular(a_iv);
}
/*******************************/
inline bool EBGraph::isCovered(const IntVect& a_iv) const
{
  return m_implem->isCovered(a_iv);
}
/*******************************/
inline bool EBGraph::isCovered(const Box& a_box) const
{
  return m_implem->isCovered(a_box);
}
/*******************************/
inline bool EBGraph::isRegular(const Box& a_box) const
{
  return m_implem->isRegular(a_box);
}
/*******************************/
inline Vector<FaceIndex>
EBGraph::getFaces(const VolIndex& a_vof,
                  const int& a_idir,
                  const Side::LoHiSide& a_sd) const
{
  return m_implem->getFaces(a_vof, a_idir, a_sd);
}
/*******************************/
inline Vector<VolIndex>
EBGraph::refine(const VolIndex& a_coarVoF) const
{
  return m_implem->refine(a_coarVoF);
}
/*******************************/
inline Vector<FaceIndex>
EBGraph::refine(const FaceIndex& a_coarFace, const EBGraph& a_fineGraph) const
{
  return m_implem->refine(a_coarFace, *a_fineGraph.m_implem);
}
/*******************************/
inline FaceIndex EBGraph::coarsen(const FaceIndex& a_fineFace) const
{
  return m_implem->coarsen(a_fineFace);
}
/*******************************/
inline VolIndex EBGraph::coarsen(const VolIndex& a_fineVoF) const
{
  return m_implem->coarsen(a_fineVoF);
}
/*******************************/
inline bool EBGraph::operator==(const EBGraph& a_ebiin)
{
  return (m_implem == a_ebiin.m_implem);
}
/*******************************/
inline bool EBGraph::operator<(const EBGraph& a_ebiin) const
{
  return (m_implem < a_ebiin.m_implem);
}
/*******************************/
inline const ProblemDomain& EBGraph::getDomain() const
{
  return (m_implem->getDomain());
}
/*******************************/
inline void EBGraph::setDomain(const ProblemDomain& a_domain)
{
  m_implem->setDomain(a_domain);
}
/*******************************/
inline void EBGraph::setToAllRegular()
{
  m_implem->setToAllRegular();
}
/*******************************/
inline void EBGraph::setToAllCovered()
{
  m_implem->setToAllCovered();
}
/*******************************/
inline void EBGraph::
coarsenVoFs(const EBGraph& a_fineEBIS,
            const Box& a_coarRegion)
{
  m_implem->coarsenVoFs(*a_fineEBIS.m_implem, a_coarRegion);
}
/*******************************/
inline void EBGraph::
coarsenFaces(const EBGraph& a_coarGhost, const EBGraph& a_fineEBIS)
{
  m_implem->coarsenFaces(*a_coarGhost.m_implem, *a_fineEBIS.m_implem);
}
/*******************************/
inline void EBGraph::
fixFineToCoarse(EBGraph& a_fineEBIS) const
{
  m_implem->fixFineToCoarse(*a_fineEBIS.m_implem);
}
/*******************************/
inline Vector<FaceIndex>
EBGraph::getAllFaces(const IntVect& a_iv,
                     const int& a_idir,
                     const Side::LoHiSide& a_sd) const
{
  return m_implem->getAllFaces(a_iv, a_idir, a_sd);
}

/*******************************/

inline int
EBGraph::numVoFs(const IntVect& a_iv) const
{
  return m_implem->numVoFs(a_iv);
}

inline
const FaceIterator& EBGraph::FaceIteratorCache(const Box& a_region, int a_direction,
                                               FaceStop::WhichFaces a_loc) const
{
  return m_implem->FaceIteratorCache(a_region, a_direction, a_loc, *this);
}

inline
const FaceIterator& EBGraph::FaceIteratorCache(const IntVectSet& a_region, int a_direction,
                                               FaceStop::WhichFaces a_loc) const
{
  return m_implem->FaceIteratorCache(a_region, a_direction, a_loc, *this);
}
inline
const VoFIterator& EBGraph::VoFIteratorCache(const IntVectSet& ivs) const
{
  return m_implem->VoFIteratorCache(ivs, *this);
}

#include "NamespaceFooter.H"
#endif
