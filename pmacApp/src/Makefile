# Makefile
TOP = ../..
include $(TOP)/configure/CONFIG

# The following are used for debugging messages.
#!USR_CXXFLAGS += -DDEBUG

DBD += pmacAsynMotorPort.dbd
DBD += pmacAsynMotorPortVxWorks.dbd

LIBRARY_IOC += pmacAsynMotorPort

INC += pmacController.h
INC += pmacAxis.h
INC += pmacCSController.h
INC += pmacCSAxis.h
INC += pmacCSMonitor.h
INC += pmacCsGroups.h
INC += Hashtable.h
INC += StringHashtable.h
INC += IntegerHashtable.h
INC += pmacGroupsHashtable.h
INC += CharIntHashtable.h
INC += pmacCommandStore.h
INC += pmacMessageBroker.h
INC += pmacTrajectory.h
INC += pmacHardwareInterface.h
INC += pmacHardwareTurbo.h
INC += pmacHardwarePower.h
INC += pmacCallbackStore.h
INC += pmacCallbackInterface.h
INC += pmacDebugger.h

SRCS += pmacDebugger.cpp
SRCS += pmacCSMonitor.cpp
SRCS += pmacController.cpp
SRCS += pmacAxis.cpp
SRCS += pmacCSController.cpp
SRCS += pmacCSAxis.cpp
SRCS += pmacCsGroups.cpp
SRCS += pmacGroupsHashtable.cpp
SRCS += CharIntHashtable.cpp
SRCS += StringHashtable.cpp
SRCS += IntegerHashtable.cpp
SRCS += Hashtable.cpp
SRCS += pmacCommandStore.cpp
SRCS += pmacMessageBroker.cpp
SRCS += pmacTrajectory.cpp
SRCS += pmacHardwareInterface.cpp
SRCS += pmacHardwareTurbo.cpp
SRCS += pmacHardwarePower.cpp
SRCS += pmacCallbackStore.cpp
SRCS += pmacCallbackInterface.cpp

pmacAsynMotorPort_LIBS += motor asyn
pmacAsynMotorPort_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES

