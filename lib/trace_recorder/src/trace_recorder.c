/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file trace_recorder.c
 * @brief Overrides Zephyr __weak tracing _user hooks to record thread
 *        context switches, ISR events, and idle events in a static ring
 *        buffer for post-mortem Renode memory dump.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/tracing/tracing_user.h>

#include <trace_recorder/trace_recorder.h>

LOG_MODULE_REGISTER(trace_recorder, CONFIG_TRACE_RECORDER_LOG_LEVEL);

/* ── trace record format (8 bytes) ───────────────────────────────── */

#define TRACE_EVENT_SWITCHED_IN  0
#define TRACE_EVENT_SWITCHED_OUT 1
#define TRACE_EVENT_CREATE       2
#define TRACE_EVENT_ISR_ENTER    3
#define TRACE_EVENT_ISR_EXIT     4
#define TRACE_EVENT_IDLE_ENTER   5

#define TRACE_THREAD_ID_UNKNOWN 0xFFFFU

struct trace_record {
	uint32_t timestamp; /* k_cycle_get_32() */
	uint8_t event_type; /* TRACE_EVENT_* */
	uint8_t event_data; /* priority for SWITCHED_IN, 0 otherwise */
	uint16_t thread_id; /* assigned thread ID, or TRACE_THREAD_ID_UNKNOWN */
};

BUILD_ASSERT(sizeof(struct trace_record) == 8, "trace_record must be 8 bytes");

/* ── static buffer (BSS — dumpable via Renode sysbus ReadMemory) ─── */

static struct trace_record trace_records[CONFIG_TRACE_RECORDER_BUFFER_SIZE];

/* No init needed — BSS zeroed by CRT. All-zero record means end-of-data. */

static char trace_thread_names[CONFIG_TRACE_RECORDER_MAX_THREADS][CONFIG_THREAD_MAX_NAME_LEN];

/* ── ring buffer state ───────────────────────────────────────────── */

static struct k_spinlock g_trace_lock;
static uint32_t g_trace_head;                      /* next write index */
static bool g_trace_ready;                         /* false until SYS_INIT completes */
static uint32_t g_trace_overflow;                  /* count of dropped records */
static atomic_t g_next_thread_id = ATOMIC_INIT(1); /* 1-based; 0 = unassigned */

/* ── helper: write one record ─────────────────────────────────────── */

static void trace_write_record(uint8_t event_type, uint8_t event_data, uint16_t thread_id)
{
	k_spinlock_key_t key = k_spin_lock(&g_trace_lock);

	struct trace_record *rec = &trace_records[g_trace_head];
	rec->timestamp = k_cycle_get_32();
	rec->event_type = event_type;
	rec->event_data = event_data;
	rec->thread_id = thread_id;

	g_trace_head++;
	if (g_trace_head >= CONFIG_TRACE_RECORDER_BUFFER_SIZE) {
		g_trace_head = 0;
		g_trace_overflow++;
	}

	k_spin_unlock(&g_trace_lock, key);
}

/* ── helper: get thread ID from custom_data ───────────────────────── */

static uint16_t trace_thread_id(struct k_thread *thread)
{
	if (thread->custom_data == NULL) {
		return TRACE_THREAD_ID_UNKNOWN;
	}
	/* custom_data stores a 1-based ID (0 = unassigned, NULL-safe) */
	return (uint16_t)(uintptr_t)thread->custom_data;
}

/* ── Zephyr tracing _user hooks (override __weak defaults) ────────── */

void sys_trace_thread_create_user(struct k_thread *thread)
{
	if (!g_trace_ready) {
		return;
	}

	uint16_t id = (uint16_t)atomic_inc(&g_next_thread_id);

	__ASSERT(id < CONFIG_TRACE_RECORDER_MAX_THREADS,
		 "trace_recorder: too many threads (max %d); increase "
		 "CONFIG_TRACE_RECORDER_MAX_THREADS",
		 CONFIG_TRACE_RECORDER_MAX_THREADS);

	thread->custom_data = (void *)(uintptr_t)id;

	const char *name = k_thread_name_get(thread);
	if (name != NULL) {
		strncpy(trace_thread_names[id], name, CONFIG_THREAD_MAX_NAME_LEN - 1);
		trace_thread_names[id][CONFIG_THREAD_MAX_NAME_LEN - 1] = '\0';
	}

	trace_write_record(TRACE_EVENT_CREATE, 0, id);
}

void sys_trace_thread_switched_in_user(void)
{
	if (!g_trace_ready) {
		return;
	}

	struct k_thread *t = k_sched_current_thread_query();
	uint16_t id = trace_thread_id(t);
	uint8_t prio = (uint8_t)k_thread_priority_get(t);

	trace_write_record(TRACE_EVENT_SWITCHED_IN, prio, id);
}

void sys_trace_thread_switched_out_user(void)
{
	if (!g_trace_ready) {
		return;
	}

	struct k_thread *t = k_sched_current_thread_query();
	uint16_t id = trace_thread_id(t);

	trace_write_record(TRACE_EVENT_SWITCHED_OUT, 0, id);
}

void sys_trace_isr_enter_user(void)
{
	if (!g_trace_ready) {
		return;
	}
	trace_write_record(TRACE_EVENT_ISR_ENTER, 0, TRACE_THREAD_ID_UNKNOWN);
}

void sys_trace_isr_exit_user(void)
{
	if (!g_trace_ready) {
		return;
	}
	trace_write_record(TRACE_EVENT_ISR_EXIT, 0, TRACE_THREAD_ID_UNKNOWN);
}

void sys_trace_idle_user(void)
{
	if (!g_trace_ready) {
		return;
	}
	trace_write_record(TRACE_EVENT_IDLE_ENTER, 0, TRACE_THREAD_ID_UNKNOWN);
}

/* No-ops for hooks we don't need (must override to prevent link errors
 * if tracing_test.h/other format backend is not in use). These are
 * weak in tracing_user.c so defining them here is sufficient. */

void sys_trace_thread_abort_user(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}
void sys_trace_thread_suspend_user(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}
void sys_trace_thread_resume_user(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}
void sys_trace_thread_name_set_user(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}
void sys_trace_thread_info_user(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}
void sys_trace_thread_priority_set_user(struct k_thread *thread, int prio)
{
	ARG_UNUSED(thread);
	ARG_UNUSED(prio);
}
void sys_trace_thread_sched_ready_user(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}
void sys_trace_thread_pend_user(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}
void sys_trace_sys_init_enter_user(const struct init_entry *entry, int level)
{
	ARG_UNUSED(entry);
	ARG_UNUSED(level);
}
void sys_trace_sys_init_exit_user(const struct init_entry *entry, int level, int result)
{
	ARG_UNUSED(entry);
	ARG_UNUSED(level);
	ARG_UNUSED(result);
}

/* ── SYS_INIT: assign IDs to pre-existing threads ─────────────────── */

static void assign_existing_thread(struct k_thread *thread, void *user_data)
{
	ARG_UNUSED(user_data);

	if (thread->custom_data != NULL) {
		return;
	}

	uint16_t id = (uint16_t)atomic_inc(&g_next_thread_id);

	if (id >= CONFIG_TRACE_RECORDER_MAX_THREADS) {
		LOG_WRN("too many pre-existing threads; skipping ID assignment");
		return;
	}

	thread->custom_data = (void *)(uintptr_t)id;

	const char *name = k_thread_name_get(thread);
	if (name != NULL) {
		strncpy(trace_thread_names[id], name, CONFIG_THREAD_MAX_NAME_LEN - 1);
		trace_thread_names[id][CONFIG_THREAD_MAX_NAME_LEN - 1] = '\0';
	}
}

static int trace_recorder_init(void)
{
	k_thread_foreach(assign_existing_thread, NULL);
	g_trace_ready = true;
	LOG_DBG("trace_recorder: init done, %u pre-existing threads",
		(uint32_t)atomic_get(&g_next_thread_id));
	return 0;
}
SYS_INIT(trace_recorder_init, APPLICATION, 1);
