#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>

//#define ENABLE_NETWORK
#define DEBUG

#define CPU_NUM	4
#define DEFAULT_SAMPLE_UNIT_UTIME 100000  // Unit: us
#define DEFAULT_SAMPLE_UNIT_SECOND 0
#define DEFAULT_UID 10222
#define DEFAULT_OUTPUT_NAME "/sdcard/systemLogger/output.csv"

#define CONFIG_HZ 100
#define BUFF_SIZE 513
#define PID_MAX 32768

/** For thread profiling **/
typedef struct threadConf{
	int pid;
	char name[65];
	char state;
	int gid;
	int uid;
	unsigned long int utime;
	unsigned long int stime;
	unsigned long int old_utime;
	unsigned long int old_stime;
	long int prio;
	long int nice;
	int last_cpu;
}ThreadConf;

ThreadConf thread;
int target_pid;
/************************/

struct timeval oldTime;
struct itimerval tick;
int stopFlag = 0;

unsigned long long CPUInfo[CPU_NUM][10];

char buff[129];
char outfile_name[129];
int curFreq, maxFreq;
int cpu_on;
int uid;
double util[CPU_NUM]; 
unsigned long long lastwork[CPU_NUM], workload[CPU_NUM];
unsigned long long curwork[CPU_NUM];
unsigned long long idle[CPU_NUM], lastidle[CPU_NUM];
unsigned long long processR, ctxt, lastCtxt, count;


/**************** non-blocking input *************/
struct termios orig_termios;

int kbhit()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(0, &fds); //STDIN_FILENO is 0
    select(1, &fds, NULL, NULL, &tv);
    return FD_ISSET(0, &fds);
}

void reset_terminal_mode()
{
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
{
    struct termios new_termios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    /* register cleanup handler, and set the new terminal mode */
    //atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}


/****************************************************/


void initThreadConf(){
	thread.pid = thread.utime = thread.stime = thread.prio = thread.nice = 0;
	thread.old_stime = thread.old_utime = 0;
	thread.gid = thread.uid = -1;
	thread.state = '\0';
	thread.name[0] = '\0';
	thread.last_cpu = -1;
}

void getCPUMaxFreq(int cpu_num){

	FILE *fp;
	char buff[128];
	
	sprintf(buff, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu_num);
	fp = fopen(buff, "r");
	if(fp == NULL){
		printf("error: can't open scaling_max_freq\n");
		maxFreq = 1024000;
		return;
	}else{
		printf("get max frequency\n");
		fscanf(fp, "%d", &maxFreq);
	}
	fclose(fp);
} 

void parse(){
	char buff[128];
	int i;
	struct timeval tv;
	time_t nowtime;
	struct tm *nowtm;
	char timeStr[256] = "";
	int gpuFreq = 0, tmpGpuUtil = 0;
	double gpuUtil = 0;
	/////////////////////////////////////////////
	
	FILE *fp_out = fopen(outfile_name, "a");

	/* get current time */
	gettimeofday(&tv,NULL);
	nowtime = tv.tv_sec;
	nowtm = localtime(&nowtime);
	strftime(buff, sizeof(buff), "%Y-%m-%d-%H:%M:%S", nowtm);
	snprintf(timeStr, sizeof(timeStr), "%s.%06ld", buff, tv.tv_usec);

	/* profile cpu frequency */
	FILE *fp_freq = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
	fscanf(fp_freq, "%d", &curFreq);
	fclose(fp_freq);
	
	/* profile # of cpu on */
	FILE *fp_cpuon = fopen("/sys/devices/system/cpu/online", "r");
	fscanf(fp_cpuon, "%s", buff);
	fclose(fp_cpuon);
	cpu_on = 1;
	for(i = 0; i < strlen(buff); i++)
	{
		if(buff[i] == ',')
			cpu_on++;
		else if(buff[i] == '-')
			cpu_on += buff[i+1] - buff[i-1];
	}
		
	/* profile cpu util. */
	FILE *fp = fopen("/proc/stat","r");
	while(fgets(buff, 128, fp))
	{
		for(i = 0; i < CPU_NUM; i++)
		{
			char findStr[128];
			sprintf(findStr, "cpu%d ", i);
			if(strstr(buff, findStr))
			{   
				// time(unit: jiffies) spent of all cpus for: user nice system idle iowait irq softirq stead guest
				sscanf(buff, "%s%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", findStr, &CPUInfo[i][0], &CPUInfo[i][1], &CPUInfo[i][2], &CPUInfo[i][3], &CPUInfo[i][4], &CPUInfo[i][5], &CPUInfo[i][6], &CPUInfo[i][7], &CPUInfo[i][8], &CPUInfo[i][9]); 
				workload[i] = CPUInfo[i][0]+CPUInfo[i][1]+CPUInfo[i][2]+CPUInfo[i][4]+CPUInfo[i][5]+CPUInfo[i][6]+CPUInfo[i][7];
				idle[i] = CPUInfo[i][3] - lastidle[i];
			}		
			curwork[i] = workload[i] - lastwork[i];
			// In order to avoid div0 fault, we need to check the value.
			util[i] = curwork == 0 ? 0 : (double)curwork[i] /(double)(curwork[i]+idle[i]);
		}
		if(strstr(buff, "procs_running"))
		{
			sscanf(buff, "procs_running %llu", &processR); // # of processes running
		}
		if(strstr(buff, "ctxt"))
		{
			sscanf(buff, "ctxt %llu", &ctxt); // # of context switch
		}
	}
	fclose(fp);

	/* brightness */
	int brightness;
	FILE *fp_bl = fopen("/sys/class/backlight/panel/brightness","r");	
	fscanf(fp_bl, "%d", &brightness);
	fclose(fp_bl);
	
#ifdef ENABLE_NETWORK
	/* network */
	char tcp_send[100];
	char tcp_rcv[100];
	sprintf(tcp_send,"/proc/uid_stat/%d/tcp_snd",uid);
	sprintf(tcp_rcv,"/proc/uid_stat/%d/tcp_rcv",uid);
	FILE *fp_snd = fopen(tcp_send,"r");
	FILE *fp_rcv = fopen(tcp_rcv,"r");
	int snd,rcv;
	fscanf(fp_snd, "%d", &snd);
	fclose(fp_snd);
	fscanf(fp_rcv, "%d", &rcv);
	fclose(fp_rcv);
#else
	int snd = 0,rcv = 0;
#endif

	/* GPU */
	FILE *fp_gpuFreq = fopen("/sys/module/pvrsrvkm/parameters/sgx_gpu_clk","r");
	fscanf(fp_gpuFreq, "%d", &gpuFreq);
	fclose(fp_gpuFreq);
	FILE *fp_gpuUtil = fopen("/sys/module/pvrsrvkm/parameters/sgx_gpu_utilization","r");
	fscanf(fp_gpuUtil, "%d", &tmpGpuUtil);
	fclose(fp_gpuUtil);
	
	// The range of gpu's utilization [0:256]
	gpuUtil = (double)tmpGpuUtil / (double)256;

	/* output */
	fprintf(fp_out, "%s,%d,%d", timeStr, cpu_on, curFreq);
	for(i = 0; i < CPU_NUM; i++){
		fprintf(fp_out, ",%llu,%.4f", curwork[i], util[i]);
	}
	fprintf(fp_out, ",%llu,%llu,%d,%d,%d,%d,%.4f\n", processR, ctxt-lastCtxt,brightness,snd,rcv, gpuFreq, gpuUtil);
	fclose(fp_out);

#ifdef DEBUG	
	printf("%llu,%s,%d,%d", count++, timeStr, cpu_on, curFreq);
	for(i = 0; i < CPU_NUM; i++){
		printf(",%llu,%.4f", curwork[i], util[i]);
	}
	printf(",%llu,%llu,%d,%d,%d,%d,%.4f\n", processR, ctxt-lastCtxt,brightness,snd,rcv, gpuFreq, gpuUtil);
#endif
	
	
	/* update data */
	for(i = 0; i < CPU_NUM; i++){
		lastwork[i] = workload[i];
		lastidle[i] = CPUInfo[i][3];
	}
	lastCtxt = ctxt;

	if(stopFlag == 1)
	{
		tick.it_value.tv_sec = 0;
		tick.it_value.tv_usec = 0;
		tick.it_interval.tv_sec = 0;
		tick.it_interval.tv_usec = 0;
		setitimer(ITIMER_REAL, &tick, NULL);
		
		printf("End!!!\n");
		stopFlag = 2;
		exit(0);
	}
}

int main(int argc, char **argv)
{
	int res, i;
	char c;
	struct timezone timez;
	int sec, usec;
//#ifdef TARGET_ONLY
	//target_pid = 0;
	if(argc == 1){
		sec = DEFAULT_SAMPLE_UNIT_SECOND;
		usec = DEFAULT_SAMPLE_UNIT_UTIME;
		uid = DEFAULT_UID;
		strcpy(outfile_name, DEFAULT_OUTPUT_NAME);
		tick.it_value.tv_sec = tick.it_interval.tv_sec = sec;
		tick.it_value.tv_usec = tick.it_interval.tv_usec = usec;
	} else if(argc < 5) {
		printf("please enter sample period\n");
		printf("usage: %s [sec] [usec] [output file] [uid]\n", argv[0]);
		return 0;
	} else {
		sscanf(argv[1], "%d", &sec);
		sscanf(argv[2], "%d", &usec);
		sscanf(argv[4], "%d", &uid);
		strcpy(outfile_name, argv[3]);
		tick.it_value.tv_sec = tick.it_interval.tv_sec = sec;
		tick.it_value.tv_usec = tick.it_interval.tv_usec = usec;
	}
//#endif
	/* initialize */
	for(i = 0; i < CPU_NUM; i++){
		lastwork[i] = 0;
		workload[i] = 0;
		idle[i] = 0;
		lastidle[i] = 0;
	}
	ctxt = lastCtxt = count = 0;
	
	
	FILE *fp_out = fopen(outfile_name, "w");
	fprintf(fp_out, "time,cpu_on,curFreq");
	for(i = 0; i < CPU_NUM; i++){
		fprintf(fp_out, ",work_cycles%d,util%d", i, i);
	}
	fprintf(fp_out, ",proc_running,ctxt,brightness,snd,rcv,gpuFreq,gpuUtil\n");
	fclose(fp_out);
/*	
	FILE *fp_thout = fopen("/sdcard/cuckoo_thprof.csv", "a");
	fprintf(fp_thout, "pid, gid, state, utime, stime, util, priority, nice value, last_cpu\n");
	fclose(fp_thout);
*/	
	
	getCPUMaxFreq(0);

	/**
	 * Timer interrupt handler
	 */
	signal(SIGALRM, parse);             /* SIGALRM handeler */
	
	res = setitimer(ITIMER_REAL, &tick, NULL);
	gettimeofday(&oldTime, &timez);
	printf("start!\n");

	// continue until user pressed 'p'

	set_conio_terminal_mode();
	while(1)
	{   	
		while(!kbhit()){
			
		} //non-blocking check input

		c = fgetc(stdin);
		if(c == 'p') 
		{   
			stopFlag = 1;
			break;
		}   
	}
	reset_terminal_mode();
	while(stopFlag != 2);
	
	return 0;
}


