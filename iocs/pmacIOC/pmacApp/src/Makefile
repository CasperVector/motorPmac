TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

# The following are used for debugging messages.
#!USR_CXXFLAGS += -DDEBUG

#=============================
# Build the IOC application

PROD_IOC = pmac
# pmac.dbd will be created and installed
DBD += pmac.dbd

# pmac.dbd will be made up from these files:
pmac_DBD += base.dbd

# Include dbd files from all support applications:
pmac_DBD += asyn.dbd
pmac_DBD += motorSupport.dbd
pmac_DBD += devSoftMotor.dbd
pmac_DBD += pmacAsynIPPort.dbd
pmac_DBD += drvAsynPowerPMACPort.dbd
pmac_DBD += pmacAsynMotorPort.dbd
pmac_DBD += busySupport.dbd
pmac_DBD += calcSupport.dbd

# Add all the support libraries needed by this IOC
pmac_LIBS += pmacAsynIPPort powerPmacAsynPort pmacAsynMotorPort
pmac_LIBS += motor softMotor
pmac_LIBS += asyn busy calc
#ifdef SNCSEQ
pmac_LIBS += seq pv
#endif

# pmac_registerRecordDeviceDriver.cpp derives from pmac.dbd
pmac_SRCS += pmac_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
pmac_SRCS_DEFAULT += pmacMain.cpp
pmac_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#pmac_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
pmac_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

