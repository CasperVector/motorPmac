# Makefile
TOP = ../..
include $(TOP)/configure/CONFIG

# The following are used for debugging messages.
#!USR_CXXFLAGS += -DDEBUG

DBD = pmacAsynIPPort.dbd
INC += pmacAsynIPPort.h

LIBRARY_IOC = pmacAsynIPPort

SRCS += pmacAsynIPPort.c

pmacAsynIPPort_LIBS += asyn
pmacAsynIPPort_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
