/* file I/O */
int    cfg_parse_file(char *dname, char *filename);
int    cfg_write_file(char *dname, char *filename);

#if 0
void   cfg_parse_option(char *section, char *tag, char *value);
void   cfg_parse_options(int *argc, char **argv);
#endif

/* update */
void   cfg_set_str(char *dname, char *sname, char *ename, const char *value);
void   cfg_set_int(char *dname, char *sname, char *ename, int value);
void   cfg_set_bool(char *dname, char *sname, char *ename, int value);

void   cfg_del_section(char *dname, char *sname);
void   cfg_del_entry(char *dname, char *sname, char *ename);

/* search */
char*  cfg_sections_first(char *dname);
char*  cfg_sections_next(char *dname, char *current);
char*  cfg_sections_prev(char *dname, char *current);
int    cfg_sections_count(char *dname);
char*  cfg_sections_index(char *dname, int i);

char*  cfg_entries_first(char *dname, char *sname);
char*  cfg_entries_next(char *dname, char *sname, char *current);
char*  cfg_entries_prev(char *dname, char *sname, char *current);
int    cfg_entries_count(char *dname, char *sname);
char*  cfg_entries_index(char *dname, char *sname, int i);

char*  cfg_search(char *dname, char *sname, char *ename, char *value);

/* read */
char*  cfg_get_str(char *dname, char *sname, char *ename);
int    cfg_get_int(char *dname, char *sname, char *ename,
		   int def);
int    cfg_get_signed_int(char *dname, char *sname, char *ename,
			  unsigned int def);
float  cfg_get_float(char *dname, char *sname, char *ename);
int    cfg_get_bool(char *dname, char *sname, char *ename,
		    int def);

int    cfg_get_sflags(char *dname, char *sname);
int    cfg_get_eflags(char *dname, char *sname, char *ename);
int    cfg_set_sflags(char *dname, char *sname,
		      unsigned int mask, unsigned int bits);
int    cfg_set_eflags(char *dname, char *sname, char *ename,
		      unsigned int mask, unsigned int bits);
