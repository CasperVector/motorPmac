TOP=../..

include $(TOP)/configure/CONFIG

# -------------------------------

DBD = pmacAsynIPPort.dbd
INC += pmacAsynIPPort.h

LIBRARY = pmacAsynIPPort
pmacAsynIPPort_SRCS = pmacAsynIPPort.c

pmacAsynIPPort_LIBS += asyn
pmacAsynIPPort_LIBS += $(EPICS_BASE_IOC_LIBS)
pmacAsynIPPort_SYS_LIBS_WIN32 += ws2_32

include $(TOP)/configure/RULES
