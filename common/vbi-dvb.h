#if (VBI_VERSION_MAJOR == 0) && (VBI_VERSION_MINOR <= 2) && (VBI_VERSION_MICRO <= 5)

vbi_capture*
vbi_capture_dvb_new(char *dev, int scanning,
		    unsigned int *services, int strict,
		    char **errstr, vbi_bool trace);
int vbi_capture_dvb_filter(vbi_capture *cap, int pid);

#endif
