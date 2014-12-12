#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>

#include "profile_frameRate.h"

long min(long a, long b){
	return a > b ? b : a;
}

float getFrameRate(long utime){
	FILE *pp;
	float fps;	
	char line[128];
	char *finishFrameTime;
	char finishTime[128][32];
	float timeIntervalNanoseconds=min(1000*1000*1000, utime*1000);
	float frameCount = 0;
	int i=0;
	
	pp=popen("dumpsys SurfaceFlinger --latency SurfaceView","r");
	
	fgets(line, sizeof(line), pp);
	for ( i = 0; i < 128; i++)
	{
		fgets(line, sizeof line, pp);

		strtok(line, "\t");
		strtok(NULL, "\t");
		finishFrameTime = strtok(NULL, "\t");
		strcpy(finishTime[i], finishFrameTime);
	}
	
	pclose(pp);
	frameCount=0;
	for ( i = 0; i < 128; i++)
	{
		if ((atof(finishTime[127]) - atof(finishTime[i]))<= timeIntervalNanoseconds)
		{
			frameCount=frameCount+1.0;
		}
	}
	fps=(double)frameCount*(1000000000/timeIntervalNanoseconds);
	return fps;
}
