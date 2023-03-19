#include"webServer.h"
int main(int argc,char* argv[]){
    
    if(argc < 2){
        printf("Usage : %s [PORT]\n",argv[0]);
        exit(-1);
    }

    webServer server(atoi(argv[1]));

    server.start();

    return 0;
}