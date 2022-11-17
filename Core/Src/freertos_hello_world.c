/*
    Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
    Copyright (c) 2012 - 2020 Xilinx, Inc. All Rights Reserved.
        SPDX-License-Identifier: MIT


    http://www.FreeRTOS.org
    http://aws.amazon.com/freertos


    1 tab == 4 spaces!
*/

#define POSIX_VERSION 1

#if POSIX_VERSION

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h" /* To start the scheduler */
/* POSIX includes */
#include "FreeRTOS_POSIX/fcntl.h"
#include "FreeRTOS_POSIX/mqueue.h"
#include "FreeRTOS_POSIX/pthread.h"
#include "FreeRTOS_POSIX/time.h"
#include "FreeRTOS_POSIX/unistd.h"
#include "main.h"
#include "string.h"

#define DELAY_10_SECONDS 10000UL
#define DELAY_1_SECOND 1000UL
#define TIMER_CHECK_THRESHOLD 9
/*-----------------------------------------------------------*/

/* The Tx and Rx tasks as described at the top of this file. */
static void *prvTxTask(void *pvParameters);
static void *prvRxTask(void *pvParameters);
static void vTimerCallback(union sigval pxTimer);
/*-----------------------------------------------------------*/

/* The queue used by the Tx and Rx tasks, as described at the top of this
file. */
static pthread_t xTxTask;
static pthread_attr_t xTxAttr;
static pthread_t xRxTask;
static pthread_attr_t xRxAttr;
static mqd_t xQueue = NULL;
static struct mq_attr xQueueAttr = {
    0,
};
const char *xQueueName = "/posix_mq";
static timer_t xTimer = NULL;
static struct sigevent xSev = {
    0,
};
static struct itimerspec xTrigger = {
    0,
};
const char *HWstring = "Hello World";
long RxtaskCntr = 0;

void prvMainPosixTask(void *opaque);

/* Main with POSIX functions */
void prvMainPosixTask(void *opaque) {
  int iret = 0;

  HAL_GPIO_WritePin(
      LD2_GPIO_Port, LD2_Pin,
      GPIO_PIN_SET);  // xil_printf( "Hello from Freertos example main\r\n" );

  /* Create the queue before launching the tasks because the scheduler is
   * already running */
  xQueueAttr.mq_maxmsg = 1; /* There is only one space in the queue. */
  xQueueAttr.mq_msgsize = sizeof(HWstring); /* Each space in the queue is large
                                               enough to hold the string. */

  xQueue = mq_open(
      xQueueName,       /* Message queue name must start with / */
      O_RDWR | O_CREAT, /* Read and Write. Create queue if it doesn't exist. */
      0,            /* the "mode" parameter is unsupported by FreeRTOS+POSIX */
      &xQueueAttr); /* The attributes for the queue */

  /* Check the queue was created. */
  configASSERT(xQueue);

  /* Create the two tasks.  The Tx task is given a lower priority than the
  Rx task, so the Rx task will leave the Blocked state and pre-empt the Tx
  task as soon as the Tx task places an item in the queue. */
  iret = pthread_attr_init(&xTxAttr);
  configASSERT(!iret);
  iret = pthread_attr_init(&xRxAttr);
  configASSERT(!iret);

  struct sched_param xTxSchedParam = {
      .sched_priority = sched_get_priority_min(0),
  }; /* The task runs at the idle priority */
  struct sched_param xRxSchedParam = {
      .sched_priority = sched_get_priority_min(0) + 1,
  }; /* The task runs at a higher priority */

  iret = pthread_attr_setschedparam(&xTxAttr, &xTxSchedParam);
  configASSERT(!iret);
  iret = pthread_attr_setschedparam(&xRxAttr, &xRxSchedParam);
  configASSERT(!iret);

  iret = pthread_create(
      &xTxTask, &xTxAttr, /* The thread attributes. */
      prvTxTask,          /* The function that implements the thread. */
      NULL);              /* The task parameter is not used, so set to NULL. */
  configASSERT(!iret);

  iret = pthread_create(
      &xRxTask, &xRxAttr, /* The thread attributes. */
      prvRxTask,          /* The function that implements the thread. */
      NULL);              /* The task parameter is not used, so set to NULL. */
  configASSERT(!iret);

  /* Create a timer with a timer expiry of 10 seconds. The timer would expire
   after 10 seconds and the timer call back would get called. In the timer call
   back checks are done to ensure that the tasks have been running properly till
   then. The threads are joined in the timer call back and a message is printed
   to convey that the example has run successfully. The timer expiry is set to
   10 seconds and the timer set to not auto reload. */
  xSev.sigev_notify = SIGEV_THREAD; /* Must be SIGEV_THREAD since signals are
                                       currently not supported */
  xSev.sigev_notify_function = vTimerCallback; /* Callback function */
  xSev.sigev_value.sival_ptr = &xTimer;        /* Timer ID */

  iret = timer_create(0, /* the "clock_id" parameter is ignored as this function
                            uses the FreeRTOS tick count as its clock */
                      &xSev, &xTimer);

  /* Check the timer was created. */
  configASSERT(!iret);
  configASSERT(xTimer);

  /* start the timer with a block time of 0 ticks. This means as soon
     as the schedule starts the timer will start running and will expire after
     10 seconds */
  xTrigger.it_value.tv_sec =
      10; /* Set the expiration 10 seconds in the future */

  iret = timer_settime(xTimer,    /* The timer to be armed */
                       0,         /* No flags */
                       &xTrigger, /* Trigger in 10 seconds */
                       NULL);     /* No old value needed */
  configASSERT(!iret);

  /* This task was created with the native xTaskCreate() API function, so
  must not run off the end of its implementing thread. */
  vTaskDelete(NULL);
}

/*-----------------------------------------------------------*/
static void *prvTxTask(void *pvParameters) {
  int iret = 0;

  for (;;) {
    /* Delay for 1 second. */
    sleep(1);

    /* Send the next value on the queue. */
    iret = mq_send(xQueue,           /* The queue being written to. */
                   HWstring,         /* The address of the data being sent. */
                   sizeof(HWstring), /* The size of the data being sent. */
                   0);               /* The message priority. */

    if (iret) {
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin,
                        GPIO_PIN_RESET);  // xil_printf( "Tx task could not send
                                          // message to queue\r\n" );
      break;
    }
  }

  return NULL;
}

/*-----------------------------------------------------------*/
static void *prvRxTask(void *pvParameters) {
  char Recdstring[15] = "";
  int size = 0;

  for (;;) {
    /* Block to wait for data arriving on the queue. */
    size = mq_receive(xQueue,             /* The queue being read. */
                      Recdstring,         /* Data is read into this address. */
                      sizeof(Recdstring), /* Size of destination buffer. */
                      NULL);              /* The message priority. */

    if (size <= 0) {
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin,
                        GPIO_PIN_RESET);  // xil_printf( "Rx task could not
                                          // receive message from queue\r\n" );
      break;
    }

    if (strncmp(Recdstring, "stop", 15) == 0) {
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin,
                        GPIO_PIN_RESET);  // xil_printf( "Rx task was told to
                                          // stop receiving messages\r\n" );
      break;
    }

    /* Print the received data. */
    HAL_GPIO_TogglePin(LD3_GPIO_Port,
                       LD3_Pin);  // xil_printf( "Rx task received string from
                                  // Tx task: %s\r\n", Recdstring );
    RxtaskCntr++;
  }

  return NULL;
}

/*-----------------------------------------------------------*/
static void vTimerCallback(union sigval pxTimer) {
  int iret = 0;
  configASSERT(pxTimer.sival_ptr);

  /* Cannot check the timer ID the same way as FreeRTOS, because the ID is
   * assigned by timer_create() */

  /* If the RxtaskCntr is updated every time the Rx task is called. The
   Rx task is called every time the Tx task sends a message. The Tx task
   sends a message every 1 second.
   The timer expires after 10 seconds. We expect the RxtaskCntr to at least
   have a value of 9 (TIMER_CHECK_THRESHOLD) when the timer expires. */
  if (RxtaskCntr >= TIMER_CHECK_THRESHOLD) {
    // xil_printf("Successfully ran FreeRTOS Hello World Example\r\n");
    HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
  } else {
    HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
    // xil_printf("FreeRTOS Hello World Example FAILED\r\n");
  }

  /* Join the threads */

  /* Send a message to the Rx thread so it can finish gracefully */
  iret = mq_send(xQueue,           /* The queue being written to. */
                 "stop",           /* The address of the data being sent. */
                 sizeof(HWstring), /* The size of the data being sent. */
                 0);               /* The message priority. */
  configASSERT(!iret);
  pthread_join(xRxTask, NULL);
  // xil_printf("Rx thread joined\r\n");

  /* Closing the message queue will make the Tx thread stop (but not wake the
   * blocked Rx task) */
  mq_close(xQueue);
  mq_unlink(xQueueName);
  pthread_join(xTxTask, NULL);
  // xil_printf("Tx thread joined\r\n");
}

#else

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"
/* Xilinx includes. */
#include "xil_printf.h"
#include "xparameters.h"

#define TIMER_ID 1
#define DELAY_10_SECONDS 10000UL
#define DELAY_1_SECOND 1000UL
#define TIMER_CHECK_THRESHOLD 9
/*-----------------------------------------------------------*/

/* The Tx and Rx tasks as described at the top of this file. */
static void prvTxTask(void *pvParameters);
static void prvRxTask(void *pvParameters);
static void vTimerCallback(TimerHandle_t pxTimer);
/*-----------------------------------------------------------*/

/* The queue used by the Tx and Rx tasks, as described at the top of this
file. */
static TaskHandle_t xTxTask;
static TaskHandle_t xRxTask;
static QueueHandle_t xQueue = NULL;
static TimerHandle_t xTimer = NULL;
char HWstring[15] = "Hello World";
long RxtaskCntr = 0;

int main(void) {
  const TickType_t x10seconds = pdMS_TO_TICKS(DELAY_10_SECONDS);

  xil_printf("Hello from Freertos example main\r\n");

  /* Create the two tasks.  The Tx task is given a lower priority than the
  Rx task, so the Rx task will leave the Blocked state and pre-empt the Tx
  task as soon as the Tx task places an item in the queue. */
  xTaskCreate(prvTxTask,          /* The function that implements the task. */
              (const char *)"Tx", /* Text name for the task, provided to assist
                                     debugging only. */
              configMINIMAL_STACK_SIZE, /* The stack allocated to the task. */
              NULL, /* The task parameter is not used, so set to NULL. */
              tskIDLE_PRIORITY, /* The task runs at the idle priority. */
              &xTxTask);

  xTaskCreate(prvRxTask, (const char *)"GB", configMINIMAL_STACK_SIZE, NULL,
              tskIDLE_PRIORITY + 1, &xRxTask);

  /* Create the queue used by the tasks.  The Rx task has a higher priority
  than the Tx task, so will preempt the Tx task and remove values from the
  queue as soon as the Tx task writes to the queue - therefore the queue can
  never have more than one item in it. */
  xQueue = xQueueCreate(1, /* There is only one space in the queue. */
                        sizeof(HWstring)); /* Each space in the queue is large
                                              enough to hold a uint32_t. */

  /* Check the queue was created. */
  configASSERT(xQueue);

  /* Create a timer with a timer expiry of 10 seconds. The timer would expire
   after 10 seconds and the timer call back would get called. In the timer call
   back checks are done to ensure that the tasks have been running properly till
   then. The tasks are deleted in the timer call back and a message is printed
   to convey that the example has run successfully. The timer expiry is set to
   10 seconds and the timer set to not auto reload. */
  xTimer = xTimerCreate((const char *)"Timer", x10seconds, pdFALSE,
                        (void *)TIMER_ID, vTimerCallback);
  /* Check the timer was created. */
  configASSERT(xTimer);

  /* start the timer with a block time of 0 ticks. This means as soon
     as the schedule starts the timer will start running and will expire after
     10 seconds */
  xTimerStart(xTimer, 0);

  /* Start the tasks and timer running. */
  vTaskStartScheduler();

  /* If all is well, the scheduler will now be running, and the following line
  will never be reached.  If the following line does execute, then there was
  insufficient FreeRTOS heap memory available for the idle and/or timer tasks
  to be created.  See the memory management section on the FreeRTOS web site
  for more details. */
  for (;;)
    ;
}

/*-----------------------------------------------------------*/
static void prvTxTask(void *pvParameters) {
  const TickType_t x1second = pdMS_TO_TICKS(DELAY_1_SECOND);

  for (;;) {
    /* Delay for 1 second. */
    vTaskDelay(x1second);

    /* Send the next value on the queue.  The queue should always be
    empty at this point so a block time of 0 is used. */
    xQueueSend(xQueue,   /* The queue being written to. */
               HWstring, /* The address of the data being sent. */
               0UL);     /* The block time. */
  }
}

/*-----------------------------------------------------------*/
static void prvRxTask(void *pvParameters) {
  char Recdstring[15] = "";

  for (;;) {
    /* Block to wait for data arriving on the queue. */
    xQueueReceive(xQueue,         /* The queue being read. */
                  Recdstring,     /* Data is read into this address. */
                  portMAX_DELAY); /* Wait without a timeout for data. */

    /* Print the received data. */
    xil_printf("Rx task received string from Tx task: %s\r\n", Recdstring);
    RxtaskCntr++;
  }
}

/*-----------------------------------------------------------*/
static void vTimerCallback(TimerHandle_t pxTimer) {
  long lTimerId;
  configASSERT(pxTimer);

  lTimerId = (long)pvTimerGetTimerID(pxTimer);

  if (lTimerId != TIMER_ID) {
    xil_printf("FreeRTOS Hello World Example FAILED");
  }

  /* If the RxtaskCntr is updated every time the Rx task is called. The
   Rx task is called every time the Tx task sends a message. The Tx task
   sends a message every 1 second.
   The timer expires after 10 seconds. We expect the RxtaskCntr to at least
   have a value of 9 (TIMER_CHECK_THRESHOLD) when the timer expires. */
  if (RxtaskCntr >= TIMER_CHECK_THRESHOLD) {
    xil_printf("Successfully ran FreeRTOS Hello World Example");
  } else {
    xil_printf("FreeRTOS Hello World Example FAILED");
  }

  vTaskDelete(xRxTask);
  vTaskDelete(xTxTask);
}

#endif
