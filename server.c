/*
 * FestivalFlow HTTP server + scheduling engine.
 * Build: gcc server.c -o festivalflow (Linux/macOS)
 *        gcc server.c -o festivalflow.exe -lws2_32 (MinGW Windows)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  typedef SOCKET socket_t;
  #define CLOSESOCK closesocket
  #define SOCKERR() WSAGetLastError()
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int socket_t;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR (-1)
  #define CLOSESOCK close
  #define SOCKERR() 0
#endif

#ifndef PORT
#define PORT 8080
#endif
#define MAX_TASKS 300
#define MAX_DEPTS 15
#define MAX_DEPS 600
#define MAX_BODY 200000
#define MAX_TOKENS 20000
#define TOK_STRING 1
#define TOK_OBJECT 2
#define TOK_ARRAY 3
#define TOK_PRIMITIVE 4

typedef struct {
    int type, start, end, size, parent;
} Token;

static Token toks[MAX_TOKENS];
static int ntok = 0;

/* Small JSON tokenizer: strings, objects, arrays and primitives. */
static void skip_ws(const char *s, int *p) {
    while (s[*p] && isspace((unsigned char)s[*p])) (*p)++;
}
static int parse_string(const char *s, int *p, int parent) {
    int idx = ntok++;
    toks[idx].type = TOK_STRING; toks[idx].start = ++(*p);
    toks[idx].parent = parent; toks[idx].size = 0;
    while (s[*p]) {
        if (s[*p] == '\\' && s[*p+1]) { *p += 2; continue; }
        if (s[*p] == '"') break;
        (*p)++;
    }
    toks[idx].end = *p;
    if (s[*p] == '"') (*p)++;
    return idx;
}
static int parse_value(const char *s, int *p, int parent) {
    skip_ws(s,p);
    if (!s[*p] || ntok >= MAX_TOKENS) return -1;
    int idx = ntok++;
    toks[idx].parent = parent; toks[idx].size = 0;
    if (s[*p] == '{' || s[*p] == '[') {
        char close = s[*p] == '{' ? '}' : ']';
        toks[idx].type = s[*p] == '{' ? TOK_OBJECT : TOK_ARRAY;
        toks[idx].start = (*p)++;
        while (1) {
            skip_ws(s,p);
            if (s[*p] == close) { (*p)++; break; }
            if (!s[*p]) return -1;
            if (s[*p] == ',') { (*p)++; continue; }
            if (s[*p] == ':' ) { (*p)++; continue; }
            if (s[*p] == '"') parse_string(s,p,idx);
            else parse_value(s,p,idx);
            toks[idx].size++;
        }
        toks[idx].end = *p;
    } else if (s[*p] == '"') {
        ntok--;
        return parse_string(s,p,parent);
    } else {
        toks[idx].type = TOK_PRIMITIVE; toks[idx].start = *p;
        while (s[*p] && s[*p] != ',' && s[*p] != ']' && s[*p] != '}' &&
               !isspace((unsigned char)s[*p])) (*p)++;
        toks[idx].end = *p;
    }
    return idx;
}
static int token_next(int i) {
    int j = i + 1;
    while (j < ntok && toks[j].start < toks[i].end) j++;
    return j;
}
static int token_eq(const char *json, int i, const char *v) {
    return i >= 0 && toks[i].type == TOK_STRING &&
           (int)strlen(v) == toks[i].end - toks[i].start &&
           strncmp(json + toks[i].start, v, strlen(v)) == 0;
}
static void token_copy(const char *json, int i, char *out, int cap) {
    int n = (i >= 0) ? toks[i].end - toks[i].start : 0;
    if (n < 0) n = 0;
    if (n >= cap) n = cap - 1;
    memcpy(out, json + (i >= 0 ? toks[i].start : 0), (size_t)n);
    out[n] = '\0';
}
static int object_get(const char *json, int obj, const char *key) {
    if (obj < 0 || toks[obj].type != TOK_OBJECT) return -1;
    int i = obj + 1;
    while (i < ntok && toks[i].start < toks[obj].end) {
        int k = i;
        int v = k + 1;
        if (token_eq(json,k,key)) return v;
        i = token_next(v);
    }
    return -1;
}

typedef struct { char id[64]; char dept[64]; } Task;
typedef struct { char from[64]; char to[64]; } Dep;
static Task tasks[MAX_TASKS];
static Dep deps[MAX_DEPS];
static char departments[MAX_DEPTS][64];
static int task_count, dept_count, dep_count;
static int task_dept[MAX_TASKS];
static int task_edges[MAX_TASKS][MAX_TASKS];
static int dept_edges[MAX_DEPTS][MAX_DEPTS];
static int result_order[MAX_TASKS];
static int result_count;
static char error_message[256];

static int find_task(const char *id) {
    for (int i=0;i<task_count;i++) if (!strcmp(tasks[i].id,id)) return i;
    return -1;
}
static int find_dept(const char *name) {
    for (int i=0;i<dept_count;i++) if (!strcmp(departments[i],name)) return i;
    return -1;
}
static int add_task(const char *id, const char *dept) {
    if (task_count >= MAX_TASKS) return 0;
    if (find_task(id) >= 0) { snprintf(error_message,sizeof(error_message),"Duplicate task ID: %s",id); return 0; }
    int d = find_dept(dept);
    if (d < 0) {
        if (dept_count >= MAX_DEPTS) { snprintf(error_message,sizeof(error_message),"Maximum of 15 departments exceeded."); return 0; }
        d = dept_count++; strncpy(departments[d],dept,63); departments[d][63]=0;
    }
    strncpy(tasks[task_count].id,id,63); tasks[task_count].id[63]=0;
    strncpy(tasks[task_count].dept,dept,63); tasks[task_count].dept[63]=0;
    task_dept[task_count]=d; task_count++;
    return 1;
}
static int add_dep(const char *from, const char *to) {
    if (dep_count >= MAX_DEPS) { snprintf(error_message,sizeof(error_message),"Maximum of 600 dependencies exceeded."); return 0; }
    int a=find_task(from), b=find_task(to);
    if (a<0 || b<0) { snprintf(error_message,sizeof(error_message),"A dependency references an unknown task."); return 0; }
    if (a==b) { snprintf(error_message,sizeof(error_message),"Self-dependency makes the plan IMPOSSIBLE."); return -1; }
    if (!task_edges[a][b]) {
        task_edges[a][b]=1;
        if (task_dept[a] != task_dept[b]) dept_edges[task_dept[a]][task_dept[b]]=1;
        dep_count++;
    }
    return 1;
}
static int topo_departments(int order[MAX_DEPTS]) {
    int indeg[MAX_DEPTS]={0}, used[MAX_DEPTS]={0};
    for(int i=0;i<dept_count;i++) for(int j=0;j<dept_count;j++) if(dept_edges[i][j]) indeg[j]++;
    for(int k=0;k<dept_count;k++) {
        int pick=-1;
        for(int i=0;i<dept_count;i++) if(!used[i] && indeg[i]==0){pick=i;break;}
        if(pick<0) return 0;
        used[pick]=1; order[k]=pick;
        for(int j=0;j<dept_count;j++) if(dept_edges[pick][j]) indeg[j]--;
    }
    return 1;
}
static int topo_tasks_for_dept(int d, int order[MAX_TASKS], int *n) {
    int indeg[MAX_TASKS]={0}, used[MAX_TASKS]={0}, count=0;
    for(int i=0;i<task_count;i++) if(task_dept[i]==d) {
        count++;
        for(int j=0;j<task_count;j++) if(task_dept[j]==d && task_edges[j][i]) indeg[i]++;
    }
    for(int k=0;k<count;k++) {
        int pick=-1;
        for(int i=0;i<task_count;i++) if(task_dept[i]==d && !used[i] && indeg[i]==0){pick=i;break;}
        if(pick<0) return 0;
        used[pick]=1; order[(*n)++]=pick;
        for(int j=0;j<task_count;j++) if(task_dept[j]==d && task_edges[pick][j]) indeg[j]--;
    }
    return 1;
}
static int solve(void) {
    int dept_order[MAX_DEPTS], local[MAX_TASKS];
    result_count=0;
    if (!topo_departments(dept_order)) return 0;
    for(int k=0;k<dept_count;k++) {
        int n=0;
        if(!topo_tasks_for_dept(dept_order[k],local,&n)) return 0;
        for(int j=0;j<n;j++) result_order[result_count++]=local[j];
    }
    return result_count==task_count;
}
static void reset_data(void) {
    task_count=dept_count=dep_count=result_count=0;
    memset(task_edges,0,sizeof(task_edges));
    memset(dept_edges,0,sizeof(dept_edges));
    memset(task_dept,0,sizeof(task_dept));
    error_message[0]=0;
}
static int parse_request(const char *body) {
    ntok=0; int p=0;
    if(parse_value(body,&p,-1)<0) { strcpy(error_message,"Invalid JSON."); return 0; }
    int root=0, ta=object_get(body,root,"tasks"), da=object_get(body,root,"dependencies");
    if(ta<0 || toks[ta].type!=TOK_ARRAY) { strcpy(error_message,"The tasks array is required."); return 0; }
    int i=ta+1;
    while(i<ntok && toks[i].start<toks[ta].end) {
        if(toks[i].type!=TOK_OBJECT) { strcpy(error_message,"Each task must be an object."); return 0; }
        int idt=object_get(body,i,"id"), dpt=object_get(body,i,"department");
        char id[64], dept[64];
        if(idt<0 || dpt<0) { strcpy(error_message,"Every task needs id and department."); return 0; }
        token_copy(body,idt,id,sizeof(id)); token_copy(body,dpt,dept,sizeof(dept));
        if(!id[0] || !dept[0]) { strcpy(error_message,"Task IDs and departments cannot be empty."); return 0; }
        if(!add_task(id,dept)) return 0;
        i=token_next(i);
    }
    if(task_count==0) { strcpy(error_message,"Add at least one task."); return 0; }
    if(da>=0 && toks[da].type==TOK_ARRAY) {
        i=da+1;
        while(i<ntok && toks[i].start<toks[da].end) {
            int ft=object_get(body,i,"from"), tt=object_get(body,i,"to");
            char from[64],to[64];
            if(ft<0 || tt<0) { strcpy(error_message,"Each dependency needs from and to."); return 0; }
            token_copy(body,ft,from,sizeof(from)); token_copy(body,tt,to,sizeof(to));
            int r=add_dep(from,to);
            if(r==0) return 0;
            if(r<0) return -1;
            i=token_next(i);
        }
    }
    return 1;
}
static void send_all(socket_t s, const char *data) {
    size_t len=strlen(data), sent=0;
    while(sent<len) {
        int n=send(s,data+sent,(int)(len-sent),0);
        if(n<=0) break;
        sent+=(size_t)n;
    }
}
static void send_response(socket_t s, int code, const char *status, const char *body, const char *type) {
    char header[512];
    snprintf(header,sizeof(header),
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
      code,status,type,(unsigned long)strlen(body));
    send_all(s,header); send_all(s,body);
}
static void json_escape(const char *src, char *dst, size_t cap) {
    size_t j=0;
    for(size_t i=0;src[i] && j+3<cap;i++) {
        unsigned char c=(unsigned char)src[i];
        if(c=='"' || c=='\\'){dst[j++]='\\';dst[j++]=(char)c;}
        else if(c<32) continue;
        else dst[j++]=(char)c;
    }
    dst[j]=0;
}
static void handle_api(socket_t client, const char *body) {
    reset_data();
    int parsed=parse_request(body);
    char out[50000]; size_t pos=0;
    if(parsed<=0) {
        if(parsed<0) {
            snprintf(out,sizeof(out),"{\"status\":\"IMPOSSIBLE\",\"order\":[],\"message\":\"Self-dependency makes the plan impossible.\"}");
            send_response(client,200,"OK",out,"application/json"); return;
        }
        char msg[512]; json_escape(error_message,msg,sizeof(msg));
        snprintf(out,sizeof(out),"{\"status\":\"ERROR\",\"order\":[],\"message\":\"%s\"}",msg);
        send_response(client,400,"Bad Request",out,"application/json"); return;
    }
    if(!solve()) {
        snprintf(out,sizeof(out),"{\"status\":\"IMPOSSIBLE\",\"order\":[],\"message\":\"No schedule satisfies both dependency and department grouping rules.\"}");
        send_response(client,200,"OK",out,"application/json"); return;
    }
    pos+=(size_t)snprintf(out+pos,sizeof(out)-pos,"{\"status\":\"VALID\",\"order\":[");
    for(int i=0;i<result_count;i++) {
        char id[130],dept[130];
        json_escape(tasks[result_order[i]].id,id,sizeof(id));
        json_escape(tasks[result_order[i]].dept,dept,sizeof(dept));
        pos+=(size_t)snprintf(out+pos,sizeof(out)-pos,"%s{\"task\":\"%s\",\"department\":\"%s\"}",i?",":"",id,dept);
    }
    snprintf(out+pos,sizeof(out)-pos,"],\"message\":\"A valid grouped schedule was found.\"}");
    send_response(client,200,"OK",out,"application/json");
}
static void serve_file(socket_t client, const char *path) {
    char full[512];
    if(!strcmp(path,"/")) strcpy(full,"public/index.html");
    else if(strstr(path,"..")) { send_response(client,403,"Forbidden","Forbidden","text/plain"); return; }
    else snprintf(full,sizeof(full),"public%s",path);
    FILE *f=fopen(full,"rb");
    if(!f) { send_response(client,404,"Not Found","File not found","text/plain"); return; }
    const char *type=strstr(full,".css")?"text/css":strstr(full,".js")?"application/javascript":"text/html";
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f);
    char *data=(char*)malloc((size_t)len+1);
    if(!data){fclose(f);return;}
    fread(data,1,(size_t)len,f); data[len]=0; fclose(f);
    char header[512];
    snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",type,len);
    send_all(client,header);
    /* send binary-safe file contents */
    long sent=0; while(sent<len){int n=send(client,data+sent,(int)(len-sent),0);if(n<=0)break;sent+=n;}
    free(data);
}
static void handle_client(socket_t client) {
    char *req=(char*)malloc(MAX_BODY+8192);
    if(!req){CLOSESOCK(client);return;}
    int total=0,n;
    while(total<MAX_BODY+8191) {
        n=recv(client,req+total,MAX_BODY+8191-total,0);
        if(n<=0) break;
        total+=n; req[total]=0;
        char *hdr_end=strstr(req,"\r\n\r\n");
        if(hdr_end) {
            int header_len=(int)(hdr_end+4-req);
            int content_len=0;
            char *cl=strstr(req,"Content-Length:");
            if(cl && cl<hdr_end) content_len=atoi(cl+15);
            if(total>=header_len+content_len) break;
        }
    }
    req[total]=0;
    char method[16]="",path[256]="/";
    sscanf(req,"%15s %255s",method,path);
    char *hdr_end=strstr(req,"\r\n\r\n");
    if(!strcmp(method,"POST") && !strcmp(path,"/api/solve") && hdr_end) {
        handle_api(client,hdr_end+4);
    } else if(!strcmp(method,"GET")) {
        serve_file(client,path);
    } else if(!strcmp(method,"OPTIONS")) {
        send_response(client,204,"No Content","","text/plain");
    } else {
        send_response(client,404,"Not Found","Not found","text/plain");
    }
    free(req); CLOSESOCK(client);
}
int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){printf("Winsock startup failed.\n");return 1;}
#endif
    socket_t server=socket(AF_INET,SOCK_STREAM,0);
    if(server==INVALID_SOCKET){printf("Could not create socket.\n");return 1;}
    int opt=1;
#ifdef _WIN32
    setsockopt(server,SOL_SOCKET,SO_REUSEADDR,(const char*)&opt,sizeof(opt));
#else
    setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
#endif
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY); addr.sin_port=htons(PORT);
    if(bind(server,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR ||
       listen(server,10)==SOCKET_ERROR) {
        printf("Could not bind to port %d. Is it already in use?\n",PORT); CLOSESOCK(server); return 1;
    }
    printf("\nFestivalFlow is running at http://localhost:%d\n",PORT);
    printf("Keep this window open. Press Ctrl+C to stop.\n\n");
    while(1) {
        socket_t client=accept(server,NULL,NULL);
        if(client!=INVALID_SOCKET) handle_client(client);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
