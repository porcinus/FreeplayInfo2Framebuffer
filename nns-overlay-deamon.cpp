/*
NNS @ 2018
nns-overlay-deamon v0.1c
Use to create a 'OSD' on program running on gl or dispmanx driver
*/

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <limits.h>
#include <time.h>



FILE *temp_filehandle;								//file handle
int gpio_pin = -1;										//gpio pin
char gpio_path[PATH_MAX];							//gpio full path to sysfs
bool gpio_activelow=false;						//gpio active low
bool gpio_reverselogic=false;					//gpio reverselogic
char gpio_buffer[4];									//gpio read buffer
int gpio_interval=-1;									//gpio check interval
int gpio_value;												//gpio value
char *png_path;												//full path to str file
bool png_exist=false;									//file exist?
char program_path[PATH_MAX];					//full path to this program
int screen_width=-1;									//screen width
int bar_height=0;									  	//bar height
int duration = -1;										//video duration
//char omx_exec_path[PATH_MAX];					//full command line to run omx
//char ffmpeg_exec_path[PATH_MAX];	//full command line to run ffmpeg
char img2dispmanx_exec_path[PATH_MAX];								//full command line to run img2dispmanx


char icon_overheat_max_exec_path[PATH_MAX];		//full command line to run img2dispmanx cpu-overheat-max
char icon_overheat_warn_exec_path[PATH_MAX];	//full command line to run img2dispmanx cpu-overheat-warning
unsigned int icon_overheat_max_start = 0;			//time of last cpu-overheat-max run
unsigned int icon_overheat_warn_start = 0;		//time of last cpu-overheat-warning run
unsigned int tmp_time = 0;										//little opt
int rpi_cpu_temp = 0;													//rpi cpu temperature




void show_usage(void){
	printf("Example : ./nns-overlay-deamon -pin 41 -interval 200 -file \"/dev/shm/fb_footer.png\" -duration 5 -screenwidth 1024 -height 40\n");
	printf("Options:\n");
	printf("\t-pin, pin number corresponding to input to monitor\n");
	printf("\t-reverselogic, optional, reverse activelow logic\n");
	printf("\t-interval, optional, pin checking interval in msec\n");
	printf("\t-file, full path to png file, used for OSD\n");
	printf("\t-duration, in sec, used for OSD\n");
	printf("\t-screenwidth, screen width, used for OSD\n");
	printf("\t-height, bar height, optional, used for OSD\n");
}

int main(int argc, char *argv[]){
	if(argc<9){show_usage();return 1;} //wrong arguments count
	//if(access("/usr/bin/omxplayer",F_OK)!=0){printf("Failed, require OMXplayer\n");return 1;} //'omxplayer' is not installed
	//if(access("/usr/bin/ffmpeg",F_OK)!=0){printf("Failed, require ffmpeg\n");return 1;} //'ffmpeg' is not installed
	
	sleep(2);
	
	for(int i=1;i<argc;++i){ //argument to variable
		if(strcmp(argv[i],"-help")==0){show_usage();return 1;
		}else if(strcmp(argv[i],"-pin")==0){gpio_pin=atoi(argv[i+1]);snprintf(gpio_path,sizeof(gpio_path),"/sys/class/gpio/gpio%i/",gpio_pin);
		}else if(strcmp(argv[i],"-reverselogic")==0){gpio_reverselogic=true;
		}else if(strcmp(argv[i],"-interval")==0){gpio_interval=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-file")==0){png_path=(char*)argv[i+1];
		}else if(strcmp(argv[i],"-duration")==0){duration=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-screenwidth")==0){screen_width=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-height")==0){bar_height=atoi(argv[i+1]);}
	}
	
	if(gpio_pin<0||screen_width<0||duration<0){printf("Failed, missing some arguments\n");show_usage();return 1;} //user miss some needed arguments
	if(gpio_interval<100||gpio_interval>600){printf("Warning, wrong cheking interval set, setting it to 200msec\n");gpio_interval=200;} //wrong interval
	
	if(gpio_reverselogic){printf("Reversed activelow logic\n");}
	
	
	while(!png_exist){
		if(access(png_path,R_OK)!=0){
			printf("Failed, %s not readable, retrying\n",png_path);
			sleep(5);
		}else{
			printf("%s found\n",png_path);
			png_exist=true;
		}
	}
	
	
	
	if(access(gpio_path,R_OK)!=0){ //gpio not accessible, try to export
		printf("%s not accessible, trying export\n",gpio_path);
		temp_filehandle = fopen("/sys/class/gpio/export","wo"); fprintf(temp_filehandle,"%d", gpio_pin); fclose(temp_filehandle); //try export gpio
		if(access(gpio_path,R_OK)!=0){printf("Failed to export\n");return(1);}else{printf("Export with success\n");} //export gpio failed
	}
	


strncpy(program_path,argv[0],strlen(argv[0])-19); //backup program path
//printf("program path:%s\n",program_path);
if(strcmp(program_path,".")==0){
	getcwd(program_path,sizeof(program_path)); //backup program path
	//printf("program path:%s\n",program_path);
}
	

chdir(gpio_path); //change directory to gpio sysfs
	
	
	//printf("program path:%s\n",program_path);
	//printf("gpio path:%s\n",gpio_path);
	
	
	
	
	//check pin direction
	temp_filehandle = fopen("direction","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio direction
	if(strcmp(gpio_buffer,"out")==0){printf("Failed, gpio pin direction is %s\n",gpio_buffer);return(1); //check gpio direction
	}/*else{printf("GPIO: direction is %s\n",gpio_buffer);}*/
	
	//check if pin is active low
	temp_filehandle = fopen("active_low","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio active low
	if(strcmp(gpio_buffer,"1")==0){gpio_activelow=true;} //parse gpio active low
	//printf("GPIO: active_low is %s\n",gpio_buffer);
	
	//snprintf(omx_exec_path,sizeof(omx_exec_path),"omxplayer --no-osd --no-keys --alpha 150 --layer 20000 --win 0,0,%i,%i --align center --font-size 750 --no-ghost-box --subtitles \"%s\" \"%s/black.avi\" >/dev/null 2>&1",screen_width,bar_height,png_path,program_path); //parse command line for omx
	
	//snprintf(ffmpeg_exec_path,sizeof(ffmpeg_exec_path),"nice -5 ffmpeg -loglevel panic -y -loop 1 -i \"%s\" -t %i -r 5  -force_key_frames 1 -c:v mjpeg -f avi \"%s.avi\" >/dev/null 2>&1",png_path,duration,png_path); //parse command line for ffmpeg
	//snprintf(omx_exec_path,sizeof(omx_exec_path),"omxplayer --no-osd --no-keys --layer 20000 --win 0,0,%i,%i \"%s.avi\" >/dev/null 2>&1",screen_width,bar_height,png_path); //parse command line for omx
	
	
	snprintf(img2dispmanx_exec_path,sizeof(img2dispmanx_exec_path),"timeout %i %s/img2dispmanx -file \"%s\" -width %i -height %i -layer 20000  >/dev/null 2>&1",duration,program_path,png_path,screen_width,bar_height); //parse command line for img2dispmanx

	snprintf(icon_overheat_max_exec_path,sizeof(icon_overheat_max_exec_path),"%s/img2dispmanx -file \"%s/img/cpu-overheat-max.png\" -x 10 -y 60 -width 64 -layer 20002 -timeout 5 >/dev/null 2>&1 &",program_path,program_path); //parse command line for img2dispmanx
	snprintf(icon_overheat_warn_exec_path,sizeof(icon_overheat_warn_exec_path),"%s/img2dispmanx -file \"%s/img/cpu-overheat-warning.png\" -x 10 -y 60 -width 64 -layer 20001 -timeout 5 >/dev/null 2>&1 &",program_path,program_path); //parse command line for img2dispmanx




	while(true){
		chdir(gpio_path); //change directory to gpio sysfs
		temp_filehandle = fopen("value","r"); fgets(gpio_buffer,sizeof(gpio_buffer),temp_filehandle); fclose(temp_filehandle); //read gpio value
		gpio_value=atoi(gpio_buffer); //parse gpio value
		if((gpio_value==0&&(!gpio_activelow&&!gpio_reverselogic||gpio_activelow&&gpio_reverselogic))||(gpio_value==1&&(gpio_activelow&&!gpio_reverselogic||!gpio_activelow&&gpio_reverselogic))){ //gpio button pressed
			chdir(program_path); //change directory
			//system(ffmpeg_exec_path); //convert png to avi
			//system(omx_exec_path); //run omxplayer
			//printf("program path:%s\n",img2dispmanx_exec_path);
			//printf("%u\n", (unsigned)time(NULL));
			system(img2dispmanx_exec_path); //display overlay, blocking
			//printf("%u\n", (unsigned)time(NULL));
		}
		
		//read rpi temp value
		//temp_filehandle = fopen("/home/temp","r");
		temp_filehandle = fopen("/sys/class/thermal/thermal_zone0/temp","r");
		fscanf(temp_filehandle, "%i", &rpi_cpu_temp);
		fclose(temp_filehandle); 
		
		
		tmp_time=time(NULL);
		if((tmp_time - icon_overheat_warn_start)>5 && rpi_cpu_temp>=80000 && rpi_cpu_temp<85000){ //low overheat
			icon_overheat_warn_start=tmp_time;
			chdir(program_path); //change directory
			system(icon_overheat_warn_exec_path); //display overheat overlay, non blocking
			//printf("%u\n", (unsigned)time(NULL));
		}
		
		if((tmp_time - icon_overheat_max_start)>5 && rpi_cpu_temp>=85000){ //hot overheat
			icon_overheat_max_start=tmp_time;
			chdir(program_path); //change directory
			system(icon_overheat_max_exec_path); //display overheat overlay, non blocking
			//printf("%u\n", (unsigned)time(NULL));
		}
		
		usleep(gpio_interval*1000); //sleep
	}
	
	
	
	return(0);
}