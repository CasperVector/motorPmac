# Makefile
TOP = ../..
include $(TOP)/configure/CONFIG
ifdef SSH

# The following are used for debugging messages.
#!USR_CXXFLAGS += -DDEBUG

DBD += drvAsynPowerPMACPort.dbd

LIBRARY_IOC_Linux = powerPmacAsynPort

powerPmacAsynPort_SRCS += sshDriver.cpp
powerPmacAsynPort_SRCS += drvAsynPowerPMACPort.cpp
powerPmacAsynPort_LIBS += asyn ssh2
powerPmacAsynPort_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_IOC_Linux = powerPMACShell

powerPMACShell_SRCS += interactive.cpp
powerPMACShell_LIBS += powerPmacAsynPort
powerPMACShell_LIBS += asyn ssh2
powerPMACShell_LIBS += $(EPICS_BASE_IOC_LIBS)

USR_INCLUDES+= $(SSH_INCLUDE)
ssh2_DIR = $(SSH_LIB)

endif
include $(TOP)/configure/RULES

