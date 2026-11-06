#ifndef OKUPA_DSI_AUDIO_H
#define OKUPA_DSI_AUDIO_H

/* Music on the DSi: the game's synthesiser fills a looping 16-bit sample
 * buffer just ahead of where the sound hardware is playing. */
void ok_dsi_audio_start(void);
/* Call often (every frame, and while waiting on something). */
void ok_dsi_audio_pump(void);
void ok_dsi_audio_tune(int tune);
void ok_dsi_audio_sfx(int sfx);

#endif
