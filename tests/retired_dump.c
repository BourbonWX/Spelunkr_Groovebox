/* SPDX-License-Identifier: GPL-3.0-only
 * The web editor's RETIRED map (web/editor.html), from the firmware: every retired engine's factory presets, each as
 * the [engine, preset] it loads on (retire.c eng_subst_k / eng_subst_preset), as a JS object literal.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/retired_dump tests/retired_dump.c -lm
 *   build/host/retired_dump
 * (SAMPLE, 4: the editor keeps a fifth entry, its once-PERC kit, by hand.) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
int main(void)
{
    uint32_t e, k, first = 1;
    printf("{");
    for (e = 0; e < NENGINES; e++) {
        uint32_t n;
        if (!eng_retired(e))
            continue;
        n = (uint32_t)strlen(PRESET_CAT[e]);
        if (!n)
            n = 1;
        printf("%s%u: [", first ? "" : ", ", e);
        first = 0;
        for (k = 0; k < n; k++)
            printf("%s[%u, %u]", k ? ", " : "", eng_subst_k(e, k), eng_subst_preset(e, k));
        printf("]");
    }
    printf("}\n");
    return 0;
}
