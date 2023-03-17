/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32f4_discovery.h"
/* Kernel includes. */
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"
#include "../inc/stm32f4xx_rcc.h"

#define mainQUEUE_LENGTH 100

#define TASK1_EXECUTION_TIME 95
#define TASK2_EXECUTION_TIME 150
#define TASK3_EXECUTION_TIME 250
#define TASK1_PERIOD 500
#define TASK2_PERIOD 500
#define TASK3_PERIOD 750

// Enum definitions
enum message_type
{
	RELEASE_DD_TASK,
	COMPLETE_DD_TASK,
	GET_ACTIVE_DD_TASK_LIST,
	GET_COMPLETED_DD_TASK_LIST,
	GET_OVERDUE_DD_TASK_LIST
};

enum task_type
{
	PERIODIC,
	APERIODIC
};

// Struct definitions
typedef struct dd_task
{
	TaskHandle_t t_handle;
	enum task_type type;
	uint32_t task_id;
	TickType_t release_time;
	TickType_t absolute_deadline;
	TickType_t completion_time;
} dd_task;

typedef struct dd_task_list
{
	dd_task task;
	struct dd_task_list *next_task;
} dd_task_list;

typedef struct queue_message
{
	enum message_type type;
	dd_task *parameters;
} queue_message;

typedef struct user_defined_parameters
{
	uint16_t execution_time;
	uint16_t period;
	uint32_t task_id;
} user_defined_parameters;


// Return maximum value of two numbers
#define max(a,b) \
	({ __typeof__ (a) _a = (a); \
    	__typeof__ (b) _b = (b); \
    	_a > _b ? _a : _b; })

// Return minimum value of two numbers
 #define min(a,b) \
	({ __typeof__ (a) _a = (a); \
    	__typeof__ (b) _b = (b); \
    	_a < _b ? _a : _b; })

// Function declarations
static void UserDefined_Task( void *pvParameters );
static void Generator_Task( void *pvParameters );


void create_dd_task(
		TaskHandle_t t_handle,
		enum task_type type,
		uint32_t task_id,
		uint32_t absolute_deadline
);
void delete_dd_task(uint32_t task_id);
dd_task_list** get_active_dd_task_list(void);
dd_task_list** get_complete_dd_task_list(void);
dd_task_list** get_overdue_dd_task_list(void);
void init_task_array(TaskHandle_t *task_array[3], user_defined_parameters *user_defined_tasks[3]);
void init_user_defined_task_parameters(user_defined_parameters *user_defined_tasks[3]);


// Queue declarations
xQueueHandle xQueue_message_handle = 0;


int main(void)
{

	// Create the queues
	xQueue_message_handle = xQueueCreate(mainQUEUE_LENGTH, sizeof(queue_message));

	// Add the queues to the registry
	vQueueAddToRegistry(xQueue_message_handle, "MessageQueue");

	// Create the three tasks used in the program
	xTaskCreate(Generator_Task, "Generator", configMINIMAL_STACK_SIZE, NULL, 2, NULL);

	/* Start the tasks and timer running. */
	vTaskStartScheduler();

    return 0;

}

static void UserDefined_Task ( void *pvParameters )
{
	user_defined_parameters *parameters = (user_defined_parameters *) pvParameters;
	TickType_t start_ticks = xTaskGetTickCount();

	while (xTaskGetTickCount() - start_ticks < parameters->execution_time / portTICK_PERIOD_MS) {};

	queue_message *message = pvPortMalloc( sizeof(queue_message) );
	message->type = COMPLETE_DD_TASK;
	dd_task *message_parameters = pvPortMalloc (sizeof(dd_task) );
	message_parameters->task_id = parameters->task_id;
	message->parameters = message_parameters;

	if(xQueueSend(xQueue_message_handle, &message, 1000))
	{
		printf("User Defined Task Failed!\n");
		fflush(stdout);
	}
}

static void Generator_Task ( void *pvParameters )
{
	uint8_t task_index = 0;
	user_defined_parameters *user_defined_tasks[3];
	init_user_defined_task_parameters(user_defined_tasks);
	uint16_t task_id = 0;
	TaskHandle_t *task_array[3];
	init_task_array(task_array, user_defined_tasks);

	TickType_t sleep_times[3];
	for (int i = 0; i < 3; ++i)
	{
		sleep_times[i] = xTaskGetTickCount();
	}

	while(1)
	{
		// Create dd_task
		uint8_t cur_task_index = task_index++;
		dd_task *cur_task = pvPortMalloc( sizeof (dd_task) );
		cur_task->task_id = task_id++;
		cur_task->type = PERIODIC;
		cur_task->release_time = xTaskGetTickCount();
		cur_task->absolute_deadline = cur_task->release_time +
									(user_defined_tasks[cur_task_index % 3]->period / portTICK_PERIOD_MS);
		cur_task->t_handle = task_array[task_index % 3];
		sleep_times[task_index % 3] = cur_task->absolute_deadline;

		//Send message
		queue_message *message = pvPortMalloc( sizeof(queue_message) );
		message->type = RELEASE_DD_TASK;
		message->parameters = cur_task;
		if(xQueueSend(xQueue_message_handle, &message, 1000))
		{
			printf("Generator Task Failed!\n");
			fflush(stdout);
		}

		// Sleep
		TickType_t sleep_until = min(min(sleep_times[0], sleep_times[1]), sleep_times[2]);
		if(sleep_until > xTaskGetTickCount())
		{
			vTaskDelay(sleep_until - xTaskGetTickCount());
		}
	}


	xTaskCreate( UserDefined_Task, "UserDefined", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
}

void init_task_array(TaskHandle_t *task_array[3], user_defined_parameters *user_defined_tasks[3])
{
	for (int i = 0; i < 3; ++i)
	{
		xTaskCreate(UserDefined_Task, "UserDefinedTask1", configMINIMAL_STACK_SIZE, user_defined_tasks[i], 2, task_array[i]);
	}
}

void init_user_defined_task_parameters(user_defined_parameters *user_defined_tasks[3])
{
	user_defined_tasks[0] = malloc (sizeof(user_defined_parameters));
	user_defined_tasks[1] = malloc (sizeof(user_defined_parameters));
	user_defined_tasks[2] = malloc (sizeof(user_defined_parameters));
	user_defined_tasks[0]->execution_time = TASK1_EXECUTION_TIME;
	user_defined_tasks[0]->period = TASK1_PERIOD;
	user_defined_tasks[1]->execution_time = TASK2_EXECUTION_TIME;
	user_defined_tasks[1]->period = TASK2_PERIOD;
	user_defined_tasks[2]->execution_time = TASK3_EXECUTION_TIME;
	user_defined_tasks[2]->period = TASK3_PERIOD;
}

//static void Manager_Task( void *pvParameters )
//{
//		if(!xQueueSend(xQueue_lightStatus_handle, &cars_moving, 1000))
//		{
//			if(++tx_data == 3)
//				tx_data = 0;
//			vTaskDelay(delay_ticks);
//		}
//		else
//		{
//			printf("Manager Task failed!\n");
//			fflush(stdout);
//	}
//}
//
//static void CarLights_Task( void *pvParameters )
//{
//				if(xQueueReceive(xQueue_lightStatus_handle, cars_moving, 2000))
//				{
//					if (cars_moving)
//					{
//					car_array[i + 1] = car_array[i];
//					car_array[i] = 0;
//					}
//
//					// If red/amber light, do not move the car forward
//					else
//					{
//						if (!car_array[i + 1] && i != 7)
//						{
//							car_array[i + 1] = car_array[i];
//							car_array[i] = 0;
//						}
//					}
//				}
//			// Delay for a value proportional to the potentiometer value
//			vTaskDelay(delay_ticks);
//}




/*-----------------------------------------------------------*/
/* FreeRTOS-specific functions*/

void vApplicationMallocFailedHook( void )
{
	/* The malloc failed hook is enabled by setting
	configUSE_MALLOC_FAILED_HOOK to 1 in FreeRTOSConfig.h.

	Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( xTaskHandle pxTask, signed char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected.  pxCurrentTCB can be
	inspected in the debugger if the task name passed into this function is
	corrupt. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* The idle task hook is enabled by setting configUSE_IDLE_HOOK to 1 in
	FreeRTOSConfig.h.

	This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amount of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}
/*-----------------------------------------------------------*/

static void prvSetupHardware( void )
{
	/* Ensure all priority bits are assigned as preemption priority bits.
	http://www.freertos.org/RTOS-Cortex-M3-M4.html */
	NVIC_SetPriorityGrouping( 0 );

	/* TODO: Setup the clocks, etc. here, if they were not configured before
	main() was called. */
}

