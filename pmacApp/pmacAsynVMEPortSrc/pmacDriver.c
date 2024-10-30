/*
 *  Implementation of the Open/Close/Read/Write/Ioctl interface to
 *  PMAC DPRAM ASCII and PMAC Mailbox ASCII.
 *
 *  Author: Andy Foster (for Diamond)
 *  Date:   26th May 2006
 *
*/

/* ANSI C headers */
#include <epicsStdioRedirect.h>


/* rtems headers */
#include <rtems.h>
#include <rtems/io.h>
#include <rtems/error.h>
#include <rtems/system.h>
#include <rtems/console.h>
#include <rtems/termiostypes.h>
#include <rtems/seterr.h>
//#include  <libcpu/io.h>
#include <bsp.h>
#include <bsp/fatal.h>
#include <sys/filio.h>


/* EPICS headers */
#include <epicsRingBytes.h>
#include <epicsTypes.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <cantProceed.h>
#include <devLib.h>

typedef int STATUS;
#define IMPORT	extern
#define OK		0
#define ERROR		(-1)

/* PMAC headers */
#include <pmacVme.h>
#include <pmacDriver.h>

/** Makes writing to a register on the VME board a bit more explicit */
#define getReg(location)          (location)
#define setReg(location, value)   (location) = (value)

#define PMAC_DRIVER_DEBUG        0
#define PMAC_BASE_MBX_REGS_IN   16
#define PMAC_BASE_MBX_REGS_OUT  15
#define PMAC_BASE_ASC_REGS_OUT 160

#define logMsg errlogPrintf

int    pmacDrvNumAsc  = 0;     /* DPRAM ASCII driver number   */
int    pmacDrvNumMbx  = 0;     /* Mailbox ASCII driver number */
int    replyQueueSize = 40960; /* Size of ring buffer - large enough for a list gather response of 1024 samples of 3 variables */

typedef struct
{
	rtems_termios_device_context base;
	rtems_termios_tty *tty;
    int              ctlr;
    int              openFlag;
    int              cancelFlag;
    int              polling;
    epicsEventId     ioReceivedId;
    void             (*readMeISR)( void * );
    size_t           lastsent;			/* bytes transmitted in last write cycle */
} PMAC_DEV;

/* Function prototypes */

static void pmacAscReadMeISR( rtems_termios_device_context *base );
static void pmacMbxReadMeISR( rtems_termios_device_context *base );
static void pmacMbxReceivedISR( rtems_termios_device_context *base );

static bool pmacOpen( rtems_termios_tty *tty, rtems_termios_device_context *base, struct termios *term, rtems_libio_open_close_args_t *args);
static void pmacClose( rtems_termios_tty *tty, rtems_termios_device_context *base, rtems_libio_open_close_args_t *args);
static bool pmacSetAttributes( rtems_termios_device_context *base, const struct termios *term);
static int  pmacIoctl( rtems_termios_device_context *base, ioctl_command_t request, void *arg);
static void pmacWriteAsc( rtems_termios_device_context *base, const char *buffer, size_t nBytes);
static void pmacWriteMbx( rtems_termios_device_context *base, const char *buffer, size_t nBytes);


static PMAC_DEV pmacAscDev[PMAC_MAX_CTLRS];
static PMAC_DEV pmacMbxDev[PMAC_MAX_CTLRS];

IMPORT int       pmacVmeConfigLock;
IMPORT PMAC_CTLR pmacVmeCtlr[PMAC_MAX_CTLRS];

/* TRANSACTION_LOCK introduces a binary semaphore that is taken by the DPRAM
   write routine and given by the read ISR when it sees the end of transaction.
   terminator (ACK). This ensures that there is only one transaction going to
   one PMAC at a time. This makes communication more reliable if there is more
   than one PMAC in the VME chassis */

#define TRANSACTION_LOCK
#ifdef TRANSACTION_LOCK
static epicsEventId transactionLock = NULL;
#endif

/* DISABLE_MBX will disable the VME mailbox comms code which is currently not fully debugged
   and will crash in the interrupt routine.
   The VME mailox buffer is only 16 characters much less than the ASCII DPRAM buffer so
   less efficient, it may not be worth fixing mailbox comms */
#define DISABLE_MBX

/* This is the polling task for DPRAM ASCII communications.  This task is only
   created if the driver is requested to use polling and the default behaviour
   is to use the interrupt service routine.  This task is created in the pmacDrv
   function below.

   Added by Alan Greer on 12th August 2014. */

static void tpmac_poll_task_c(void *ptr)
{
  PMAC_DEV *pDev = (PMAC_DEV *)ptr;

  /* Run this loop forever, checking status as fast as possible */
  while(1)
  {
    /* Call the same routine that would be interrupted */
    pmacAscReadMeISR(&pDev->base);
    /* Make sure other scheduled tasks are allowed to execute */
    epicsThreadSleep(0); // this may be a tick so longer than original vxWorks taskDelay(0)??
  }
}

const rtems_termios_device_handler PMAC_handler_interrupt_Asc =
{
	.first_open = pmacOpen,
	.last_close = pmacClose,
	.poll_read = NULL,
	.write = pmacWriteAsc,
	.set_attributes = pmacSetAttributes,
	.ioctl = pmacIoctl,
	.mode = TERMIOS_IRQ_DRIVEN
};

const rtems_termios_device_handler PMAC_handler_interrupt_Mbx =
{
	.first_open = pmacOpen,
	.last_close = pmacClose,
	.poll_read = NULL,
	.write = pmacWriteMbx,
	.set_attributes = pmacSetAttributes,
	.ioctl = pmacIoctl,
	.mode = TERMIOS_IRQ_DRIVEN
};

/* This routine installs the DPRAM ASCII driver and the Mailbox ASCII driver.
   It adds a DRPAM ASCII device and a Mailbox ASCII device for every PMAC
   card that has been configured.
   This routine is called from "drvPmac_init" in "drvPmac.c"
   which in turn is called from EPICS "iocInit()" */

STATUS pmacDrv(int polling)
{
  STATUS ret;
  int    installedAsc;
  int    installedMbx;
  int    i=0;
  static char   devNameAsc[32];
  static char   devNameMbx[32];
  char   errorString[64];
  long   status;
  rtems_status_code rt_stat;

  ret          = OK;
  installedAsc = FALSE;
  installedMbx = FALSE;

  /* For the DPRAM ASCII driver */

  /* check if driver already installed */

  if (pmacDrvNumAsc > 0)
  {
    installedAsc = TRUE;
    ret          = OK;
  }

  rtems_termios_initialize();
  //rtems_termios_bufsize need large replyQueueSize? (see definition at top of file)
  //  rtems_status_code rtems_termios_bufsize (
  //  size_t cbufsize,     /* cooked buffer size */
  //  size_t raw_input,    /* raw input buffer size */
  //  size_t raw_output    /* raw output buffer size */
  //  );
  rtems_termios_bufsize(1024,replyQueueSize,1024);

  if( !installedAsc )
  {
#ifdef TRANSACTION_LOCK
    if (transactionLock == NULL) transactionLock = epicsEventMustCreate (epicsEventFull);
#endif
    pmacDrvNumAsc++;
    if( PMAC_DRIVER_DEBUG )
      printf( "pmacDrv installAsc\n" );

    /* Add a DPRAM ASCII device for every configured card */
    for( i=0; i < PMAC_MAX_CTLRS; i++ )
    {
      pmacAscDev[i].ctlr       = pmacVmeCtlr[i].ctlr;
      pmacAscDev[i].openFlag   = 0;
      pmacAscDev[i].cancelFlag = 0;
      pmacAscDev[i].polling    = polling;
      pmacAscDev[i].tty        = NULL;

      if( pmacVmeCtlr[i].configured )
      {
        rtems_termios_device_context_initialize(&pmacAscDev[i].base, "PMAC-ASC");

        sprintf( devNameAsc, "/dev/pmacasc%d", pmacVmeCtlr[i].ctlr );
        rt_stat = rtems_termios_device_install(devNameAsc, &PMAC_handler_interrupt_Asc, NULL, &pmacAscDev[i].base);
        if( rt_stat != RTEMS_SUCCESSFUL )
        {
          sprintf( errorString, "pmacDrv: Error adding: /dev/pmacasc%d device (rtems err: %d %s )", pmacVmeCtlr[i].ctlr, rt_stat, rtems_status_text(rt_stat) );
          cantProceed( errorString );
          ret = ERROR;
        }

        pmacAscDev[i].ioReceivedId = 0;
        pmacAscDev[i].readMeISR    = (void *)pmacAscReadMeISR;

        if (polling == 0){
          status = devConnectInterruptVME( pmacVmeCtlr[i].irqVector + 1,
                                        (void *)pmacAscReadMeISR, &pmacAscDev[i].base );
          if( PMAC_DRIVER_DEBUG )
            printf("pmacDrv: &(pmacAscDev[i])=%#010lx\n", (long unsigned int)&pmacAscDev[i].base);

          if(!RTN_SUCCESS(status))
            cantProceed("pmacDrv: Failed to connect to DPRAM ASCII readme interrupt");

          if( PMAC_DRIVER_DEBUG )
            printf ("Enabling interrupt level %d\n", pmacVmeCtlr[i].irqLevel);
          status = devEnableInterruptLevelVME ( pmacVmeCtlr[i].irqLevel);
          if (!RTN_SUCCESS(status))
            cantProceed("pmacDrv: Failure to enable interrupt level.");
        } else {
          /* Create the thread that calls the interrupt service routine when interrupts are not available */
          status = (epicsThreadCreate("tpmac_poll_task",
                                      epicsThreadPriorityLow,
                                      epicsThreadGetStackSize(epicsThreadStackMedium),
                                      (EPICSTHREADFUNC)tpmac_poll_task_c,
                                      &(pmacAscDev[i])) == NULL);
          if (status){
            cantProceed("pmacDrv: Failed to create DPRAM ASCII polling task");
          }
        }
      }
    }
  }

  /* For the Mailbox ASCII driver */

  /* check if driver already installed */

  if(pmacDrvNumMbx > 0)
  {
    installedMbx = TRUE;
    ret          = OK;
  }

  if( !installedMbx )
  {
    pmacDrvNumMbx++;
    if( PMAC_DRIVER_DEBUG )
      printf( "pmacDrv installMbx\n" );

    /* Add a Mailbox ASCII device for every configured card */
    for( i=0; i < PMAC_MAX_CTLRS; i++ )
    {
      pmacMbxDev[i].ctlr       = pmacVmeCtlr[i].ctlr;
      pmacMbxDev[i].openFlag   = 0;
      pmacMbxDev[i].cancelFlag = 0;
      pmacMbxDev[i].polling    = polling;
      pmacMbxDev[i].tty        = NULL;

      if ( pmacVmeCtlr[i].configured )
      {
        rtems_termios_device_context_initialize(&pmacMbxDev[i].base, "PMAC-MBX");

        sprintf( devNameMbx, "/dev/pmacmbx%d", pmacVmeCtlr[i].ctlr );
        rt_stat = rtems_termios_device_install(devNameMbx, &PMAC_handler_interrupt_Mbx, NULL, &pmacMbxDev[i].base);
        if( rt_stat != RTEMS_SUCCESSFUL )
        {
          sprintf( errorString, "pmacDrv: Error adding: /dev/pmacmbx%d device (rtems err: %d %s)", pmacVmeCtlr[i].ctlr, rt_stat, rtems_status_text(rt_stat) );
          cantProceed( errorString );
          ret = ERROR;
        }

        pmacMbxDev[i].ioReceivedId = epicsEventMustCreate( epicsEventEmpty );
        pmacMbxDev[i].readMeISR    = (void *)pmacMbxReadMeISR;

        status = devConnectInterruptVME( pmacVmeCtlr[i].irqVector,
                                      (void *)pmacMbxReadMeISR, &(pmacMbxDev[i].base) );

        if( !RTN_SUCCESS(status) )
          cantProceed("pmacDrv: Failed to connect to Mailbox ASCII readme interrupt");

        status = devConnectInterruptVME( pmacVmeCtlr[i].irqVector - 1,
                                      (void *)pmacMbxReceivedISR, &(pmacMbxDev[i].base) );

        /* Pre-enable responses to commands */
        /* pmacVmeCtlr[i].pBase->mailbox.MB[1].data = 0; */
        if( PMAC_DRIVER_DEBUG )
          printf("pmacDrv: &(pmacMbxDev[i])=%#010lx\n", (long unsigned int)&pmacMbxDev[i].base);

        if( !RTN_SUCCESS(status) )
          cantProceed("pmacDrv: Failed to connect to Mailbox ASCII received interrupt");

        if( PMAC_DRIVER_DEBUG )
          printf ("Enabling interrupt level %d\n", pmacVmeCtlr[i].irqLevel);
        status = devEnableInterruptLevelVME ( pmacVmeCtlr[i].irqLevel);
        if (!RTN_SUCCESS(status))
          cantProceed("pmacDrv: Failure to enable interrupt level.");
      }
    }
  }

  return( ret );
}

/* The routines:                                                      */
/*   pmacOpen                                                         */
/*   pmacClose                                                        */
/*   pmacSetAttributes                                                */
/*   pmacIoctl                                                        */
/*   are generic between the DPRAM ASCII and Mailbox ASCII interfaces */

static bool pmacOpen( rtems_termios_tty *tty, rtems_termios_device_context *base, struct termios *term, rtems_libio_open_close_args_t *args)
{
  PMAC_DEV *pPmacDev  = (PMAC_DEV *) base;
  if( PMAC_DRIVER_DEBUG )
	printf("pmacOpen tty=%#010lx base=%#010lx\n", (long unsigned int)tty, (long unsigned int)base);

  if( pPmacDev->openFlag )
    return FALSE;
  else
  {
    pPmacDev->tty = tty;
    pPmacDev->openFlag = TRUE;
  }
  return TRUE;
}


static void pmacClose( rtems_termios_tty *tty, rtems_termios_device_context *base, rtems_libio_open_close_args_t *args)
{
  PMAC_DEV *pPmacDev  = (PMAC_DEV *) base;
  if( PMAC_DRIVER_DEBUG )
	printf("pmacClose\n");
  if( pPmacDev->openFlag )
  {
    pPmacDev->tty = NULL;
    pPmacDev->openFlag = FALSE;
  }
  return;
}


// dummy function - no attributes to handle
static bool pmacSetAttributes( rtems_termios_device_context *base, const struct termios *term)
{
	PMAC_DEV *pPmacDev __attribute__ ((unused)) = (PMAC_DEV *) base;
    if( PMAC_DRIVER_DEBUG )
	  printf("pmacSetAttributes\n");

	return TRUE;
}

static int pmacIoctl( rtems_termios_device_context *base, ioctl_command_t request, void *arg)
{
  int ret = OK;
  PMAC_DEV   *pPmacDev = (PMAC_DEV *) base;
  if( PMAC_DRIVER_DEBUG )
    printf("pmacIoctl\n");

  switch ( request ) {
    case FIONREAD: {
      /* return number of characters in input buffer */
      *(int*)arg = pPmacDev->tty->ccount; // from cooked buffer???
      // maybe this should look at struct rtems_termios_rawbuf pPmacDev->tty->rawInBuf head - tail ???
      }
      break;
/* Following vxWorks code not used, replyQ replaced with termios buffer
    case FIORFLUSH:
      epicsRingBytesFlush( pPmacDev->replyQ );
      break;

    case FIOCANCEL:
      pPmacDev->cancelFlag = TRUE;
      epicsEventSignal( pPmacDev->ioReadmeId );
      break; */
    default:
      rtems_set_errno_and_return_minus_one( EINVAL );
  }

  return( ret );
}


/* The write and ISR routines are specific to the */
/* DPRAM ASCII and Mailbox ASCII interfaces       */
static void pmacWriteAsc( rtems_termios_device_context *base, const char *buffer, size_t nBytes)
{
  int        i;
  int        ctlr;
  int        numWritten;
  static int totalWritten = 0;
  PMAC_DPRAM *dpramAsciiOut;
  PMAC_DPRAM *dpramAsciiOutControl;
  PMAC_DEV   *pDev = (PMAC_DEV *) base;

  ctlr                 = pDev->ctlr;
  dpramAsciiOut        = pmacRamAddr(ctlr,0x0EA0);
  dpramAsciiOutControl = pmacRamAddr(ctlr,0x0E9C);
  i                    = 0;
  numWritten           = 0;

#ifdef TRANSACTION_LOCK
  epicsEventWaitWithTimeout( transactionLock, 0.3 );
#endif
  if( PMAC_DRIVER_DEBUG )
    printf( "pmacWriteAsc: nBytes=%d term=0x%x buf=[%.*s]\n", nBytes, buffer[nBytes-1], nBytes, buffer );

  for( i=0; (i < nBytes); i++ )
  {
    if( totalWritten == 0 )
    {
      /* Ensure pmac dpram out is ready */
      int count = 0;
      const double delay = epicsThreadSleepQuantum();

      while( getReg( *dpramAsciiOutControl ) != 0x0 )
      {
        epicsThreadSleep(delay);
        count++;
        if( count > 10 )
          printf( "pmacWriteAsc: Stuck in while loop\n" );
      }
    }

    if( buffer[i] == '\n') /* old tpmac pmacController.cpp used to use \r */
    {
      /* Send command to PMAC (replacing end of line char with a null char) */
      //printf( "pmacWriteAsc: Send command\n" );
      setReg( dpramAsciiOut[totalWritten], (char) 0 );
      setReg( *dpramAsciiOutControl, (char) 1 );
      totalWritten = 0;
    }
    else
    {
      setReg( dpramAsciiOut[totalWritten], buffer[i] );
      totalWritten++;
      if (totalWritten==PMAC_BASE_ASC_REGS_OUT)
      {
        /* dpram buffer full so send it to the pmac */
        setReg( *dpramAsciiOutControl, (char) 1 );
        totalWritten = 0;
      }
    }
    numWritten++;
  }
  if (numWritten > 0)
    rtems_termios_dequeue_characters(pDev->tty, numWritten);
  return;
}


/* pmacAscReadMeISR - Interrupt Service Routine which is called when
                      PMAC issues an interrupt to tell us the DPRAM ASCII buffer
                      can be read.
                      Note: There is 1 interrupt per line of the response and
                      1 interrupt for the ACK at the end. */

static void pmacAscReadMeISR( rtems_termios_device_context *base )
{
  int         i;
  int         ctlr;
  rtems_status_code rt_stat;
  int         length;
  volatile epicsUInt16 *dpramAsciiInControl;
  PMAC_DPRAM  *dpramAsciiIn;
  union {epicsUInt16 S; char C[2];} control;
  PMAC_DEV   *pPmacDev = (PMAC_DEV *) base;

  ctlr                = pPmacDev->ctlr;
  dpramAsciiInControl = (volatile epicsUInt16 *) pmacRamAddr(ctlr, 0x0F40);
  dpramAsciiIn        = pmacRamAddr(ctlr, 0x0F44);
  control.S           = getReg (*dpramAsciiInControl);

  if (control.S == 0)
  {
      if (pPmacDev->polling == 0){
        printk( "No response from PMAC in pmacAscReadMeISR\n" );
      }
      return;
  }

  /* Added check ctx_tty!=0 (i.e.device opened) before using enqueue */
  /* Only fill the termios buffer if the device has been opened */
  if (pPmacDev->tty)
  {
    if (control.C[1] == 0 )
    {
      //printk("pmacAscReadMeISR...1\n");
      length = getReg( *pmacRamAddr(ctlr, 0x0F42) ) - 1;
      for( i=0; i<length; i++ )
      {
        char c = getReg(*dpramAsciiIn);
        rt_stat = rtems_termios_enqueue_raw_characters(pPmacDev->tty, &c, 1);
        if( rt_stat != RTEMS_SUCCESSFUL ) printk("PMAC termios buffer full\n");
        //printk("PMAC rx char 0x%x\n",c);
        dpramAsciiIn++;
      }
      rt_stat = rtems_termios_enqueue_raw_characters(pPmacDev->tty, &(control.C[0]), 1);
      if( rt_stat != RTEMS_SUCCESSFUL ) printk("PMAC reply termios buffer full\n");
      //printk("PMAC reply char 0x%x\n",control.C[0]);
    }
    else
    {
      /* Build a "ERRnnn" string from the BCD error code in dpramAsciiInControl */
      char response[]={PMAC_TERM_BELL,'E','R','R','0','0','0',PMAC_TERM_CR,PMAC_TERM_ACK};

      /* Convert the BCD encoded error number to its ASCII equivalent */
      response[4] += ((control.C[1])       & 0xF );
      response[5] += ((control.C[0] >> 4 ) & 0xF );
      response[6] += ((control.C[0] )      & 0xF );
      printk("PMAC err reply %s\n",response);

      /* Push the data the onto the ring buffer */
      rt_stat = rtems_termios_enqueue_raw_characters(pPmacDev->tty, response, sizeof(response) );
      if( rt_stat != RTEMS_SUCCESSFUL ) printk("PMAC reply ring buffer full\n");
    }
  }
  setReg( *dpramAsciiInControl, (epicsUInt16) 0 );

#ifdef TRANSACTION_LOCK
  if ( control.C[0] == PMAC_TERM_ACK ) epicsEventSignal( transactionLock );
#endif

  return;
}


static void pmacWriteMbx( rtems_termios_device_context *base, const char *buffer, size_t nBytes)
{
  int       i;
  int       j;
  int       numWritten;
  int       ctlr;
  char      firstChar, c;
  PMAC_CTLR *pPmacCtlr;
  PMAC_DEV   *pPmacDev = (PMAC_DEV *) base;

  j          = 0;
  numWritten = 0;
  ctlr       = pPmacDev->ctlr;
  pPmacCtlr  = &pmacVmeCtlr[ctlr];

  if( PMAC_DRIVER_DEBUG )
    printf( "pmacWriteMbx: nBytes=%d term=0x%x buffer=[%.*s]\n", nBytes, buffer[nBytes-1], nBytes, buffer );

  while( numWritten < nBytes )
  {
    firstChar = buffer[j];
    for( i = 1; i < PMAC_BASE_MBX_REGS_OUT; i++ )
    {
      if( PMAC_DRIVER_DEBUG )
        printf("pmacWriteMbx: 0x%x (%d)\n", buffer[j+i], i);
      pPmacCtlr->pBase->mailbox.MB[i+1].data = buffer[j+i];
      numWritten++;
      /* lowLevelPortConnect in pmacMessageBroker.cpp specifies '\n' for end of command line which works for
         dpram but pmac mailbox seems to need '\r' so we change \n to \r before sending it to the pmac.
         (At some point in the past pmacMessageBroker must have specified \r...) */
      //if( buffer[j+i] == PMAC_TERM_CR )
      if( buffer[j+i] == '\n' )
      {
        pPmacCtlr->pBase->mailbox.MB[i+1].data = PMAC_TERM_CR;
        break;
      }
    }

    if( PMAC_DRIVER_DEBUG )
      printf("pmacWriteMbx: 0x%x (0)\n", firstChar);
    pPmacCtlr->pBase->mailbox.MB[0].data = firstChar;
    numWritten++;

    /* record number of characters written to the mailbox */
    pPmacDev->lastsent = (i<PMAC_BASE_MBX_REGS_OUT) ? i+1 : i;

    /* Now, enable the response, and read the data back to ensure it is written */
    pPmacCtlr->pBase->mailbox.MB[1].data = 0;
    c = pPmacCtlr->pBase->mailbox.MB[1].data;

    /* wait for pmacMbxReceivedISR to confirm reception of command */
    epicsEventWait( pPmacDev->ioReceivedId );

    rtems_termios_dequeue_characters(pPmacDev->tty, pPmacDev->lastsent);

    if( PMAC_DRIVER_DEBUG )
      printf( "pmacWriteMbx: dequeued\n");

    j += PMAC_BASE_MBX_REGS_OUT;
  }

  return;
}


/* We get this interrupt when PMAC has filled the Mailbox registers */
/* and we can then read the data                                    */
/* 4/9/24: THIS FUNCTION NEEDS DEBUGGING 
   Currently VME Mailbox transmit to pmac works but the receive does not.
   The interrupt does call this function but the #ifdef code block below crashes */
static void pmacMbxReadMeISR( rtems_termios_device_context *base )
{
#ifdef DISABLE_MBX
  printk("Inside pmacMbxReadMeISR...\n");

#else
  int       i;
  int       ctlr;
  //int       pushOK;
  rtems_status_code rt_stat;
  char	    c;
  PMAC_CTLR *pPmacCtlr;
  int        terminator=0;
  PMAC_DEV   *pPmacDev = (PMAC_DEV *) base;

  /* logMsg("Inside pmacMbxReadMeISR...\n", 0, 0, 0, 0, 0, 0); */
printk("Inside pmacMbxReadMeISR...\n");
  ctlr      = pPmacDev->ctlr;
  pPmacCtlr = &pmacVmeCtlr[ctlr];

  /* Added check ctx_tty!=0 (i.e.device opened) before using enqueue */
  /* Only fill the termios buffer if the device has been opened */
  if (pPmacDev->tty)
  {
    for( i = 0; i < PMAC_BASE_MBX_REGS_IN && !terminator; i++ )
    {
      c = pPmacCtlr->pBase->mailbox.MB[i].data;
printk("PMAC reply char 0x%x\n",c);

      rt_stat = rtems_termios_enqueue_raw_characters(pPmacDev->tty, &c, 1);
      if( rt_stat != RTEMS_SUCCESSFUL ) printk("PMAC reply termios buffer full\n");

      terminator = ( (c == PMAC_TERM_CR) || (c == PMAC_TERM_ACK) || (c == PMAC_TERM_BELL) );
      if (terminator)
      {
        static int hadBell = 0;
        int bell = (c == PMAC_TERM_BELL);

        /* Add an ACK to the first terminator after a BELL to make parsing easier */
        if (hadBell) 
        {
            c = PMAC_TERM_ACK;
            rt_stat = rtems_termios_enqueue_raw_characters(pPmacDev->tty, &c, 1);
            if( rt_stat != RTEMS_SUCCESSFUL )  printk("PMAC reply termios buffer full\n");
        }
        hadBell = bell;
      }
    }

    /* Write to mailbox register number 1 if there is more data in the response
     to this command. According to the manual we should be able to do this
     after receiving any response to pre-enable the next response, but in
     reality, this doesn't always work */

    if (c != PMAC_TERM_ACK) 
    {
        pPmacCtlr->pBase->mailbox.MB[1].data = 0;
        c = pPmacCtlr->pBase->mailbox.MB[1].data;
    }
  }
#endif /* DISABLE_MBX */
  return;
}


/* We get this interrupt when PMAC has successfully received the data */
/* we have placed in the Mailbox registers                            */
static void pmacMbxReceivedISR( rtems_termios_device_context *base )
{
  PMAC_DEV   *pPmacDev = (PMAC_DEV *) base;

  /* logMsg("Inside pmacMbxReceivedISR...\n", 0, 0, 0, 0, 0, 0); */
//printk("Inside pmacMbxReceivedISR...base=%#010lx base2=%#010lx, n=%d\n",base, base2, pPmacDev->lastsent);

  /* dequeue the characters that have just been received by the pmac */
  /* moved this to pmacWriteMbx because it causes a crash here - it shouldnt - not sure why */
  //if (pPmacDev->tty) rtems_termios_dequeue_characters(pPmacDev->tty, pPmacDev->lastsent);

  epicsEventSignal( pPmacDev->ioReceivedId );
//printk("Signalled pmacMbxReceivedISR...\n");
  return;
}


/* This is a test routine for testing Open/Close of multiple PMAC devices
   without having PMAC cards in the crate */

long pmacVmeConfigSim( int ctlrNumber, unsigned long addrBase, unsigned long addrDpram,
                       unsigned int irqVector, unsigned int irqLevel )
{
  PMAC_CTLR *pPmacCtlr;
	
  if( pmacVmeConfigLock != 0 )
  {
    printf( "pmacVmeConfigSim: Cannot change configuration after initialization\n" );
    return(ERROR);
  }
  	
  if( (ctlrNumber < 0) | (ctlrNumber >= PMAC_MAX_CTLRS) )
  {
    printf( "pmacVmeConfigSim: Controller number %d invalid -- must be 0 to %d.\n",
             ctlrNumber, PMAC_MAX_CTLRS-1 );
    return(ERROR);
  }
  
  if( pmacVmeCtlr[ctlrNumber].configured )
  {
    printf( "pmacVmeConfigSim: Controller %d already configured -- request ignored.\n",
            ctlrNumber );
    return(ERROR);
  }
	
  pPmacCtlr                = &pmacVmeCtlr[ctlrNumber];
  pPmacCtlr->ctlr          = ctlrNumber;
  pPmacCtlr->vmebusBase    = addrBase;
  pPmacCtlr->irqVector     = irqVector;
  pPmacCtlr->irqLevel      = irqLevel;
  pPmacCtlr->enabled       = FALSE;
  pPmacCtlr->present       = FALSE;
  pPmacCtlr->active        = FALSE;
  pPmacCtlr->enabledBase   = TRUE;
  pPmacCtlr->presentBase   = TRUE;
  pPmacCtlr->activeBase    = FALSE;
  pPmacCtlr->enabledDpram  = TRUE;
  pPmacCtlr->presentDpram  = TRUE;
  pPmacCtlr->activeDpram   = FALSE;
  pPmacCtlr->enabledGather = TRUE;
  pPmacCtlr->activeGather  = FALSE;
  pPmacCtlr->vmebusDpram   = addrDpram;
  if( addrDpram == 0 )
    pPmacCtlr->enabledDpram = FALSE;

  pPmacCtlr->present    = pPmacCtlr->presentBase | pPmacCtlr->presentDpram;
  pPmacCtlr->enabled    = pPmacCtlr->enabledBase | pPmacCtlr->enabledDpram;
  pPmacCtlr->configured = TRUE;
	
  return(0);
}


#define PMAC_ASYN
#ifdef PMAC_ASYN
#include "asynDriver.h"
#include "drvAsynSerialPort.h"
#include <epicsExport.h>
#include <iocsh.h>

int pmacAsynConfig( char * mbx_prefix, char * asc_prefix, unsigned int priority, int polling)
{
    int i;
    char devName[32];
    char asynName[32];
    static int installedAsynAsc = 0;
    static int installedAsynMbx = 0;

    pmacDrv(polling);

#ifdef DISABLE_MBX
    /* Mailbox (mbx_prefix) communication interrupts currently cause a crash so mailbox comms is disabled here.*/
    /* Note DPRAM (asc_prefix) comms is preferable, its faster using a much bigger buffer compared to a mailbox.*/
    if( strlen(mbx_prefix)>0 )
    {
        printf( "VME mailbox communication (%s) not debugged.\n", mbx_prefix );
        printf( "Use a DPRAM ASCII port instead (e.g. pmacAsynConfig( \"\", \"PMAC_S\", 0, 0) )\n" );
        cantProceed("pmacAsynConfig: MBX comms disabled.");
    }
#endif /* DISABLE_MBX */

    if( !installedAsynMbx && strlen(mbx_prefix)>0 )
    {
        /* Add a MBX ASCII device for every configured card */
        for( i=0; i < PMAC_MAX_CTLRS; i++ )
        {
            if( pmacVmeCtlr[i].configured )
            {
                //sprintf( devName,  "/dev/pmac/%d/mbx", pmacVmeCtlr[i].ctlr );
                sprintf( devName,  "/dev/pmacmbx%d", pmacVmeCtlr[i].ctlr );
                sprintf( asynName, "%s%d", mbx_prefix, pmacVmeCtlr[i].ctlr );
                drvAsynSerialPortConfigure( asynName, devName, priority, 0, 0 );
                printf("Adding MBX ASCII port %s\n",asynName);
            }
        }
        installedAsynMbx = 1;
    }

    if( !installedAsynAsc && strlen(asc_prefix)>0 )
    {
        /* Add a DPRAM ASCII device for every configured card */
        for( i=0; i < PMAC_MAX_CTLRS; i++ )
        {
            if( pmacVmeCtlr[i].configured )
            {
                //sprintf( devName,  "/dev/pmac/%d/asc", pmacVmeCtlr[i].ctlr );
                sprintf( devName,  "/dev/pmacasc%d", pmacVmeCtlr[i].ctlr );
                sprintf( asynName, "%s%d", asc_prefix, pmacVmeCtlr[i].ctlr );
                drvAsynSerialPortConfigure( asynName, devName, priority, 0, 0 );
                printf("Adding DPRAM ASCII port %s\n",asynName);
            }
        }
        installedAsynAsc = 1;
    }

    return 0;
}

static const iocshArg pmacAsynConfigArg0 = {"PMAC Mailbox Asyn port prefix",     iocshArgString};
static const iocshArg pmacAsynConfigArg1 = {"PMAC DPRAM ASCII Asyn port prefix", iocshArgString};
static const iocshArg pmacAsynConfigArg2 = {"Asyn port priority (0 for default)", iocshArgInt};
static const iocshArg pmacAsynConfigArg3 = {"Should the driver poll (0=no, 1=yes)", iocshArgInt};
static const iocshArg * const pmacAsynConfigArgs[] = {&pmacAsynConfigArg0, &pmacAsynConfigArg1, &pmacAsynConfigArg2, &pmacAsynConfigArg3};
 
static const iocshFuncDef pmacAsynConfigDef = {"pmacAsynConfig", 4, pmacAsynConfigArgs};

static void pmacAsynConfigCallFunc(const iocshArgBuf *args)
{
    pmacAsynConfig(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}


static void pmacAsynConfigRegister(void)
{
    iocshRegister(&pmacAsynConfigDef,  pmacAsynConfigCallFunc);
}

epicsExportRegistrar(pmacAsynConfigRegister);

#endif
