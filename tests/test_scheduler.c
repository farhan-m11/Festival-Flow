/* Scheduler regression tests. Build from the project root:
 *   gcc tests/test_scheduler.c -o tests/test_scheduler
 *   tests/test_scheduler
 * Windows MinGW: gcc tests/test_scheduler.c -o tests/test_scheduler.exe -lws2_32
 */
#define main festivalflow_server_main
#include "../server.c"
#undef main

static int check_valid(const char *name, const char *json) {
    reset_data();
    int parsed = parse_request(json);
    if (parsed != 1 || !solve()) {
        printf("FAIL %s: expected a valid schedule (%s)\n", name,
               error_message[0] ? error_message : "solver returned IMPOSSIBLE");
        return 0;
    }

    int position[MAX_TASKS];
    int seen_dept[MAX_DEPTS] = {0};
    int previous_dept = -1;
    for (int i = 0; i < MAX_TASKS; i++) position[i] = -1;
    for (int i = 0; i < result_count; i++) position[result_order[i]] = i;

    for (int i = 0; i < task_count; i++) {
        if (position[i] < 0 || position[i] >= task_count) {
            printf("FAIL %s: task missing from schedule\n", name);
            return 0;
        }
    }
    for (int i = 0; i < task_count; i++) {
        int d = task_dept[result_order[i]];
        if (d != previous_dept && seen_dept[d]) {
            printf("FAIL %s: department block is split\n", name);
            return 0;
        }
        seen_dept[d] = 1;
        previous_dept = d;
    }
    for (int from = 0; from < task_count; from++) {
        for (int to = 0; to < task_count; to++) {
            if (task_edges[from][to] && position[from] >= position[to]) {
                printf("FAIL %s: prerequisite order violated\n", name);
                return 0;
            }
        }
    }
    printf("PASS %s: VALID\n", name);
    return 1;
}

static int check_impossible(const char *name, const char *json) {
    reset_data();
    int parsed = parse_request(json);
    if (parsed < 0 || (parsed == 1 && !solve())) {
        printf("PASS %s: IMPOSSIBLE\n", name);
        return 1;
    }
    printf("FAIL %s: expected IMPOSSIBLE\n", name);
    return 0;
}

int main(void) {
    int passed = 0;
    passed += check_valid("example_with_internal_and_cross_department_edges",
        "{\"tasks\":[{\"id\":\"S1\",\"department\":\"Sound\"},"
        "{\"id\":\"S2\",\"department\":\"Sound\"},"
        "{\"id\":\"L1\",\"department\":\"Lights\"},"
        "{\"id\":\"L2\",\"department\":\"Lights\"},"
        "{\"id\":\"P\",\"department\":\"Prep\"}],"
        "\"dependencies\":[{\"from\":\"P\",\"to\":\"S1\"},"
        "{\"from\":\"S1\",\"to\":\"S2\"},"
        "{\"from\":\"S2\",\"to\":\"L1\"},"
        "{\"from\":\"L1\",\"to\":\"L2\"}]}");
    passed += check_impossible("cycle_inside_department",
        "{\"tasks\":[{\"id\":\"A\",\"department\":\"Arts\"},"
        "{\"id\":\"B\",\"department\":\"Arts\"}],"
        "\"dependencies\":[{\"from\":\"A\",\"to\":\"B\"},"
        "{\"from\":\"B\",\"to\":\"A\"}]}");
    passed += check_impossible("department_level_conflict",
        "{\"tasks\":[{\"id\":\"A1\",\"department\":\"Audio\"},"
        "{\"id\":\"A2\",\"department\":\"Audio\"},"
        "{\"id\":\"V1\",\"department\":\"Visuals\"},"
        "{\"id\":\"V2\",\"department\":\"Visuals\"}],"
        "\"dependencies\":[{\"from\":\"A1\",\"to\":\"V1\"},"
        "{\"from\":\"V2\",\"to\":\"A2\"}]}");
    passed += check_valid("independent_one_task_departments",
        "{\"tasks\":[{\"id\":\"REG\",\"department\":\"Registration\"},"
        "{\"id\":\"MED\",\"department\":\"Medical\"},"
        "{\"id\":\"G\",\"department\":\"Games\"}],\"dependencies\":[]}");

    printf("%d/4 tests passed.\n", passed);
    return passed == 4 ? 0 : 1;
}
