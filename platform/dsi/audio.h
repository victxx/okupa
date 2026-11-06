#ifndef OKUPA_DSI_AUDIO_H
#define OKUPA_DSI_AUDIO_H

/* Music on the DSi: the game's synthesiser fills a looping 16-bit sample
 * buffer just ahead of where the sound hardware is playing, from a timer
 * interrupt, so slow frames never starve it. */
void ok_dsi_audio_start(void);
/* Does nothing now (the interrupt fills the buffer); kept for old callers. */
void ok_dsi_audio_pump(void);
void ok_dsi_audio_tune(int tune);
void ok_dsi_audio_sfx(int sfx);

#endif
