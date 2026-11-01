#ifndef DOOM_INVULN_EFFECT_H
#define DOOM_INVULN_EFFECT_H
#ifdef __cplusplus
extern "C" {
#endif
/* Exact Player renderer only; all resources remain privately owned. */
int doom_invuln_effect_start(void *engine_module, void *renderer_module);
int doom_invuln_effect_set(int enabled);
int doom_invuln_effect_stop(void);
typedef struct DoomInvulnEffectDiagnostics {
    const char *stage;
    unsigned shader_calls, stock_binds, substitutions, selector_calls;
} DoomInvulnEffectDiagnostics;
void doom_invuln_effect_diagnostics(DoomInvulnEffectDiagnostics *out);
#ifdef __cplusplus
}
#endif
#endif
