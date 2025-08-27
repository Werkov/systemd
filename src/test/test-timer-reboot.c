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


DEFINE_TEST_MAIN(LOG_DEBUG);
