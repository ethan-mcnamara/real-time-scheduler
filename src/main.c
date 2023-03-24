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

#define SCHEDULER_PRIORITY 1
#define GENERATOR_PRIORITY 2
#define MONITOR_PRIORITY 4
#define PENDING_TASK_PRIORITY 0
#define ACTIVE_TASK_PRIORITY 3

#define MONITOR_PERIOD_MS 2000

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
	TickType_t execution_time;
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

typedef struct generator_task_parameters
{
	uint16_t execution_time;
	uint16_t period;
	uint32_t task_id;
} generator_task_parameters;

typedef struct user_defined_parameters
{
	uint16_t execution_time;
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
static void Scheduler_Task( void *pvParameters );
static void Monitor_Task( void *pvParameters );


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
void init_user_defined_task_parameters(generator_task_parameters *user_defined_tasks[3]);
void sort_dd_task_list(dd_task_list *dd_task_list);
void swap_nodes(dd_task_list *a, dd_task_list *b);
static void prvSetupHardware( void );
void output_task_lists(dd_task_list *active_task_list, dd_task_list *completed_task_list, dd_task_list *overdue_task_list);


// Queue declarations
xQueueHandle xQueue_message_handle = 0;
xQueueHandle xQueue_monitor_handle = 0;

int main(void)
{
	prvSetupHardware();

	// Create the queues
	xQueue_message_handle = xQueueCreate(mainQUEUE_LENGTH, sizeof(queue_message*));
	xQueue_monitor_handle = xQueueCreate(mainQUEUE_LENGTH, sizeof(dd_task_list*));

	// Add the queues to the registry
	vQueueAddToRegistry(xQueue_message_handle, "MessageQueue");
	vQueueAddToRegistry(xQueue_monitor_handle, "MonitorQueue");

	// Create the  tasks used in the program
	xTaskCreate(Generator_Task, "Generator", configMINIMAL_STACK_SIZE, NULL, GENERATOR_PRIORITY, NULL);
	xTaskCreate(Scheduler_Task, "Scheduler", configMINIMAL_STACK_SIZE, NULL, SCHEDULER_PRIORITY, NULL);
	xTaskCreate(Monitor_Task, "Monitor", configMINIMAL_STACK_SIZE, NULL, MONITOR_PRIORITY, NULL);

	/* Start the tasks and timer running. */
	printf("Before task scheduler\n");
	vTaskStartScheduler();

	printf("Insufficient heap\n");
    return 0;

}

static void UserDefined_Task ( void *pvParameters )
{
	printf("Start User-Defined\n");
	user_defined_parameters *parameters = (user_defined_parameters *) pvParameters;
	TickType_t start_ticks = xTaskGetTickCount();

	while (xTaskGetTickCount() - start_ticks < parameters->execution_time / portTICK_PERIOD_MS) {};

	queue_message *message = pvPortMalloc( sizeof(queue_message) );
	message->type = COMPLETE_DD_TASK;
	dd_task *message_parameters = pvPortMalloc (sizeof(dd_task) );
	message_parameters->task_id = parameters->task_id;
	message->parameters = message_parameters;

	if(xQueueSend(xQueue_message_handle, &message, 1000) != pdTRUE)
	{
		printf("User Defined Task Failed!\n");
		fflush(stdout);
	}
	printf("End User-Defined\n");
	vTaskDelete( NULL );
}

static void Generator_Task ( void *pvParameters )
{
	uint8_t task_index = 0;
	generator_task_parameters *user_defined_tasks[3];
	init_user_defined_task_parameters(user_defined_tasks);
	uint16_t task_id = 0;

	TickType_t sleep_times[3];
	for (int i = 0; i < 3; ++i)
	{
		sleep_times[i] = xTaskGetTickCount();
	}

	while(1)
	{
		printf("Start Generator Loop\n");
		// Create dd_task
		uint8_t cur_task_index = task_index++;
		dd_task *cur_task = pvPortMalloc( sizeof (dd_task) );
		cur_task->task_id = task_id++;
		cur_task->type = PERIODIC;
		cur_task->release_time = xTaskGetTickCount();
		cur_task->absolute_deadline = cur_task->release_time +
									(user_defined_tasks[cur_task_index % 3]->period / portTICK_PERIOD_MS);
//		cur_task->t_handle = task_array[task_index % 3];
		cur_task->t_handle = NULL;
		sleep_times[task_index % 3] = cur_task->absolute_deadline;

		//Send message
		queue_message *message = pvPortMalloc( sizeof(queue_message) );
		message->type = RELEASE_DD_TASK;
		message->parameters = cur_task;
		if(xQueueSend(xQueue_message_handle, &message, 1000) != pdTRUE)
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
		printf("End Generator Loop\n");
	}
}

static void Scheduler_Task ( void *pvParameters )
{
	dd_task_list *active_task_list = pvPortMalloc( sizeof(dd_task_list));
	active_task_list = NULL;
	dd_task_list *completed_task_list = pvPortMalloc( sizeof(dd_task_list));
	completed_task_list = NULL;
	dd_task_list *overdue_task_list = pvPortMalloc( sizeof(dd_task_list));
	overdue_task_list = NULL;
	dd_task_list *comparison_list = pvPortMalloc( sizeof(dd_task_list));
	comparison_list = NULL;
	queue_message *message;
	user_defined_parameters *parameters = pvPortMalloc( sizeof(user_defined_parameters) );

	while (1)
	{
		if (xQueueReceive(xQueue_message_handle, &message, 1000) == pdPASS)
		{
			switch (message->type)
			{
			case RELEASE_DD_TASK:
			{
				printf("Start Release\n");
				// Create new task
				if (active_task_list != comparison_list)
				{
					vTaskPrioritySet(active_task_list->task.t_handle, PENDING_TASK_PRIORITY);
				}


				parameters->task_id = message->parameters->task_id;
				parameters->execution_time = message->parameters->execution_time;

				xTaskCreate(UserDefined_Task, "UserDefined", configMINIMAL_STACK_SIZE,
						parameters, PENDING_TASK_PRIORITY, &message->parameters->t_handle);
				dd_task_list *new_task = pvPortMalloc( sizeof(dd_task_list));
//				new_task->task = *(dd_task*)pvPortMalloc( sizeof(dd_task) );
				new_task->task = *message->parameters;

				dd_task_list *end_active_list = active_task_list;
				if (active_task_list == comparison_list)
				{
					active_task_list = new_task;
				}
				else
				{
					while (end_active_list->next_task != NULL)
					{
						end_active_list = end_active_list->next_task;
					}
					end_active_list->next_task = new_task;
					sort_dd_task_list(active_task_list);
				}

				// Remove overdue tasks
				while (active_task_list->task.absolute_deadline <
						active_task_list->task.execution_time + xTaskGetTickCount())
				{
					dd_task_list *end_overdue_list = overdue_task_list;
					while (end_overdue_list->next_task != NULL)
					{
						end_overdue_list = end_overdue_list->next_task;
					}
					end_overdue_list->next_task = active_task_list;
					end_overdue_list->next_task->next_task = NULL;
					active_task_list = active_task_list->next_task;

					end_overdue_list->next_task->task.completion_time = xTaskGetTickCount();
					vTaskDelete(end_overdue_list->next_task->task.t_handle);
				}

				vTaskPrioritySet( active_task_list->task.t_handle, ACTIVE_TASK_PRIORITY);
				printf("End release\n");
				break;
			}

			case COMPLETE_DD_TASK:
			{
				printf("Start complete\n");
				// Delete task
				message->parameters->completion_time = xTaskGetTickCount();
//				vTaskDelete(message->parameters->t_handle);

				dd_task_list *end_completed_list = completed_task_list;
				while (end_completed_list->next_task != NULL)
				{
					end_completed_list = end_completed_list->next_task;
				}
				end_completed_list->next_task = active_task_list;
				active_task_list = active_task_list->next_task;

//				eTaskState cur_state = eTaskGetState(active_task_list->task.t_handle);

				//
				//
				// BREAKING HERE ON 3RD ITERATION
				//
				//
				vTaskPrioritySet( active_task_list->task.t_handle, ACTIVE_TASK_PRIORITY);

				printf("End complete\n");
				break;
			}

			case GET_ACTIVE_DD_TASK_LIST:
			{
				// Send active task list via queue
				if(xQueueSend(xQueue_monitor_handle, &active_task_list, 1000) != pdTRUE)
				{
					printf("Generator Task Failed!\n");
					fflush(stdout);
				}
				break;
			}

			case GET_COMPLETED_DD_TASK_LIST:
			{
				// Send completed task list via queue
				if(xQueueSend(xQueue_monitor_handle, &completed_task_list, 1000) != pdTRUE)
				{
					printf("Generator Task Failed!\n");
					fflush(stdout);
				}
				break;
			}

			case GET_OVERDUE_DD_TASK_LIST:
			{
				// Send overdue task list via queue
				if(xQueueSend(xQueue_monitor_handle, &overdue_task_list, 1000) != pdTRUE)
				{
					printf("Generator Task Failed!\n");
					fflush(stdout);
				}
				break;
			}

			default:
			{
				printf("Message type error in Scheduler Task!\n");
				fflush(stdout);
			}
			}
		}
	}
}

static void Monitor_Task ( void *pvParameters )
{
	dd_task_list *active_task_list;
	dd_task_list *completed_task_list;
	dd_task_list *overdue_task_list;

	queue_message *active_message = pvPortMalloc( sizeof(queue_message) );
	active_message->type = GET_ACTIVE_DD_TASK_LIST;

	queue_message *overdue_message = pvPortMalloc( sizeof(queue_message) );
	overdue_message->type = GET_OVERDUE_DD_TASK_LIST;

	queue_message *completed_message = pvPortMalloc( sizeof(queue_message) );
	completed_message->type = GET_COMPLETED_DD_TASK_LIST;

	while (1)
	{
		printf("Start monitor loop\n");
		// Active queue
		if(xQueueSend(xQueue_message_handle, &active_message, 1000) != pdTRUE)
		{
			printf("Monitor Task Failed!\n");
			fflush(stdout);
		}
		if (xQueueReceive(xQueue_monitor_handle, &active_task_list, 1000) != pdPASS)
		{
			printf("Monitor Task Failed!\n");
			fflush(stdout);
		}

		// Completed queue
		if(xQueueSend(xQueue_message_handle, &completed_message, 1000) != pdTRUE)
		{
			printf("Monitor Task Failed!\n");
			fflush(stdout);
		}
		if (xQueueReceive(xQueue_monitor_handle, &completed_task_list, 1000) != pdPASS)
		{
			printf("Monitor Task Failed!\n");
			fflush(stdout);
		}

		// Overdue queue
		if(xQueueSend(xQueue_message_handle, &overdue_message, 1000) != pdTRUE)
		{
			printf("Monitor Task Failed!\n");
			fflush(stdout);
		}
		if (xQueueReceive(xQueue_monitor_handle, &overdue_task_list, 1000) != pdPASS)
		{
//			printf("Monitor Task Failed - overdue task list receive\n");
//			fflush(stdout);
		}

		output_task_lists(active_task_list, completed_task_list, overdue_task_list);

		vTaskDelay(MONITOR_PERIOD_MS / portTICK_PERIOD_MS);
		printf("End monitor loop\n");
	}
}

void output_task_lists(dd_task_list *active_task_list, dd_task_list *completed_task_list, dd_task_list *overdue_task_list)
{
	dd_task_list *cur_elem;
	uint16_t counter = 0;

	printf("ACTIVE LIST\n");
	cur_elem = active_task_list;
	while (cur_elem != NULL)
	{
		counter++;
		printf("Task ID: %lu, ", cur_elem->task.task_id);
		printf("Release time: %lu, ", cur_elem->task.release_time);
		printf("Absolute deadline: %lu, ", cur_elem->task.absolute_deadline);
		printf("Completion time: %lu\n", cur_elem->task.completion_time);
		cur_elem = cur_elem->next_task;
		fflush(stdout);
	}
	printf("Number active tasks: %d\n\n", counter);
	fflush(stdout);

	counter = 0;
	printf("COMPLETED LIST\n");
	cur_elem = completed_task_list;
	while (cur_elem != NULL)
	{
		counter++;
		printf("Task ID: %lu, ", cur_elem->task.task_id);
		printf("Release time: %lu, ", cur_elem->task.release_time);
		printf("Absolute deadline: %lu, ", cur_elem->task.absolute_deadline);
		printf("Completion time: %lu\n", cur_elem->task.completion_time);
		cur_elem = cur_elem->next_task;
		fflush(stdout);
	}
	printf("Number completed tasks: %d\n\n", counter);
	fflush(stdout);

	counter = 0;
	printf("OVERDUE LIST\n");
	if (overdue_task_list != NULL)
	{
		cur_elem = overdue_task_list;
		while (cur_elem != NULL)
		{
			counter++;
			printf("Task ID: %lu, ", cur_elem->task.task_id);
			printf("Release time: %lu, ", cur_elem->task.release_time);
			printf("Absolute deadline: %lu, ", cur_elem->task.absolute_deadline);
			printf("Completion time: %lu\n", cur_elem->task.completion_time);
			cur_elem = cur_elem->next_task;
			fflush(stdout);
		}
	}
	printf("Number overdue tasks: %d\n", counter);
	fflush(stdout);
}

void sort_dd_task_list(dd_task_list *task_list)
{
	uint8_t swapped;
	dd_task_list *ptr1;
	dd_task_list *lptr = NULL;

	// Check for empty list
	if (task_list == NULL)
	{
		return;
	}

	do
	{
		swapped = 0;
		ptr1 = task_list;

		while (ptr1->next_task != lptr)
		{
			if (ptr1->task.absolute_deadline > ptr1->next_task->task.absolute_deadline)
			{
				swap_nodes(ptr1, ptr1->next_task);
				swapped = 1;
			}
			ptr1 = ptr1->next_task;
		}
		lptr = ptr1;
	}
	while(swapped);
}

void swap_nodes(dd_task_list *a, dd_task_list *b)
{
	dd_task *temp = malloc (sizeof (dd_task) );
	temp = &a->task;
	a->task = b->task;
	b->task = *temp;
}

void init_user_defined_task_parameters(generator_task_parameters *user_defined_tasks[3])
{
	user_defined_tasks[0] = malloc (sizeof(generator_task_parameters));
	user_defined_tasks[1] = malloc (sizeof(generator_task_parameters));
	user_defined_tasks[2] = malloc (sizeof(generator_task_parameters));
	user_defined_tasks[0]->execution_time = TASK1_EXECUTION_TIME;
	user_defined_tasks[0]->period = TASK1_PERIOD;
	user_defined_tasks[1]->execution_time = TASK2_EXECUTION_TIME;
	user_defined_tasks[1]->period = TASK2_PERIOD;
	user_defined_tasks[2]->execution_time = TASK3_EXECUTION_TIME;
	user_defined_tasks[2]->period = TASK3_PERIOD;
}

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

