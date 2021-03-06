#ifdef CH_LANG_CC
/*
*      _______              __
*     / ___/ /  ___  __ _  / /  ___
*    / /__/ _ \/ _ \/  V \/ _ \/ _ \
*    \___/_//_/\___/_/_/_/_.__/\___/
*    Please refer to Copyright.txt, in Chombo's root directory.
*/
#endif

#include <iostream>
#include <cstring>
// #extern "C" {      // The #extern "C" might have been here for a reason...
#include <unistd.h>
// }

#include "SPMD.H"
#include "IntVectSet.H"
#include "parstream.H"
#include "NamespaceHeader.H"

using std::cout;
using std::endl;

// try a 30 Mbyte max message size and see if that helps.

long long CH_MAX_MPI_MESSAGE_SIZE = 30*1024*1024;
long long CH_MaxMPISendSize = 0;
long long CH_MaxMPIRecvSize  = 0;

int reportMPIStats()
{
  pout()<<"Chombo message size limit:"<< CH_MAX_MPI_MESSAGE_SIZE<<"\n"
        <<"Max send message size:"<<CH_MaxMPISendSize<<"\n"
        <<"Max recv message size:"<<CH_MaxMPIRecvSize<<std::endl;
  return 0;
}



Vector<int> pids;



#ifndef CH_MPI

int procID(){ return 0;}

// reset this to fool serial code into thinking its parallel
int num_procs = 1 ;

int GetRank(int pid)
{
  return 0;
}

int GetPID(int rank)
{
  CH_assert(rank == 0);
  return getpid();
}

unsigned int numProc(){ return num_procs;}

#else // CH_MPI version

int GetPID(int rank)
{
  if(rank<0) return -1;
  if(rank>=pids.size()) return -2;
  return pids[rank];
}

int GetRank(int pid)
{
  for(int i=0; i<pids.size(); ++i)
    {
      if(pids[i]== pid) return i;
    }
  return -1;
}

int procID()
{
  static int ret = -1;
  if(ret==-1){
//     int proc = getpid();
//     gather(pids, proc, 0);
//     broadcast(pids, 0);
    MPI_Comm_rank(Chombo_MPI::comm, &ret);
  }
  return ret;
}

unsigned int numProc()
{
  static int ret = -1;
  if(ret == -1){
    MPI_Comm_size(Chombo_MPI::comm, &ret);
  }
  return ret;
}

// hopefully static copy of opaque handles
MPI_Comm Chombo_MPI::comm = MPI_COMM_WORLD;

#endif // CH_MPI

/// these should work independent of MPI's existence
template < >
void linearIn<float>(float& a_outputT, const void* const a_inBuf)
{
  //this fandango is to avoid unaligned accesses
  char realBuf[sizeof(float)];
  memcpy(realBuf, a_inBuf, sizeof(float));
  float* buffer = (float*)realBuf;
  a_outputT = *buffer;
}

template < >
void linearOut<float>(void* const a_outBuf, const float& a_inputT)
{
  //this fandango is to avoid unaligned accesses
  char realBuf[sizeof(float)];
  float* realPtr = (float*)realBuf;
  *realPtr = a_inputT;
  memcpy(a_outBuf, realBuf, sizeof(float));
}

template < >
int linearSize<float>(const float& inputfloat)
{
  return sizeof(float);
}

template < >
void linearIn<double>(double& a_outputT, const void* const a_inBuf)
{
  //this fandango is to avoid unaligned accesses
  char realBuf[sizeof(double)];
  memcpy(realBuf, a_inBuf, sizeof(double));
  double* buffer = (double*)realBuf;
  a_outputT = *buffer;
}

template < >
void linearOut<double>(void* const a_outBuf, const double& a_inputT)
{
  //this fandango is to avoid unaligned accesses
  char realBuf[sizeof(double)];
  double* realPtr = (double*)realBuf;
  *realPtr = a_inputT;
  memcpy(a_outBuf, realBuf, sizeof(double));
}

template < >
int linearSize<double>(const double& inputdouble)
{
  return sizeof(double);
}

template < >
int linearSize(const RealVect& vindex)
{
  return sizeof(RealVect);
}

//RealVect specialization of linearIn
template < >
void linearIn(RealVect& a_outputT, const void* const inBuf)
{
  unsigned char* bob = (unsigned char*)inBuf;
  unsigned char* to = (unsigned char*)&a_outputT;
  memcpy(to + RealVect::io_offset, bob, SpaceDim*sizeof(Real));
}

//RealVect specialization of linearOut
template < >
void linearOut(void* const a_outBuf, const RealVect& a_inputT)
{
  unsigned char* bob = (unsigned char*)a_outBuf;
  const unsigned char* from = (const unsigned char*)&a_inputT;
  memcpy(bob, from + RealVect::io_offset, SpaceDim*sizeof(Real));
}

template < >
void linearIn<int>(int& a_outputT, const void* const a_inBuf)
{
  int* buffer = (int*)a_inBuf;
  a_outputT = *buffer;
}

template < >
void linearOut<int>(void* const a_outBuf, const int& a_inputT)
{
  int* buffer = (int*)a_outBuf;
  *buffer = a_inputT;
}

template < >
int linearSize<int>(const int& a_input)
{
  return sizeof(int);
}

template < >
void linearIn<long>(long& a_outputT, const void* const a_inBuf)
{
  long* buffer = (long*)a_inBuf;
  a_outputT = *buffer;
}

template < >
void linearOut<long>(void* const a_outBuf, const long& a_inputT)
{
  long* buffer = (long*)a_outBuf;
  *buffer = a_inputT;
}

template < >
int linearSize<long>(const long& a_input)
{
  return sizeof(long);
}

template < >
void linearIn<unsigned long>(unsigned long& a_outputT, const void* const a_inBuf)
{
  unsigned long* buffer = (unsigned long*)a_inBuf;
  a_outputT = *buffer;
}

template < >
void linearOut<unsigned long>(void* const a_outBuf, const unsigned long& a_inputT)
{
  unsigned long* buffer = (unsigned long*)a_outBuf;
  *buffer = a_inputT;
}

template < >
int linearSize<unsigned long>(const unsigned long& a_input)
{
  return sizeof(unsigned long);
}

template < >
void linearIn<Box>(Box& a_outputT, const void* const a_inBuf)
{
  int* intBuf = (int*)a_inBuf;
  IntVect lo, hi;
  //output is lo[0],lo[1],...
  for(int idir = 0; idir < SpaceDim; idir++)
    {
      lo[idir] = intBuf[idir];
      hi[idir] = intBuf[idir+SpaceDim];
    }
  a_outputT = Box(lo,hi);
}

template < >
void linearOut<Box>(void* const a_outBuf, const Box& a_inputT)
{
  int* intBuf = (int*)a_outBuf;
  const IntVect& lo = a_inputT.smallEnd();
  const IntVect& hi = a_inputT.bigEnd();
  //output is lo[0],lo[1],...
  for(int idir = 0; idir < SpaceDim; idir++)
    {
      intBuf[idir] = lo[idir];
      intBuf[idir+SpaceDim] = hi[idir];
    }
}

template < >
int linearSize<Box>(const Box& a_input)
{
  //box is stored as 2*spaceDim integers
  return(2*SpaceDim*sizeof(int));
}

//Vector<int>  specialization
template < > int linearSize(const Vector<int>& a_input)
{
  return linearListSize(a_input);
}
template < > void linearIn(Vector<int>& a_outputT, const void* const inBuf)
{
  linearListIn(a_outputT, inBuf);
}
template < > void linearOut(void* const a_outBuf, const Vector<int>& a_inputT)
{
  linearListOut(a_outBuf, a_inputT);
}

//Vector<long>  specialization
template < > int linearSize(const Vector<long>& a_input)
{
  return linearListSize(a_input);
}
template < > void linearIn(Vector<long>& a_outputT, const void* const inBuf)
{
  linearListIn(a_outputT, inBuf);
}
template < > void linearOut(void* const a_outBuf, const Vector<long>& a_inputT)
{
  linearListOut(a_outBuf, a_inputT);
}

//Vector<Real>  specialization
template < > int linearSize(const Vector<float>& a_input)
{
  return linearListSize(a_input);
}
template < > void linearIn(Vector<float>& a_outputT, const void* const inBuf)
{
  linearListIn(a_outputT, inBuf);
}
template < > void linearOut(void* const a_outBuf, const Vector<float>& a_inputT)
{
  linearListOut(a_outBuf, a_inputT);
}

template < > int linearSize(const Vector<double>& a_input)
{
  return linearListSize(a_input);
}
template < > void linearIn(Vector<double>& a_outputT, const void* const inBuf)
{
  linearListIn(a_outputT, inBuf);
}
template < > void linearOut(void* const a_outBuf, const Vector<double>& a_inputT)
{
  linearListOut(a_outBuf, a_inputT);
}

//Vector<Box>  specialization
template < > int linearSize(const Vector<Box>& a_input)
{
  return linearListSize(a_input);
}
template < > void linearIn(Vector<Box>& a_outputT, const void* const inBuf)
{
  linearListIn(a_outputT, inBuf);
}
template < > void linearOut(void* const a_outBuf, const Vector<Box>& a_inputT)
{
  linearListOut(a_outBuf, a_inputT);
}

//Vector<Vector<Box> >  specialization
template < > int linearSize(const Vector<Vector<Box> >& a_input)
{
  return linearListSize(a_input);
}
template < > void linearIn(Vector<Vector<Box> >& a_outputT, const void* const inBuf)
{
  linearListIn(a_outputT, inBuf);
}
template < > void linearOut(void* const a_outBuf, const Vector<Vector<Box> >& a_inputT)
{
  linearListOut(a_outBuf, a_inputT);
}

//Vector<Vector<int> >  specialization
template < > int linearSize(const Vector<Vector<int> >& a_input)
{
  return linearListSize(a_input);
}
template < > void linearIn(Vector<Vector<int> >& a_outputT, const void* const inBuf)
{
  linearListIn(a_outputT, inBuf);
}
template < > void linearOut(void* const a_outBuf, const Vector<Vector<int> >& a_inputT)
{
  linearListOut(a_outBuf, a_inputT);
}
// template < >
// void gather<IntVectSet>(Vector<IntVectSet>& a_outVec,
//             const IntVectSet& a_input,
//             int a_dest)
// {
//   Vector<Box> boxes = a_input.boxes();
//   Vector<Vector<Box> > all_boxes;

//   gather (all_boxes, boxes, a_dest);

//   const int num_vecs = all_boxes.size();
//   a_outVec.resize(num_vecs);

//   for (int i = 0; i < num_vecs; ++i)
//   {
//     IntVectSet& ivs = a_outVec[i];
//     const Vector<Box>& vb = all_boxes[i];
//     for (int ibox = 0; ibox < vb.size(); ++ibox)
//     {
//       ivs |= vb[ibox];
//     }
//   }
// }

// return id of unique processor for special serial tasks
int
uniqueProc(const SerialTask::task& a_task)
{
#ifdef NDEBUG
    switch (a_task)
    {
    case SerialTask::compute:
    default:
        return (0);
        //break;  // unreachable break can generate compiler warning
    }
#else
// in mpi, the debugger attaches to process 0
    return (0);
#endif
}

#include "NamespaceFooter.H"
