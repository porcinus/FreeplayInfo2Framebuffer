/*
NNS @ 2019
nns-overlay-deamon
Use to create a 'OSD' on program running on dispmanx driver
*/
const char programversion[]="0.1i";

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <limits.h>
#include <time.h>
#include <wiringPi.h> //wiringpi
#include <libgen.h>


int debug=0; //program is in debug mode, 0=no 1=full
#define debug_print(fmt, ...) do { if (debug) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0) //Flavor: print advanced debug to stderr

FILE *temp_filehandle;								//file handle
bool standalone=false;								//standalone mode
bool alsamixer_enabled=true;					//alsa boolean
int info2png_height=12;							//standalone overlay height
int gpio_pin=41;											//gpio pin
int gpio_lowbatpin=-1;								//gpio pin for low battery
bool gpio_activelow=false;						//gpio active low
bool gpio_lowbatactivelow=false;			//gpio active low for low battery
bool gpio_reverselogic=false;					//gpio reverselogic
bool gpio_lowbatreverselogic=false;		//gpio reverselogic for low battery
int gpio_interval=200;								//gpio check interval
int gpio_value;												//gpio value
int gpio_lowbatvalue;									//gpio value for low battery
char png_path[PATH_MAX];							//full path to str file
bool png_exist=false;									//file exist?
char program_path[PATH_MAX];					//full path to this program
int duration=5;												//osd duration
char info2png_exec_path[PATH_MAX];						//full command line to run info2png
char *info2png_output_path;										//full path of info2png output
char img2dispmanx_exec_path[PATH_MAX];				//full command line to run img2dispmanx
char icon_overheat_max_exec_path[PATH_MAX];		//full command line to run img2dispmanx cpu-overheat-max
char icon_overheat_warn_exec_path[PATH_MAX];	//full command line to run img2dispmanx cpu-overheat-warning
char icon_lowbat_exec_path[PATH_MAX];					//full command line to run img2dispmanx low-battery
unsigned long img2dispmanx_start=0;					//time of last overlay run
unsigned long icon_lowbat_start=0;					//time of last low bat run
unsigned long icon_overheat_max_start=0;		//time of last cpu-overheat-max run
unsigned long icon_overheat_warn_start=0;		//time of last cpu-overheat-warning run
unsigned long tmp_time=0;										//little opt
int rpi_cpu_temp=0;													//rpi cpu temperature
char alsa_card[256];							//alsa card
char alsa_name[256];							//alsa name



void show_usage(void){
	fprintf(stderr,
"Example : ./nns-overlay-deamon -standalone -height 12 -pin 41 -reverselogic -interval 200 -file \"/dev/shm/fb_footer.png\" -duration 5\n"
"Version: %s\n"
"Options:\n"
"\t-standalone, assume that info2png not running as service, run it each time GPIO pin is trigger, recommended for Pi Zero user but add 1sec delay to overlay display\n"
"\t-height, used only in standalone mode, overlay height in px [Default: 12]\n"
"\t-pin, GPIO pin use to display OSD [Default: 41]\n"
"\t-reverselogic, optional, reverse activelow logic\n"
"\t-interval, optional, pin checking interval in msec [Default: 200]\n"
"\t-file, full path to png file, used for OSD [Default: /dev/shm/fb_footer.png]\n"
"\t-duration, in sec, used for OSD [Default: 5]\n"
"\t-lowbatpin, optional, GPIO pin used to signal low battery, disable if not set\n"
"\t-lowbatreverselogic, optional, reverse activelow logic for lowbatpin\n"
"\t-alsavolume, optional, enable ALSA volume [Default: 0]\n"
"\t-alsacard, optional, ALSA card [Default: default]\n"
"\t-alsaname, optional, ALSA selector name [Default: Master]\n"
"\t-debug, optional, 1=full(will spam logs), 0 if not set\n\n"
,programversion);
}

int main(int argc, char *argv[]){
	strcpy(png_path,"/dev/shm/fb_footer.png"); //init
	strcpy(alsa_card,"default"); //init
	strcpy(alsa_name,"Master"); //init
	
	for(int i=1;i<argc;++i){ //argument to variable
		if(strcmp(argv[i],"-help")==0){show_usage();return 1;
		}else if(strcmp(argv[i],"-debug")==0){debug=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-standalone")==0){standalone=true;
		}else if(strcmp(argv[i],"-height")==0){info2png_height=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-pin")==0){gpio_pin=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-reverselogic")==0){gpio_reverselogic=true;
		}else if(strcmp(argv[i],"-lowbatpin")==0){gpio_lowbatpin=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-lowbatreverselogic")==0){gpio_lowbatreverselogic=true;
		}else if(strcmp(argv[i],"-interval")==0){gpio_interval=atoi(argv[i+1]);
		}else if(strcmp(argv[i],"-alsavolume")==0){if(atoi(argv[i+1])>0){alsamixer_enabled=true;}
		}else if(strcmp(argv[i],"-alsacard")==0){strcpy(alsa_card,argv[i+1]);alsamixer_enabled=true;
		}else if(strcmp(argv[i],"-alsaname")==0){strcpy(alsa_name,argv[i+1]);alsamixer_enabled=true;
		}else if(strcmp(argv[i],"-file")==0){strcpy(png_path,argv[i+1]);
		}else if(strcmp(argv[i],"-duration")==0){duration=atoi(argv[i+1]);}
	}
	
	if(debug){fprintf(stderr,"nns-overlay-deamon : Running in debug mode\n");}
	
	if(alsamixer_enabled){fprintf(stderr,"nns-overlay-deamon : ALSA : %s : %s\n",alsa_card,alsa_name);}
	
	if(standalone){
		fprintf(stderr,"nns-overlay-deamon : Running in standalone mode\n");
		fprintf(stderr,"nns-overlay-deamon : info2png height : %dpx\n",info2png_height);
		char png_path_tmp[sizeof(png_path)]; strcpy(png_path_tmp,png_path); //backup
		info2png_output_path=dirname(png_path_tmp); //extract info2png output path
	}
	
	if(gpio_pin<0||duration<0){fprintf(stderr,"nns-overlay-deamon : Failed, missing some arguments\n");show_usage();return 1;} //user miss some needed arguments
	if(gpio_interval<100||gpio_interval>600){fprintf(stderr,"nns-overlay-deamon : Warning, wrong cheking interval set, setting it to 200msec\n");gpio_interval=200;} //wrong interval
	if(gpio_reverselogic){fprintf(stderr,"nns-overlay-deamon : Reversed activelow logic\n");}
	
	if(gpio_lowbatpin<0){fprintf(stderr,"nns-overlay-deamon : Warning, no pin set to monitor low battery, skipped\n");}else{
	if(gpio_lowbatreverselogic){fprintf(stderr,"nns-overlay-deamon : Reversed low battery activelow logic\n");}}
	
	while(wiringPiSetupGpio()==-1){
		fprintf(stderr,"nns-overlay-deamon : wiringPi not initialized, retrying in 5sec\n",png_path);
		sleep(2);
	}
	
	if(getAlt(gpio_pin)!=0){fprintf(stderr,"nns-overlay-deamon : Failed, GPIO pin is not a input\n");return(1);
	}else if(!digitalRead(gpio_pin)){gpio_activelow=true;}
	
	if(gpio_lowbatpin>-1){
		if(getAlt(gpio_lowbatpin)!=0){fprintf(stderr,"nns-overlay-deamon : Failed, GPIO low battery pin is not a input\n");return(1);
		}else if(!digitalRead(gpio_lowbatpin)){gpio_lowbatactivelow=true;}
	}
	
	if(!standalone){
		while(access(png_path,R_OK)!=0){
			fprintf(stderr,"nns-overlay-deamon : Failed, %s not readable, retrying in 5sec\n",png_path);
			sleep(5);
		}
	}
	
	strncpy(program_path,argv[0],strlen(argv[0])-19); //backup program path
	if(strcmp(program_path,".")==0){
		getcwd(program_path,sizeof(program_path)); //backup program path
	}
	
	sprintf(info2png_exec_path,"%s/info2png -height %d -o \"%s\"",program_path,info2png_height,info2png_output_path); //parse command line for info2png
	if(standalone){sprintf(info2png_exec_path,"%s -runonce",info2png_exec_path);} //standalone
	if(debug){sprintf(info2png_exec_path,"%s -debug %d",info2png_exec_path,debug);} //debug
	if(alsamixer_enabled){sprintf(info2png_exec_path,"%s -alsacard %s -alsaname %s",info2png_exec_path,alsa_card,alsa_name);} //alsa
	
	sprintf(info2png_exec_path,"%s >/dev/null 2>&1",info2png_exec_path); //finish build info2png command
	sprintf(img2dispmanx_exec_path,"%s/img2dispmanx -file \"%s\" -width FILL -layer 20000 -timeout %d  >/dev/null 2>&1 &",program_path,png_path,duration); //parse command line for img2dispmanx
	sprintf(icon_overheat_max_exec_path,"%s/img2dispmanx -file \"%s/img/cpu-overheat-max.png\" -x 10 -y 60 -width 64 -layer 20002 -timeout 5 >/dev/null 2>&1 &",program_path,program_path); //parse command line for img2dispmanx
	sprintf(icon_overheat_warn_exec_path,"%s/img2dispmanx -file \"%s/img/cpu-overheat-warning.png\" -x 10 -y 60 -width 64 -layer 20001 -timeout 5 >/dev/null 2>&1 &",program_path,program_path); //parse command line for img2dispmanx
	if(gpio_lowbatpin>-1){sprintf(icon_lowbat_exec_path,"%s/img2dispmanx -file \"%s/img/low-battery.png\" -x 80 -y 60 -width 64 -layer 20001 -timeout 5 >/dev/null 2>&1 &",program_path,program_path);} //parse command line for img2dispmanx
	
	while(true){
		tmp_time=time(NULL); //loop start time
		gpio_value=digitalRead(gpio_pin);
		
		if((tmp_time-img2dispmanx_start)>=duration && ((gpio_value==0&&(!gpio_activelow&&!gpio_reverselogic||gpio_activelow&&gpio_reverselogic))||(gpio_value==1&&(gpio_activelow&&!gpio_reverselogic||!gpio_activelow&&gpio_reverselogic)))){ //gpio button pressed
			chdir(program_path); //change directory
			if(standalone){system(info2png_exec_path);} //run info2png, blochink mode
			if(access(png_path,R_OK)!=0){
				fprintf(stderr,"nns-overlay-deamon : Can't display, %s not readable\n",png_path);
			}else{
				system(img2dispmanx_exec_path); //display overlay, non blocking
				img2dispmanx_start=tmp_time;
			}
		}
		
		if(gpio_lowbatpin>-1){ //low battery enable
			gpio_lowbatvalue=digitalRead(gpio_lowbatpin);
			if((tmp_time-icon_lowbat_start)>=5 && ((gpio_lowbatvalue==0&&(!gpio_lowbatactivelow&&!gpio_lowbatreverselogic||gpio_lowbatactivelow&&gpio_lowbatreverselogic))||(gpio_lowbatvalue==1&&(gpio_lowbatactivelow&&!gpio_lowbatreverselogic||!gpio_lowbatactivelow&&gpio_lowbatreverselogic)))){ //gpio button pressed
				chdir(program_path); //change directory
				system(icon_lowbat_exec_path); //display lowbat icon, non blocking
				icon_lowbat_start=tmp_time;
			}
		}
		
		//read rpi temp value
		temp_filehandle=fopen("/sys/class/thermal/thermal_zone0/temp","r");
		fscanf(temp_filehandle, "%i", &rpi_cpu_temp);
		fclose(temp_filehandle);
		
		if((tmp_time-icon_overheat_warn_start)>=5 && rpi_cpu_temp>=80000 && rpi_cpu_temp<85000){ //low overheat
			chdir(program_path); //change directory
			system(icon_overheat_warn_exec_path); //display overheat icon, non blocking
			icon_overheat_warn_start=tmp_time;
		}
		
		if((tmp_time-icon_overheat_max_start)>=5 && rpi_cpu_temp>=85000){ //hot overheat
			chdir(program_path); //change directory
			system(icon_overheat_max_exec_path); //display overheat icon, non blocking
			icon_overheat_max_start=tmp_time;
		}
		
		usleep(gpio_interval*1000); //sleep
	}
	
	return(0);
}