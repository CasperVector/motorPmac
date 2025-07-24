#!../../bin/linux-x86_64/pmac

< envPaths
cd "${TOP}"

dbLoadDatabase "dbd/pmac.dbd"
pmac_registerRecordDeviceDriver pdbbase
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "${MOTOR}/db:${MOTOR_PMAC}/db")

cd "${TOP}/iocBoot/${IOC}"

# Create IP Port (PortName, IPAddr:Port)
pmacAsynIPConfigure("BRICK1port", "172.23.240.86:1025")
# Create SSH Port (PortName, IPAddress, Username, Password, Priority, DisableAutoConnect, noProcessEos)
#drvAsynPowerPMACPortConfigure("BRICK1port", "172.23.247.1", "root", "deltatau", "0", "0", "0")
# Configure Model 3 Controller Driver (ControlerPort, LowLevelDriverPort, Address, Axes, MovingPoll, IdlePoll)
pmacCreateController("Brick", "BRICK1port", 0, 8, 100, 1000)
# Configure Model 3 Axes Driver (Controler Port, Axis Count)
pmacCreateAxes("Brick", 8)

pmacDisableLimitsCheck("Brick", 1, 0)
pmacDisableLimitsCheck("Brick", 2, 0)
pmacDisableLimitsCheck("Brick", 3, 0)
pmacDisableLimitsCheck("Brick", 4, 0)
pmacDisableLimitsCheck("Brick", 5, 0)
pmacDisableLimitsCheck("Brick", 6, 0)
pmacDisableLimitsCheck("Brick", 7, 0)
pmacDisableLimitsCheck("Brick", 8, 0)

# Create CS (CSPortName, ControllerPort, CSNumber, ProgramNumber)
# Configure Model 3 CS Axes Driver (CSPortName, CSAxisCount)
pmacCreateCS("CS1", "Brick", 1, 10)
pmacCreateCSAxes("CS1", 9)
pmacCreateCS("CS2", "Brick", 2, 10)
pmacCreateCSAxes("CS2", 9)
pmacCreateCS("CS3", "Brick", 3, 10)
pmacCreateCSAxes("CS3", 9)

pmacCreateCsGroup("Brick", 0, "1,2->A,B", 6)
pmacCreateCsGroup("Brick", 1, "3,4->I", 2)
pmacCreateCsGroup("Brick", 2, "MIXED CS3", 8)
pmacCreateCsGroup("Brick", 3, "MIXED CS2", 8)

pmacCsGroupAddAxis("Brick", 0, 1, A, 2)
pmacCsGroupAddAxis("Brick", 0, 2, B, 2)
pmacCsGroupAddAxis("Brick", 0, 3, C, 2)
pmacCsGroupAddAxis("Brick", 0, 4, U, 2)
pmacCsGroupAddAxis("Brick", 0, 5, V, 2)
pmacCsGroupAddAxis("Brick", 0, 6, W, 2)
pmacCsGroupAddAxis("Brick", 0, 7, X, 2)
pmacCsGroupAddAxis("Brick", 0, 8, Y, 2)
pmacCsGroupAddAxis("Brick", 1, 3, I, 3)
pmacCsGroupAddAxis("Brick", 1, 4, I, 3)
pmacCsGroupAddAxis("Brick", 2, 1, A, 3)
pmacCsGroupAddAxis("Brick", 2, 2, B, 3)
pmacCsGroupAddAxis("Brick", 2, 3, I, 3)
pmacCsGroupAddAxis("Brick", 2, 4, I, 3)
pmacCsGroupAddAxis("Brick", 3, 1, A, 2)
pmacCsGroupAddAxis("Brick", 3, 2, B, 2)
pmacCsGroupAddAxis("Brick", 3, 3, I, 2)
pmacCsGroupAddAxis("Brick", 3, 4, I, 2)

pmacSetCoordStepsPerUnit("CS2", 1, 1000)
pmacSetCoordStepsPerUnit("CS2", 2, 100)

# For Non-Power PMAC, use myPmacStatus.template instead
dbLoadRecords("myPowerPmacStatus.db", "PMAC=BRICK1,PORT=Brick")
dbLoadRecords("pmac_motor.db", "PMAC=BRICK1,PORT=Brick,SPORT=BRICK1port,TIMEOUT=4")
iocInit

