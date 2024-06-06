#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// process states
#define READY 0
#define RUNNING 1
#define BLOCKED 2
#define DELAYED 3

// process types
#define RTP 0
#define TSP 1

typedef struct
{
    int id;
    char name[20];
    int type;
    int state;
    pthread_t thread;
    int registers[4]; // PC, A, B, C
    int semaphore_id;
    void *(*function) (
        void *); // Function pointer type matching pthread_create
} PCB;

typedef struct pcbnode
{
    PCB pcb;
    struct pcbnode *next;
} PCBNode;

typedef struct pcbq
{
    int state;
    PCBNode *front;
    PCBNode *rear;
} PCBQ;

typedef struct
{
    PCBQ RTQ;
    PCBQ TSQ;
} Scheduler;

typedef struct
{
    int state;
    int value;
    PCBQ queue;
} Semaphore;

void schedule (Scheduler *sch);
// initialize queues
Semaphore s1, s2;
char shared;
Scheduler scheduler;

PCBQ ready_queue_rt;
PCBQ running_queue_rt;
PCBQ blocked_queue_rt;
PCBQ delayed_queue_rt;

PCBQ ready_queue_tc;
PCBQ running_queue_tc;
PCBQ blocked_queue_tc;
PCBQ delayed_queue_tc;

PCB *
findPCBByID (PCBQ *queue, int id)
{
    PCBNode *current = queue->front;
    while (current != NULL)
        {
            if (current->pcb.id == id)
                {
                    return &current->pcb;
                }
            current = current->next;
        }
    return NULL; // PCB with given ID not found
}

void
enqueue (PCBQ *queue, PCB pcb)
{
    PCBNode *new_node = (PCBNode *)malloc (sizeof (PCBNode));
    new_node->pcb = pcb;
    new_node->next = NULL;
    if (queue->rear == NULL)
        {
            queue->front = new_node;
            queue->rear = new_node;
        }
    else
        {
            queue->rear->next = new_node;
            queue->rear = new_node;
        }
}

PCB
dequeue (PCBQ *queue)
{
    // can't dequeue from empty queue
    if (queue->front == NULL)
        {
            printf ("Queue is empty!\n");
            exit (EXIT_FAILURE);
        }

    PCBNode *temp = queue->front;
    PCB pcb = temp->pcb;
    queue->front = queue->front->next;
    if (queue->front == NULL)
        {
            queue->rear = NULL;
        }
    free (temp);
    return pcb;
}

void
insert (PCBQ *queue, PCB pcb, int position)
{
    PCBNode *newNode = (PCBNode *)malloc (sizeof (PCBNode));
    newNode->pcb = pcb;
    newNode->next = NULL;

    if (position == 0)
        {
            newNode->next = queue->front;
            queue->front = newNode;
            if (queue->rear == NULL)
                {
                    queue->rear = newNode;
                }
            return;
        }

    PCBNode *current = queue->front;
    for (int i = 0; current != NULL && i < position - 1; i++)
        {
            current = current->next;
        }

    if (current == NULL)
        {
            free (newNode); // Position is out of bounds
            return;
        }

    newNode->next = current->next;
    current->next = newNode;
    if (newNode->next == NULL)
        {
            queue->rear = newNode;
        }
}

PCB *
take (PCBQ *queue, int id)
{
    PCBNode *current = queue->front;
    PCBNode *previous = NULL;

    while (current != NULL)
        {
            if (current->pcb.id == id)
                {
                    if (previous == NULL)
                        {
                            queue->front = current->next;
                        }
                    else
                        {
                            previous->next = current->next;
                        }

                    if (current->next == NULL)
                        {
                            queue->rear = previous;
                        }

                    PCB *pcb = &current->pcb;
                    free (current);
                    return pcb;
                }
            previous = current;
            current = current->next;
        }
    return NULL; // PCB with given ID not found
}

void
make_proc (Scheduler *sch, char *name, int type, int state,
           void *(*function) (void *))
{
    static int id = 0;
    PCB pcb;
    pcb.id = id++;
    strcpy (pcb.name, name);
    pcb.type = type;
    pcb.state = state;
    pcb.semaphore_id = (type == 0) ? s1.value : s2.value;
    pcb.function = function; // Assign function pointer
    enqueue (type == 0 ? &sch->RTQ : &sch->TSQ, pcb);
}

void
make_ready (Scheduler *sch, int id, PCBQ *queue)
{
    PCB *pcb = take (queue, id);
    pcb->state = READY;
    if (pcb->type == RTP)
        {
            enqueue (&sch->RTQ, *pcb);
        }
    else if (pcb->type == TSP)
        {
            enqueue (&sch->TSQ, *pcb);
        }
}

void
block (Scheduler *sch, int id, PCBQ *queue)
{
    PCB *pcb = take (queue, id);
    pcb->state = BLOCKED;
    // schedule
    if (pcb->type == RTP)
        {
            enqueue (&scheduler.RTQ, *pcb);
        }
    else if (pcb->type == TSP)
        {
            enqueue (&scheduler.TSQ, *pcb);
        }
}

void
init_sem (Semaphore *sem, int initial_value)
{
    sem->state = 0;
    sem->value = initial_value;
    sem->queue.front = sem->queue.rear = NULL;
}

void
wait_sem (Semaphore *sem)
{
    sem->value--;
    if (sem->value < 0)
        {
            PCB current_pcb = dequeue (&ready_queue_rt);
            enqueue (&sem->queue, current_pcb);
            block (&scheduler, current_pcb.id, &ready_queue_rt);
        }
}

void
signal_sem (Semaphore *sem)
{
    sem->value++;
    if (sem->value <= 0)
        {
            PCBQ *queue;
            PCB pcb = dequeue (&sem->queue);
            pcb.type == RTP ? queue = &ready_queue_rt : &ready_queue_tc;
            enqueue (&sem->queue, pcb);
            make_ready (&scheduler, pcb.id, queue);
        }
}

void *
producer (void *arg)
{
    FILE *src = fopen ("rtk.c", "r");
    printf ("Producer hello\n");
    if (src == NULL)
        {
            perror ("Failed to open source file");
            pthread_exit (NULL);
        }
    while (!feof (src))
        {
            wait_sem (&s1);
            if (shared == 0)
                break;
            shared = fgetc (src);
            printf ("%c", shared);
            signal_sem (&s2);
        }
    fclose (src);
    pthread_exit (NULL);
}

void *
consumer (void *arg)
{
    FILE *dst = fopen ("rtk2.c", "w");
    printf ("Consumer hello\n");
    if (dst == NULL)
        {
            perror ("Failed to open destination file");
            pthread_exit (NULL);
        }
    while (1)
        {
            wait_sem (&s2);
            if (shared == 0)
                break;
            printf ("%c", shared);
            fputc (shared, dst);
            signal_sem (&s1);
        }
    fclose (dst);
    pthread_exit (NULL);
}

void
init_scheduler (Scheduler *sch)
{
    sch->RTQ.front = sch->RTQ.rear = NULL;
    sch->TSQ.front = sch->TSQ.rear = NULL;
}

void
schedule (Scheduler *sch)
{
    while (1)
        {
            if (sch->RTQ.front != NULL)
                {
                    PCB pcb = dequeue (&sch->RTQ);
                    pthread_create (&pcb.thread, NULL, pcb.function,
                                    NULL); // Use function pointer
                }
            else if (sch->TSQ.front != NULL)
                {
                    PCB pcb = dequeue (&sch->TSQ);
                    pthread_create (&pcb.thread, NULL, pcb.function,
                                    NULL); // Use function pointer
                }
        }
}

int
main ()
{
    // Initialize semaphores
    init_sem (&s1, 0); // Semaphore s1 initialized with value 1
    init_sem (&s2, 1); // Semaphore s2 initialized with value 0

    // Initialize scheduler
    init_scheduler (&scheduler);
    shared = 1;

    // Create producer and consumer processes
    make_proc (&scheduler, "Producer", RTP, READY, producer);
    make_proc (&scheduler, "Consumer", RTP, READY, consumer);

    // Run scheduler
    schedule (&scheduler);

    return 0;
}
