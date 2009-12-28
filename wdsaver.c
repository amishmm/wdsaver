#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#define DEFAULT_HD_DEVICE "/dev/sda"
#define DEFAULT_HD_STATS_FILE "/sys/block/sda/stat"

#define HDPARM_PROG "/sbin/hdparm"
#define DEFAULT_HDPARM_OPTB 128

#define WD_DEFAULT_TIMEOUT 8
#define CHECK_PERIOD 4 //Thumbrule: atmost half of WD_DEFAULT_TIMEOUT

enum {  WD_RESET_METHOD_RANDOM_READ=1, WD_RESET_METHOD_HDPARM };

int timeout = 0;
FILE *stats_fp = NULL;
int verbosemode = 0, livemode = 0, bgmode = 0;
const char *hd_device = DEFAULT_HD_DEVICE;
const char *hd_stats_file = DEFAULT_HD_STATS_FILE;

void process(void);
void init(int argc, char **argv);
void print_usage(int argc, char **argv);
void (*wd_reset)(void);
void wd_reset_random_read(void);
void wd_reset_hdparm(void);

int wd_reset_method = WD_RESET_METHOD_RANDOM_READ;
char wdreset_command[1024];

int get_stats(unsigned int *rsect, unsigned int *wsect)
{
    //we cant fseek(stats_fp, 0, SEEK_SET) because values dont refresh with same FILE pointer
    if (stats_fp != NULL) fclose (stats_fp);//close old pointer
    if ((stats_fp = fopen (hd_stats_file, "r")) == NULL ||
        fscanf (stats_fp, "%*s%*s%u%*s%*s%*s%u", rsect, wsect) != 2 //exactly 2 items returned?
       ) {
        fprintf (stderr, "Could not get stats from file %s\n", hd_stats_file);
        return 0;
    }
    if (verbosemode) fprintf (stderr, "Read=%u, Wrote=%u\n", *rsect, *wsect);
    return 1;
}


int main(int argc, char **argv)
{
    init(argc, argv);
    process();
    exit (0);
}


void process(void)
{
    int idlestate = 0, noactivityseconds = 0;
    unsigned int rsect, wsect, last_rsect = 0, last_wsect = 0;
    if (!get_stats (&last_rsect, &last_wsect)) exit (1);
    while (1) {
        sleep (CHECK_PERIOD);
        if (!get_stats (&rsect, &wsect)) continue;
        if (last_rsect!=rsect || last_wsect!=wsect) {
            idlestate = noactivityseconds = 0;
            last_rsect = rsect; last_wsect = wsect;
        }
        else {
            noactivityseconds += CHECK_PERIOD;
            if (verbosemode && !idlestate) fprintf (stderr, "No activity from %d seconds\n", noactivityseconds);
            if (noactivityseconds < timeout) {
                wd_reset();
                if (wd_reset_method == WD_RESET_METHOD_RANDOM_READ) {
                    //since READ causes increase in READ sector count, we re-read the counters
                    if (get_stats (&rsect, &wsect)) { last_rsect = rsect; last_wsect = wsect; }
                }
            }
            else if (!idlestate) {
                idlestate = 1;
                if (verbosemode) fprintf (stderr, "Inactivity timeout reached, parking allowed\n");
            }
        }
    }
}


// random read makes head move every CHECK_PERIOD seconds
void wd_reset_random_read(void)
{
    static off_t t;
    static char buf[5];
    static int hd_fd = -1;
    static long maxrandom = 1024*1024*1024;//we assume HD has atleast 1GB capacity
    if (livemode) {
        if (hd_fd < 0) {
            if ((hd_fd = open (hd_device, O_RDONLY)) < 0) {
                fprintf (stderr, "Can't read from device %s\n", hd_device);
                return;
            }
            srandom (time(NULL));
        }
        t = lseek (hd_fd, maxrandom*((double)rand()/(double)RAND_MAX), SEEK_SET);
        if (verbosemode) fprintf (stderr, "Reading %ldth byte from %s\n", t, hd_device);
        read (hd_fd, buf, 1); 
    }
    else if (verbosemode) fprintf (stderr, "Not reading in testing (non-live) mode\n");
}


// writes disk attribute, runs separate process almost every CHECK_PERIOD seconds
void wd_reset_hdparm(void)
{
    if (verbosemode) fprintf (stderr, "Calling WD reset command%s: %s\n", livemode?"":" (not really!)", wdreset_command);
    if (livemode) system (wdreset_command);
}


/* read and set args, fork and go in background if being asked for */
void init(int argc, char **argv)
{
    int c;
    int hdparm_optB = 0;
    opterr = 0; optind = 1;
    while ((c = getopt (argc, argv, "bB:d:f:lm:ht:v")) != -1) {
        switch (c) {
            case 'b': bgmode = 1; break;
            case 'B': if (optarg && optarg[0]) hdparm_optB = atoi(optarg); break;
            case 'd': if (optarg && optarg[0]) hd_device = optarg; break;
            case 'f': if (optarg && optarg[0]) hd_stats_file = optarg; break;
            case 'l': livemode = 1; break;
            case 'm': if (optarg && optarg[0]) wd_reset_method = atoi(optarg); break;
            case 'h': print_usage(argc, argv); exit(0);
            case 't': if (optarg && optarg[0]) timeout = atoi(optarg); break;
            case 'v': verbosemode = 1; break;
            default : break;
        }
    }
    if (!livemode) verbosemode = 1;

    if (timeout <= WD_DEFAULT_TIMEOUT || (timeout % CHECK_PERIOD)) {
        if (!timeout) print_usage (argc, argv);
        else fprintf (stderr, "%s: timeout should be above %d and multiple of %d\n", argv[0], WD_DEFAULT_TIMEOUT, CHECK_PERIOD);
        exit (1);
    }

    if (!hdparm_optB) hdparm_optB = DEFAULT_HDPARM_OPTB;
    if (hdparm_optB < 1 || hdparm_optB > 255) {
        fprintf (stderr, "%s: option -B should be >=1 and <=255\n", argv[0]);
        exit (1);
    }

    if (wd_reset_method == WD_RESET_METHOD_RANDOM_READ) wd_reset = wd_reset_random_read;
    else if (wd_reset_method == WD_RESET_METHOD_HDPARM) wd_reset = wd_reset_hdparm;
    else {
        fprintf (stderr, "%s: invalid method number\n", argv[0]);
        exit (1);
    }

    struct stat s_buf;
    if (stat (hd_device, &s_buf) || !S_ISBLK(s_buf.st_mode)) {
        fprintf (stderr, "%s: harddisk device (%s) is not a block device\n", argv[0], hd_device);
        exit (1);
    }
    if (stat (hd_stats_file, &s_buf) || !S_ISREG(s_buf.st_mode)) {
        fprintf (stderr, "%s: harddisk stats file (%s) is not a regular file\n", argv[0], hd_stats_file);
        exit (1);
    }

    if (verbosemode) {
        fprintf (stderr, "Timeout=%d, OptB=%d, Livemode=%d, Background=%d\n", timeout, hdparm_optB, livemode, bgmode);
        fprintf (stderr, "Method=%d, HD Device=%s, HD stats file=%s\n", wd_reset_method, hd_device, hd_stats_file);
    }

    if (bgmode) {
        if (verbosemode) fprintf (stderr, "Forking and going to background\n");
        pid_t c = fork();
        if (c < 0) { fprintf (stderr, "fork() failed ... exiting\n"); exit(1); }
        if (c) { if (verbosemode) fprintf (stderr, "Parent exiting. Child process %d continues\n", c); exit (0); }
        setsid();
    }

    snprintf (wdreset_command, 1024, "%s -B %d '%s'", HDPARM_PROG, hdparm_optB, hd_device);
    if (verbosemode && wd_reset_method==WD_RESET_METHOD_HDPARM) fprintf (stderr, "WD reset command: %s\n", wdreset_command);
}


void print_usage(int argc, char **argv)
{
    fprintf (stderr, "Usage: %s -t N [-B N] [-b] [-d hd_device] [-f hd_stats_file] [-l] [-h] [-m N] [-v]\n", argv[0]);
    fprintf (stderr, "  -t: timeout in seconds (above %d and multiple of %d).\n", WD_DEFAULT_TIMEOUT, CHECK_PERIOD);
    fprintf (stderr, "      It represents inactivity period after which disk can park the heads\n");
    fprintf (stderr, "  -B: value for '-B' parameter of %s (default=%d)\n", HDPARM_PROG, DEFAULT_HDPARM_OPTB);
    fprintf (stderr, "  -d: harddisk device (default=%s)\n", DEFAULT_HD_DEVICE);
    fprintf (stderr, "  -f: /sys format stats file for harddisk device (default=%s)\n", DEFAULT_HD_STATS_FILE);
    fprintf (stderr, "  -m: method to use (number) to reset timer\n");
    fprintf (stderr, "      %d = random read from disk\n", WD_RESET_METHOD_RANDOM_READ);
    fprintf (stderr, "          (default) read only method but disk head moves frequently\n");
    fprintf (stderr, "      %d = hdparm -B option\n", WD_RESET_METHOD_HDPARM);
    fprintf (stderr, "          frequently writes disk attribute using hdparm\n");
    fprintf (stderr, "  -l: activate live mode (program doesn't really reset timer otherwise)\n");
    fprintf (stderr, "  -b: fork and go into background mode\n");
    fprintf (stderr, "  -v: verbose mode (to stderr) (on by default if NOT in livemode)\n");
    fprintf (stderr, "  -h: print this help and exit\n");
    fprintf (stderr, "\nNOTE: sysfs support is required (all new distro support it)\n");
    fprintf (stderr, "TIP: 1st test w/o live mode. once tested with live mode, put in rc.local\n");
    fprintf (stderr, "\nWritten by amish, hdparm idea by blackshard\n");
    fprintf (stderr, "\nLICENSE - GPL v3.0 at http://www.gnu.org/licenses/gpl-3.0.txt\n");
    fprintf (stderr, "\nDISCLAIMER - author takes no responsibility, use at your own risk.\n");
}
