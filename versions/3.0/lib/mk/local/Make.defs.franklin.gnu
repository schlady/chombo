# -*- Mode: Makefile; Modified: "Fri 29 Jul 2005 15:14:39 by dbs"; -*-

## This file defines variables for use on the login nodes of the NERSC Linux
## machine 'franklin'.  
##
## NOTE: everything is always in 64bit mode

makefiles+=local/Make.defs.franklin

CXX=g++
FC=ftn
MPICXX=CC -target=linux
USE_64=TRUE

CPP=$(CXX) -E

# += not working, must set everthing here in Make.defs.local
#cxxcomflags += -g -DCH_DISABLE_SIGNALS
#cppdbgflags += -DCH_DISABLE_SIGNALS
#cxxdbgflags += -g -DCH_DISABLE_SIGNALS 

#cxxoptflags += -O2 -Minline -Mvect=sse -Munroll=c:3 -Mnoframe -Mlre -tp amd64e -DCH_DISABLE_SIGNALS

cxxoptflags += -march=opteron -ffast-math -O3
foptflags += -O2
flibflags += -L/opt/pgi/7.0.7/linux86-64/7.0/lib -lpgf90 -lpgf90rtl -lpgftnrtl -lpgc -lpghpf_mpi -lpghpf2 -lgfortran


# The appropriate 'module' must be loaded for this to work.
# For serial, do    'module load hdf5'
# For parallel, do  'module load hdf5_par'
# To use the 64bit compilation model of xlC/xlf, you should set USE_64=TRUE and:
#  for serial, do   'module load hdf5_64'
#  for parallel, do 'module load hdf5_par_64'

ifneq ($(USE_HDF),FALSE)
  # The NERSC HDF5 modules use different variables in different HDF versions (not smart)
  #[NOTE: the HDF5C variable in some of the modules has options that don't compile Chombo]
  HDFINCFLAGS=-I$(HDF5_DIR)/include
  HDFMPIINCFLAGS=-I$(HDF5_DIR)/include
  ifeq ($(HDF5_LIB),)
    HDFLIBFLAGS=$(HDF5)
    HDFMPILIBFLAGS=$(HDF5)
  else
    HDFLIBFLAGS=$(HDF5_LIB)
    HDFMPILIBFLAGS=$(HDF5_LIB)
  endif
endif

# Check that the right HDF module is loaded.
ifneq ($(USE_HDF),FALSE)
  ifeq ($(MPI),TRUE)
    ifeq ($(findstring parallel,$(HDF5_DIR)),)
      $(error HDF5 directory [$(HDF5_DIR)] is not parallel but MPI is TRUE.  Did you load the right module?)
    endif
  else
    ifeq ($(findstring serial,$(HDF5_DIR)),)
      $(error HDF5 directory [$(HDF5_DIR)] is not serial but MPI is FALSE.  Did you load the right module?)
    endif
    ifeq ($(USE_64),TRUE)
      ifneq ($(findstring 64,$(HDF5_DIR)),64)
        $(error compiling in 64bit mode, but HDF5 directory is not 64bit.  Did you load the right module?)
      endif
    endif
  endif
endif


ifeq ($(USE_64),FALSE)
  $(error Are you sure you want to run non-64bit?)
endif
