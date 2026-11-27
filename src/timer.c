
#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

static pthread_t _timer;

struct timer_id_container_t {
	struct timer_id_t id;
	struct timer_id_container_t * next;
};

static struct timer_id_container_t * dev_list = NULL;

static _Atomic uint64_t _time;

static int timer_started = 0;
static atomic_int timer_stop = 0;


/* timer_routine — Thread timer chờ mọi thiết bị hoàn thành slot, tăng thời gian và đánh thức
 * thiết bị.
 * Tham số: args: đối số thread.
 * Trả về: Kết thúc thread bằng pthread_exit(args).
 */
static void * timer_routine(void * args) {
	while (!timer_stop) {
		printf("Time slot %3lu\n", current_time());
		int fsh = 0;
		int event = 0;
		/* Wait for all devices have done the job in current
		 * time slot */
		struct timer_id_container_t * temp;
		for (temp = dev_list; temp != NULL; temp = temp->next) {
			pthread_mutex_lock(&temp->id.event_lock);
			while (!temp->id.done && !temp->id.fsh) {
				pthread_cond_wait(
					&temp->id.event_cond,
					&temp->id.event_lock
				);
			}
			if (temp->id.fsh) {
				fsh++;
			}
			event++;
			pthread_mutex_unlock(&temp->id.event_lock);
		}

		/* Increase the time slot */
		_time++;
		
		/* Let devices continue their job */
		for (temp = dev_list; temp != NULL; temp = temp->next) {
			pthread_mutex_lock(&temp->id.event_lock);
			temp->id.done = 0;
			pthread_cond_signal(&temp->id.timer_cond);
			pthread_mutex_unlock(&temp->id.event_lock);
		}
		if (fsh == event) {
			break;
		}
	}
	pthread_exit(args);
}

/* next_slot — Báo đã hoàn thành slot và chờ timer cho phép chạy slot kế.
 * Tham số: timer_id: sự kiện của thiết bị/thread.
 * Trả về: Không trả giá trị.
 */
void next_slot(struct timer_id_t * timer_id) {
	/* Tell to timer that we have done our job in current slot */
	pthread_mutex_lock(&timer_id->event_lock);
	timer_id->done = 1;
	pthread_cond_signal(&timer_id->event_cond);
	
	/* Wait with the same mutex that protects done. */
	while (timer_id->done) {
		pthread_cond_wait(
			&timer_id->timer_cond,
			&timer_id->event_lock
		);
	}
	pthread_mutex_unlock(&timer_id->event_lock);
}

/* current_time — Lấy số time slot hiện tại từ bộ đếm atomic.
 * Tham số: Không có.
 * Trả về: Số time slot hiện tại.
 */
uint64_t current_time() {
	return _time;
}

/* start_timer — Khởi chạy thread timer sau khi đã đăng ký các sự kiện.
 * Tham số: Không có.
 * Trả về: Không trả giá trị.
 */
void start_timer() {
	timer_started = 1;
	pthread_create(&_timer, NULL, timer_routine, NULL);
}

/* detach_event — Đánh dấu thiết bị đã kết thúc và báo timer.
 * Tham số: event: sự kiện cần kết thúc.
 * Trả về: Không trả giá trị.
 */
void detach_event(struct timer_id_t * event) {
	pthread_mutex_lock(&event->event_lock);
	event->fsh = 1;
	pthread_cond_signal(&event->event_cond);
	pthread_mutex_unlock(&event->event_lock);
}

/* attach_event — Tạo sự kiện mới và các mutex/condition trước khi timer chạy.
 * Tham số: Không có.
 * Trả về: Con trỏ timer_id hoặc NULL nếu timer đã chạy.
 */
struct timer_id_t * attach_event() {
	if (timer_started) {
		return NULL;
	}else{
		struct timer_id_container_t * container =
			(struct timer_id_container_t*)malloc(
				sizeof(struct timer_id_container_t)		
			);
		container->id.done = 0;
		container->id.fsh = 0;
		pthread_cond_init(&container->id.event_cond, NULL);
		pthread_mutex_init(&container->id.event_lock, NULL);
		pthread_cond_init(&container->id.timer_cond, NULL);
		pthread_mutex_init(&container->id.timer_lock, NULL);
		if (dev_list == NULL) {
			dev_list = container;
			dev_list->next = NULL;
		}else{
			container->next = dev_list;
			dev_list = container;
		}
		return &(container->id);
	}
}

/* stop_timer — Dừng timer, join thread và giải phóng các sự kiện.
 * Tham số: Không có.
 * Trả về: Không trả giá trị.
 */
void stop_timer() {
	timer_stop = 1;
	pthread_join(_timer, NULL);
	while (dev_list != NULL) {
		struct timer_id_container_t * temp = dev_list;
		dev_list = dev_list->next;
		pthread_cond_destroy(&temp->id.event_cond);
		pthread_mutex_destroy(&temp->id.event_lock);
		pthread_cond_destroy(&temp->id.timer_cond);
		pthread_mutex_destroy(&temp->id.timer_lock);
		free(temp);
	}
}




