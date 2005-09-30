/* state */
extern char *curr_station;
extern char *curr_channel;
extern char curr_details[];
extern int  curr_tsid;
extern int  curr_pnr;

/* analog */
char* freqtab_get(void);
void freqtab_set(char *name);
int freqtab_lookup(char *channel);
int tune_analog_freq(unsigned int freq);
int tune_analog_channel(char *cname);
int tune_analog_station(char *station);

/* dvb */
int tune_dvb_channel(char *pr);

/* generic */
int tune_station(char *station);
