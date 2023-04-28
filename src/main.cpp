#include"webServer.h"
int main(int argc,char* argv[]){
    webServer server(8080);

    server.start();

    return 0;
}