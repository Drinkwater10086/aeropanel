#pragma once

namespace videoBridge {
static SOCKET listener=INVALID_SOCKET, client=INVALID_SOCKET;
static bool initialized=false;
static ULONGLONG received=0, connected=0;
static std::string request;
static double fps=0, ms=0, average=0, low=0;

static void CloseClient(){
    if(client!=INVALID_SOCKET)closesocket(client);
    client=INVALID_SOCKET;
    request.clear();
}

static void Stop(){
    CloseClient();
    if(listener!=INVALID_SOCKET)closesocket(listener);
    listener=INVALID_SOCKET;
    if(initialized)WSACleanup();
    initialized=false;
    received=0;
}

static void Start(){
    if(initialized)return;
    WSADATA data;
    if(WSAStartup(MAKEWORD(2,2),&data))return;
    initialized=true;
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in address={};address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(47653);
    u_long nonblocking=1;
    BOOL exclusive=TRUE;
    if(listener==INVALID_SOCKET||
       setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive))||
       ioctlsocket(listener,FIONBIO,&nonblocking)||
       bind(listener,(sockaddr*)&address,sizeof(address))||listen(listener,4)){
        Stop();Log(L"Video bridge could not bind localhost port 47653.");
    }
}

static void Poll(){
    if(listener==INVALID_SOCKET)return;
    auto now=GetTickCount64();
    if(client!=INVALID_SOCKET&&now-connected>1500)CloseClient();
    if(client==INVALID_SOCKET){
        client=accept(listener,NULL,NULL);
        if(client==INVALID_SOCKET)return;
        u_long nonblocking=1;
        if(ioctlsocket(client,FIONBIO,&nonblocking)){CloseClient();return;}
        connected=now;
    }
    char buffer[4096];
    int count=recv(client,buffer,sizeof(buffer),0);
    if(count==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK)return;
    if(count<=0){CloseClient();return;}
    request.append(buffer,count);
    if(request.size()>8192){CloseClient();return;}
    auto headerEnd=request.find("\r\n\r\n");
    if(headerEnd==std::string::npos)return;

    std::string headers=request.substr(0,headerEnd+2);
    for(char& c:headers)if(c>='A'&&c<='Z')c+='a'-'A';
    const std::string prefix="\r\norigin: chrome-extension://";
    auto originStart=headers.find(prefix);
    std::string origin;
    if(originStart!=std::string::npos){
        auto idStart=originStart+prefix.size();
        auto idEnd=headers.find("\r\n",idStart);
        if(idEnd!=std::string::npos&&idEnd-idStart==32){
            auto id=headers.substr(idStart,32);
            if(id.find_first_not_of("abcdefghijklmnop")==std::string::npos)origin="chrome-extension://"+id;
        }
    }
    auto reply=[&](bool ok){
        std::string response=ok?"HTTP/1.1 204 No Content\r\n":"HTTP/1.1 400 Bad Request\r\n";
        if(!origin.empty())response+="Access-Control-Allow-Origin: "+origin+"\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: X-AeroPanel-Video, Content-Type\r\n"
            "Access-Control-Allow-Private-Network: true\r\n";
        response+="Content-Length: 0\r\nConnection: close\r\n\r\n";
        send(client,response.data(),(int)response.size(),0);
        CloseClient();
    };
    if(request.rfind("OPTIONS /video HTTP/1.1\r\n",0)==0){reply(!origin.empty());return;}
    if(request.rfind("POST /video HTTP/1.1\r\n",0)!=0){reply(false);return;}
    auto marker=headers.find("\r\nx-aeropanel-video: 1\r\n");
    if(marker==std::string::npos||marker>=headerEnd){CloseClient();return;}
    const auto body=request.find("AEROVIDEO ",headerEnd+4);
    if(body!=headerEnd+4){
        if(body==std::string::npos&&request.size()<headerEnd+14)return;
        CloseClient();return;
    }
    if(request.find('\n',body)==std::string::npos)return;
    double f,m,a,l;int end=0;
    bool accepted=sscanf(request.c_str()+body,"AEROVIDEO %lf %lf %lf %lf%n",&f,&m,&a,&l,&end)==4&&
       request[body+end]=='\n'&&f>0&&f<=1000&&m>=1&&m<=10000&&a>0&&a<=1000&&l>=0&&l<=1000;
    if(accepted){
        if(!received||now-received>5000)Log(L"Browser video frame samples received.");
        fps=f;ms=m;average=a;low=l;received=now;
    }
    reply(accepted);
}
}
