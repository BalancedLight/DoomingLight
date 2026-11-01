#ifndef DOOM_INVULN_SHADER_H
#define DOOM_INVULN_SHADER_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomInvulnShader DoomInvulnShader;
/* Device must be a validated ID3D11Device. The returned shader is borrowed. */
DoomInvulnShader *doom_invuln_shader_create(void *device);
void *doom_invuln_shader_pixel(DoomInvulnShader *shader);
void doom_invuln_shader_destroy(DoomInvulnShader *shader);
#ifdef __cplusplus
}
#endif
#endif
