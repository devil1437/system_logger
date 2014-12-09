#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>

//#include <time.h>

#define CPU_NUM	4
#define SAMPLE_UNIT_UTIME 200000  /* 10Hz */
//#define SAMPLE_UNIT_UTIME 0 
#define SAMPLE_UNIT_SECOND 0
//#define SAMPLE_UNIT_SECOND 3

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
double util0, util1, util2, util3; 
unsigned long long lastwork0,lastwork1,lastwork2,lastwork3, workload0, workload1, workload2, workload3;
unsigned long long curwork0, curwork1, curwork2, curwork3;
unsigned long long idle0, idle1, idle2, idle3, lastidle0, lastidle1, lastidle2, lastidle3;
unsigned long long processR, ctxt, lastCtxt;


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
	/////////////////////////////////////////////
	
	FILE *fp_out = fopen(outfile_name, "a");

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
	workload0 = workload1 = workload2 = workload3 = 0;
	FILE *fp = fopen("/proc/stat","r");
	while(fgets(buff, 128, fp))
	{
		if(strstr(buff, "cpu0 "))
		{   
			sscanf(buff, "cpu0 %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &CPUInfo[0][0], &CPUInfo[0][1], &CPUInfo[0][2], &CPUInfo[0][3], &CPUInfo[0][4], &CPUInfo[0][5], &CPUInfo[0][6], &CPUInfo[0][7], &CPUInfo[0][8], &CPUInfo[0][9]); // time(unit: jiffies) spent of all cpus for: user nice system idle iowait irq softirq stead guest
			workload0 = CPUInfo[0][0]+CPUInfo[0][1]+CPUInfo[0][2]+CPUInfo[0][4]+CPUInfo[0][5]+CPUInfo[0][6]+CPUInfo[0][7];
			idle0 = CPUInfo[0][3] - lastidle0;
			curwork0 = workload0 - lastwork0;
			util0 = (double)curwork0 /(double)(curwork0+idle0);
		}else if(strstr(buff, "cpu1 "))
		{   
			sscanf(buff, "cpu1 %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &CPUInfo[1][0], &CPUInfo[1][1], &CPUInfo[1][2], &CPUInfo[1][3], &CPUInfo[1][4], &CPUInfo[1][5], &CPUInfo[1][6], &CPUInfo[1][7], &CPUInfo[1][8], &CPUInfo[1][9]); // time(unit: jiffies) spent of all cpus for: user nice system idle iowait irq softirq stead guest
			workload1 = CPUInfo[1][0]+CPUInfo[1][1]+CPUInfo[1][2]+CPUInfo[1][4]+CPUInfo[1][5]+CPUInfo[1][6]+CPUInfo[1][7];
			idle1 = CPUInfo[1][3] - lastidle1;
			curwork1 = workload1 - lastwork1;
			util1 = (double)curwork1 /(double)(curwork1+idle1);
		}else if(strstr(buff, "cpu2 "))
		{   
			sscanf(buff, "cpu2 %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &CPUInfo[2][0], &CPUInfo[2][1], &CPUInfo[2][2], &CPUInfo[2][3], &CPUInfo[2][4], &CPUInfo[2][5], &CPUInfo[2][6], &CPUInfo[2][7], &CPUInfo[2][8], &CPUInfo[2][9]); // time(unit: jiffies) spent of all cpus for: user nice system idle iowait irq softirq stead guest
			workload2 = CPUInfo[2][0]+CPUInfo[2][1]+CPUInfo[2][2]+CPUInfo[2][4]+CPUInfo[2][5]+CPUInfo[2][6]+CPUInfo[2][7];
			idle2 = CPUInfo[2][3] - lastidle2;
			curwork2 = workload2 - lastwork2;
			util2 = (double)curwork2 /(double)(curwork2+idle2);
		}else if(strstr(buff, "cpu3 "))
		{   
			sscanf(buff, "cpu3 %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &CPUInfo[3][0], &CPUInfo[3][1], &CPUInfo[3][2], &CPUInfo[3][3], &CPUInfo[3][4], &CPUInfo[3][5], &CPUInfo[3][6], &CPUInfo[3][7], &CPUInfo[3][8], &CPUInfo[3][9]); // time(unit: jiffies) spent of all cpus for: user nice system idle iowait irq softirq stead guest
			workload3 = CPUInfo[3][0]+CPUInfo[3][1]+CPUInfo[3][2]+CPUInfo[3][4]+CPUInfo[3][5]+CPUInfo[3][6]+CPUInfo[3][7];
			idle3 = CPUInfo[3][3] - lastidle3;
			curwork3 = workload3 - lastwork3;
			util3 = (double)curwork3 /(double)(curwork3+idle3);
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

	if(workload0 == 0) curwork0 = 0;
	if(workload1 == 0) curwork1 = 0;
	if(workload2 == 0) curwork2 = 0;
	if(workload3 == 0) curwork3 = 0;
	
	/* normalization */
	util0 = util0 * curFreq; // / maxFreq;
	util1 = util1 * curFreq; // / maxFreq;
	util2 = util2 * curFreq; // / maxFreq;
	util3 = util3 * curFreq; // / maxFreq;
	
	/* brightness */
	FILE *fp_bl = fopen("/sys/class/backlight/panel/brightness","r");
	int brightness;
	fgets(buff, 128, fp_bl);
	sscanf(buff,"%d",&brightness);

	/* network */
	char tcp_send[100];
	char tcp_rcv[100];
	sprintf(tcp_send,"/proc/uid_stat/%d/tcp_snd",uid);
	sprintf(tcp_rcv,"/proc/uid_stat/%d/tcp_rcv",uid);
	FILE *fp_snd = fopen(tcp_send,"r");
	FILE *fp_rcv = fopen(tcp_rcv,"r");
	int snd,rcv;
	fgets(buff, 128, fp_snd);
	sscanf(buff,"%d",&snd);
	fgets(buff, 128, fp_rcv);
	sscanf(buff,"%d",&rcv);

	
	/* output */
	fprintf(fp_out, "%d,%d,%llu,%.2f,%llu,%.2f,%llu,%.2f,%llu,%.2f,%llu,%llu,%d,%d,%d\n", cpu_on, curFreq, curwork0, util0, curwork1, util1, curwork2, util2, curwork3, util3, processR, ctxt-lastCtxt,brightness,snd,rcv);
	fclose(fp_out);
	//printf("%d, %llu, %.2f, %llu, %llu", 	curFreq, curwork0, util0, processR, ctxt-lastCtxt);
	
	/* update data */
	lastwork0 = workload0;
	lastwork1 = workload1;
	lastwork2 = workload2;
	lastwork3 = workload3;
	lastidle0 = CPUInfo[0][3];
	lastidle1 = CPUInfo[1][3];
	lastidle2 = CPUInfo[2][3];
	lastidle3 = CPUInfo[3][3];
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
	int res;
	char c;
	struct timezone timez;
	int sec, usec;
//#ifdef TARGET_ONLY
	//target_pid = 0;
	if(argc < 5){
		printf("please enter sample period\n");
		printf("usage: %s [sec] [usec] [output file] [uid]\n", argv[0]);
		return 0;
	}else{
		sscanf(argv[1], "%d", &sec);
		sscanf(argv[2], "%d", &usec);
		sscanf(argv[4], "%d", &uid);
		strcpy(outfile_name, argv[3]);
		tick.it_value.tv_sec = tick.it_interval.tv_sec = sec;
		tick.it_value.tv_usec = tick.it_interval.tv_usec = usec;
	}
//#endif
	/* initialize */
	lastwork0 = lastwork1 = workload0 = workload1 = 0;
	lastwork2 = lastwork3 = workload2 = workload3 = 0;
	idle0=idle1=lastidle0 =lastidle1 =0;
	idle2=idle3=lastidle2 =lastidle3 =0;
	ctxt = lastCtxt = 0;
	
	
	FILE *fp_out = fopen(outfile_name, "w");
	fprintf(fp_out, "cpu_on,curFreq,work_cycles0,util0,work_cycles1,util1,work_cycles2,util2,work_cycles3,util3,proc_running,ctxt,brightness,snd,rcv\n");
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


