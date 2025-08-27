/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "cgroup.h"
#include "cgroup-util.h"
#include "manager.h"
#include "rm-rf.h"
#include "service.h"
#include "tests.h"
#include "timer.h"
#include "unit.h"

static dual_timestamp *dual_timestamp_at(dual_timestamp *ts, time_t realtime, time_t boottime, time_t suspended) {
        if (realtime == 0)
                *ts = DUAL_TIMESTAMP_NULL;
        else {
                ts->realtime = realtime * USEC_PER_SEC;
                ts->monotonic = ts->realtime - boottime * USEC_PER_SEC - suspended * USEC_PER_SEC;
        }
        return ts;
}

static triple_timestamp *triple_timestamp_at(triple_timestamp *ts, time_t realtime, time_t boottime, time_t suspended) {
        ts->realtime = realtime * USEC_PER_SEC;
        ts->boottime = ts->realtime - boottime * USEC_PER_SEC;
        ts->monotonic = ts->boottime - suspended * USEC_PER_SEC;
        return ts;
}


#define MY_TEST(name)     \
        static int inner_test_##name(Manager *m);                                               \
        TEST_RET(name, .sd_booted=false) {                                                      \
                _cleanup_(rm_rf_physical_and_freep) char *runtime_dir = NULL;                   \
                _cleanup_(manager_freep) Manager *m = NULL;                                     \
                _cleanup_free_ char *unit_dir = NULL;                                           \
                int r;                                                                          \
                                                                                                \
                ASSERT_OK(get_testdata_dir("units", &unit_dir));                                \
                ASSERT_OK(setenv_unit_path(unit_dir));                                          \
                r = manager_new(RUNTIME_SCOPE_USER, MANAGER_TEST_RUN_BASIC, &m);                \
                if (IN_SET(r, -EPERM, -EACCES)) {                                               \
                        log_error_errno(r, "manager_new: %m");                                  \
                        return log_tests_skipped("cannot create manager");                      \
                }                                                                               \
                assert_se(r >= 0);                                                              \
                                                                                                \
                assert_se(manager_startup(m, NULL, NULL, NULL) >= 0);                           \
                return inner_test_##name(m);                                                    \
        }                                                                                       \
        static int inner_test_##name(Manager *m)


MY_TEST(timer_next_reboot) {
        Unit *u = NULL, *trigger;
        Timer *tut = NULL;
        int r;

        /* Load units and verify hierarchy. */
        ASSERT_OK(manager_load_startable_unit_or_warn(m, "hourly.timer", NULL, &u));
        assert_se(u != NULL);
        tut = TIMER(u);
        assert_se(tut != NULL);

        ASSERT_NOT_NULL(trigger = unit_new(m, sizeof(Service)));

        time_t t0 = 1750000000;   /* 2025-06-15T15:06:40+00:00 */
        time_t t1 = 1750006800;   /* 2025-06-15T17:00:00+00:00 */
        const struct {
                time_t boot; time_t userspace; time_t last_trigger;      time_t now;  time_t exp;
        } table[] = {
                {        t0,           t0+600,             t0+3600,    t0+3600+1000,         t1},
                /* initial realtime firing */
                {        t0,           t0+600,                   0,            t1-1,         t1},
                {        t0,           t0+600,                   0,              t1,    t1+3600},
                {        t0,           t0+600,                   0,            t1+1,    t1+3600},
                /* realtime firing after a precise previous firing */
                {        t0,           t0+600,             t1-3600,            t1-1,         t1},
                {        t0,           t0+600,             t1-3600,              t1,       t1+0},
                {        t0,           t0+600,             t1-3600,            t1+1,       t1+0}, /* XXX why not t1+3600 */
                /* realtime firing after a delayed previous firing */
                {        t0,           t0+600,             t0+3600,            t1-1,         t1},
                {        t0,           t0+600,             t0+3600,              t1,         t1}, /* XXX why not t1+3600 */
                {        t0,           t0+600,             t0+3600,            t1+1,         t1}, /* XXX why not t1+3600, skip compensation */
                /* realtime firing after downtime (last_trigger < boot) */
                {        t0,           t0+600,             t0-3600,         t0+1000,     t0+600}, /* missed when down ~ TIMER_STARTUP */
                {        t0,           t0+600,             t0-7200,         t0+1000,     t0+600}, /* missed 2x when down ~ TIMER_STARTUP */
        };

        FOREACH_ELEMENT(d, table) {
                dual_timestamp timestamps[_MANAGER_TIMESTAMP_MAX];
                triple_timestamp now;
                dual_timestamp exp;
                bool ret_dummy;

                dual_timestamp_at(&timestamps[MANAGER_TIMESTAMP_USERSPACE],
                                  d->userspace, d->boot, 0);
                dual_timestamp_at(&tut->last_trigger, d->last_trigger, d->boot, 0);
                triple_timestamp_at(&now, d->now, d->boot, 0);
                log_info("%s/%li\n", __func__, d-table);

                r = timer_calculate_elapse(
                        tut, &now, trigger,
                        timestamps, ELEMENTSOF(timestamps),
                        false,
                        &ret_dummy, &ret_dummy,
                        &ret_dummy);
                ASSERT_OK(r);

                dual_timestamp_at(&exp, d->exp, d->boot, 0);
                ASSERT_EQ(tut->next_elapse_realtime, exp.realtime);
        }

        return 0;
}

MY_TEST(timer_next_reboot_rd) {
        Unit *u = NULL, *trigger;
        Timer *tut = NULL;
        int r;

        /* Load units and verify hierarchy. */
        ASSERT_OK(manager_load_startable_unit_or_warn(m, "hourly-rd.timer", NULL, &u));
        assert_se(u != NULL);
        tut = TIMER(u);
        assert_se(tut != NULL);

        ASSERT_NOT_NULL(trigger = unit_new(m, sizeof(Service)));

        time_t t0 = 1750000000;   /* 2025-06-15T15:06:40+00:00 */
        time_t t1 = 1750006800;   /* 2025-06-15T17:00:00+00:00 */
        const struct {
                time_t boot; time_t userspace; time_t last_trigger;      time_t now; time_t exp; time_t delta;
        } table[] = {
                {        t0,           t0+600,             t0+3600,    t0+3600+1000,         t1,         600},
                /* initial realtime firing */
                {        t0,           t0+600,                   0,            t1-1,         t1,         600},
                {        t0,           t0+600,                   0,              t1,    t1+3600,         600},
                {        t0,           t0+600,                   0,            t1+1,    t1+3600,         600},
                /* realtime firing after a precise previous firing */
                {        t0,           t0+600,             t1-3600,            t1-1,         t1,         600},
                {        t0,           t0+600,             t1-3600,              t1,       t1+0,         600},
                {        t0,           t0+600,             t1-3600,            t1+1,       t1+0,         600}, /* XXX why not t1+3600 */
                /* realtime firing after a delayed previous firing */
                {        t0,           t0+600,             t0+3600,            t1-1,         t1,         600},
                {        t0,           t0+600,             t0+3600,              t1,         t1,         600}, /* XXX why not t1+3600 */
                {        t0,           t0+600,             t0+3600,            t1+1,         t1,         600}, /* XXX why not t1+3600, skip compensation */
                /* realtime firing after downtime (last_trigger < boot) */
                {        t0,           t0+600,             t0-3600,         t0+1000,     t0+600,         600}, /* missed when down ~ TIMER_STARTUP */
                {        t0,           t0+600,             t0-7200,         t0+1000,     t0+600,         600}, /* missed 2x when down ~ TIMER_STARTUP */

                /*
                 *   t_{i-1}    c_i         t_i
                 *    |          |---- δ ---->
                 *      v......^
                 */
                {   t1-1200,      t1-1200+600,             t1-3600,     t1-1200+600,         t1,         600},
                /*
                 *   t_{i-1}    c_i         t_i
                 *    |          |---- δ ---->
                 *            v......^
                 */
                {   t1+300,       t1+300+600,             t1-3600,       t1-300+600,         t1,         600},
        };

        FOREACH_ELEMENT(d, table) {
                dual_timestamp timestamps[_MANAGER_TIMESTAMP_MAX];
                triple_timestamp now;
                dual_timestamp exp;
                bool ret_dummy;

                dual_timestamp_at(&timestamps[MANAGER_TIMESTAMP_USERSPACE],
                                  d->userspace, d->boot, 0);
                dual_timestamp_at(&tut->last_trigger, d->last_trigger, d->boot, 0);
                triple_timestamp_at(&now, d->now, d->boot, 0);
                log_info("%s/%li\n", __func__, d-table);

                r = timer_calculate_elapse(
                        tut, &now, trigger,
                        timestamps, ELEMENTSOF(timestamps),
                        false,
                        &ret_dummy, &ret_dummy,
                        &ret_dummy);
                ASSERT_OK(r);

                dual_timestamp_at(&exp, d->exp, d->boot, 0);
                ASSERT_BETWEEN(tut->next_elapse_realtime, exp.realtime, exp.realtime + d->delta);
        }

        return 0;
}

DEFINE_TEST_MAIN(LOG_DEBUG);
