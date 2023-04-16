#include"webServer.h"
int main(int argc,char* argv[]){
    
    // if(argc < 2){
    //     printf("Usage : %s [PORT]\n",argv[0]);
    //     exit(-1);
    // }

    webServer server(8080);

    server.start();

    return 0;
}