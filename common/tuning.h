/* state */
extern char *curr_station;
extern char *curr_channel;
extern char curr_details[];

/* analog */
extern char *freqtab;
void freqtab_set(char *name);
int freqtab_lookup(char *channel);
int tune_analog_freq(int freq);
int tune_analog_channel(char *cname);
int tune_analog_station(char *station);

/* dvb */

/* generic */
int tune_station(char *station);
