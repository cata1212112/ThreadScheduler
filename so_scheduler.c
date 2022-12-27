#include "so_scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

typedef struct thread {
	tid_t tid;
	int remaining;
	unsigned int priority;
	so_handler *func;
	int io;
	int terminated;
	
	sem_t run_semaphore;
	sem_t ready_semaphore;
	
} thread;

typedef struct node {
	thread thr;
	struct node *next;
	struct node *prev;
} node;

typedef struct queue {
	node *head;
	node *tail;
} queue;

typedef struct scheduler {
	unsigned int time_quantum;
	unsigned int io;
	int num_procese;

	queue ready_queue;
	node *running_thread;
	queue waiting;

	queue all_created_threads;
	queue terminated;
} scheduler;

scheduler *sched;

void init(queue *q)
{
	q->head = q->tail = NULL;
}

int empty(queue *q)
{
	if (q->head == NULL)
		return 1;
	return 0;
}

void push(queue *q, node *Nod)
{
	if (empty(q)) {
		q->tail = q->head = Nod;
	} else {
		q->tail->next = Nod;
		Nod->prev = q->tail;
		q->tail = Nod;
	}
}

void delete_from_queue(queue *q, tid_t tid)
{
	node *start = q->head;

	while (start != NULL) {
		if (start->thr.tid == tid) {
			if (q->head == q->tail) {
				q->head = q->tail = NULL;
			} else if (start != q->tail && start != q->head) {
				start->prev->next = start->next;
				start->next->prev = start->prev;
			} else if (start == q->tail) {
				q->tail = start->prev;
				q->tail->next = NULL;
			} else {
				q->head = q->head->next;
				q->head->prev = NULL;
			}
			break;
		}
		start = start->next;
	}
}

node *get_thr_max_prio(queue *q)
{
	if (empty(q))
		return NULL;
	node *start = q->head;
	node *maxi = q->head;

	while (start != NULL) {
		if (start->thr.priority > maxi->thr.priority)
			maxi = start;
		start = start->next;
	}

	return maxi;
}

void destroy_queue(queue *q)
{
	if (sched == NULL)
		return;
	if (empty(q))
		return;
	node *start = q->head;
	node *aux;

	while (start != NULL) {
		aux = start;
		start = start->next;
		sem_destroy(&aux->thr.ready_semaphore);
		sem_destroy(&aux->thr.run_semaphore);
		free(aux);
	}
}

void destroy_queue_secundar(queue *q)
{
	if (sched == NULL)
		return;
	if (empty(q))
		return;
	node *start = q->head;
	node *aux;

	while (start != NULL) {
		aux = start;
		start = start->next;
		free(aux);
	}
}

DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io)
{
	if (sched != NULL)
		return -1;
	if (time_quantum < 1)
		return -1;
	if (io > SO_MAX_NUM_EVENTS)
		return -1;
	sched = malloc(sizeof(scheduler));
	sched->time_quantum = time_quantum;
	sched->io = io;
	sched->num_procese = 0;
	sched->running_thread = NULL;
	init(&sched->ready_queue);
	init(&sched->all_created_threads);
	init(&sched->terminated);
	init(&sched->waiting);
	return 0;
}

node *newNode(so_handler *func, unsigned int priority)
{
	node *aux = malloc(sizeof(node));

	if (aux == NULL)
		return NULL;
	aux->thr.remaining = sched->time_quantum;
	aux->thr.priority = priority;
	aux->thr.func = func;
	aux->thr.terminated = 0;
	aux->thr.io = -1;
	sem_init(&aux->thr.ready_semaphore, 0, 0);
	sem_init(&aux->thr.run_semaphore, 0, 0);
	aux->next = NULL;
	aux->prev = NULL;
	return aux;
}

node *newNodeTid(tid_t tid)
{
	node *aux = malloc(sizeof(node));

	if (aux == NULL)
		return NULL;
	aux->thr.tid = tid;
	aux->next = NULL;
	aux->prev = NULL;
	aux->thr.func = NULL;

	return aux;
}

void *start_thread(void *params)
{
	node *current_thread = params;

	push(&sched->ready_queue, current_thread);
	sem_post(&current_thread->thr.ready_semaphore);

	sem_wait(&current_thread->thr.run_semaphore);

	current_thread->thr.func(current_thread->thr.priority);


	current_thread->thr.terminated = 1;
	current_thread->next = NULL;
	current_thread->prev = NULL;

	push(&sched->terminated, current_thread);
	node *maxi = get_thr_max_prio(&sched->ready_queue);

	if (maxi != NULL) {
		delete_from_queue(&sched->ready_queue, maxi->thr.tid);
		sched->running_thread = maxi;
		sem_post(&sched->running_thread->thr.run_semaphore);
	}

	return NULL;
}

DECL_PREFIX tid_t so_fork(so_handler *func, unsigned int priority)
{
	if (priority < 0 || priority > SO_MAX_PRIO || func == NULL)
		return INVALID_TID;
	node *current_thread = newNode(func, priority);

	if (current_thread == NULL)
		return INVALID_TID;
	sched->num_procese++;
	if (pthread_create(&current_thread->thr.tid, NULL, start_thread, current_thread) != 0)
		return INVALID_TID;
	sem_wait(&current_thread->thr.ready_semaphore);
	push(&sched->all_created_threads, newNodeTid(current_thread->thr.tid));
	if (sched->running_thread == NULL) {
		sched->running_thread = current_thread;
		delete_from_queue(&sched->ready_queue, current_thread->thr.tid);
		sem_post(&current_thread->thr.run_semaphore);
	} else {
		node *maxi = get_thr_max_prio(&sched->ready_queue);

		if (maxi != NULL && maxi->thr.priority > sched->running_thread->thr.priority) {
			node *aux = sched->running_thread;


			sched->running_thread->next = NULL;
			sched->running_thread->prev = NULL;
			sched->running_thread->thr.remaining = sched->time_quantum;
			push(&sched->ready_queue, sched->running_thread);
			delete_from_queue(&sched->ready_queue, maxi->thr.tid);
			sched->running_thread = maxi;
			sem_post(&maxi->thr.run_semaphore);

			sem_wait(&aux->thr.run_semaphore);

		}
		so_exec();
	}
	return current_thread->thr.tid;
}


DECL_PREFIX int so_wait(unsigned int io)
{
	if (io < 0 || io >= sched->io)
		return -1;

	sched->running_thread->thr.io = io;
	sched->running_thread->next = NULL;
	sched->running_thread->prev = NULL;
	sched->running_thread->thr.remaining = sched->time_quantum;
	push(&sched->waiting, sched->running_thread);
	node *aux = sched->running_thread;
	node *maxi = get_thr_max_prio(&sched->ready_queue);

	if (maxi != NULL) {
		sched->running_thread = maxi;
		delete_from_queue(&sched->ready_queue, maxi->thr.tid);
		sem_post(&maxi->thr.run_semaphore);
	}

	sem_wait(&aux->thr.run_semaphore);
	return 0;
}

DECL_PREFIX int so_signal(unsigned int io)
{
	if (io < 0 || io >= sched->io)
		return -1;
	int cnt = 0;
	node *start = sched->waiting.head;
	node *urm;

	while (start != NULL) {

		if (start->thr.io == io) {
			urm = start->next;
			delete_from_queue(&sched->waiting, start->thr.tid);
			start->next = NULL;
			start->prev = NULL;
			start->thr.io = -1;
			push(&sched->ready_queue, start);
			cnt++;
			start = urm;
		} else {
			start = start->next;
		}
	}
	so_exec();
	return cnt;
}

DECL_PREFIX void so_exec(void)
{
	sched->running_thread->thr.remaining--;
	if (sched->running_thread->thr.remaining == 0) {
		node *maxi = get_thr_max_prio(&sched->ready_queue);

		if (maxi != NULL && maxi->thr.priority >= sched->running_thread->thr.priority) {
			node *aux = sched->running_thread;

			sched->running_thread->next = NULL;
			sched->running_thread->prev = NULL;
			sched->running_thread->thr.remaining = sched->time_quantum;
			push(&sched->ready_queue, sched->running_thread);
			delete_from_queue(&sched->ready_queue, maxi->thr.tid);
			sched->running_thread = maxi;
			sem_post(&maxi->thr.run_semaphore);

			sem_wait(&aux->thr.run_semaphore);

		} else {
			sched->running_thread->thr.remaining = sched->time_quantum;
		}
	}
}

DECL_PREFIX void so_end(void)
{
	if (sched != NULL) {
		node *start = sched->all_created_threads.head;

		while (start != NULL) {
			pthread_join(start->thr.tid, NULL);
			start = start->next;
		}
	}
	destroy_queue(&sched->terminated);
	destroy_queue_secundar(&sched->all_created_threads);

	free(sched);
	sched = NULL;
}

