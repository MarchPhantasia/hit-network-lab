//#include "stdafx.h"
#include <tchar.h>
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>

//��̬����һ��lib���ļ�,ws2_32.lib�ļ����ṩ�˶������������API��֧�֣���ʹ�����е�API����Ӧ�ý�ws2_32.lib���빤��
#pragma comment(lib, "Ws2_32.lib")

#define MAXSIZE 65507 // �������ݱ��ĵ���󳤶�
#define HTTP_PORT 80  // http �������˿�

#define INVALID_WEBSITE "http://http.p2hp.com/"   //��վ����
#define FISH_WEBSITE_FROM "http://http.p2hp.com/"  //������վԴ��ַ
#define FISH_WEBSITE_TO "http://jwes.hit.edu.cn/"  //������վĿ����ַ
#define FISH_WEBSITE_HOST "jwes.hit.edu.cn"        //����Ŀ�ĵ�ַ��������

// Http ��Ҫͷ������
struct HttpHeader
{
    char method[4];         // POST ���� GET��ע����ЩΪ CONNECT����ʵ���ݲ�����
    char url[1024];         // ����� url
    char host[1024];        // Ŀ������
    char cookie[1024 * 10]; // cookie
    HttpHeader()
    {
        ZeroMemory(this, sizeof(HttpHeader));
    }
};

BOOL InitSocket();
void ParseHttpHead(char* buffer, HttpHeader* httpHeader);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
void getfileDate(FILE* in, char* tempDate);//���ļ��ж�ȡ������Ϣ������������Ϣ�洢��tempDate��
void sendnewHTTP(char* buffer, char* datestring);//����һ���µ�HTTP���󣬲�����Ӧ�洢��buffer��
void makeFilename(char* url, char* filename);//����URL�����ļ���
void storefileCache(char* buffer, char* url);//���ļ����ݴ洢��������
void checkfileCache(char* buffer, char* filename);//��黺�����Ƿ���ڸ��ļ���������ڣ����ļ����ݷ���


/*
������ز���:
    SOCKET��������һ��unsigned int��������Ψһ��ID
    sockaddr_in��������������ͨ�ŵĵ�ַ��sockaddr_in����socket����͸�ֵ��sockaddr���ں�������
        short   sin_family;         ��ַ��
        u_short sin_port;           16λTCP/UDP�˿ں�
        struct  in_addr sin_addr;   32λIP��ַ
        char    sin_zero[8];        ��ʹ��
*/
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//������ز���
boolean haveCache = FALSE;
boolean needCache = TRUE;

// �����µ����Ӷ�ʹ�����߳̽��д������̵߳�Ƶ���Ĵ����������ر��˷���Դ
// ����ʹ���̳߳ؼ�����߷�����Ч��
// const int ProxyThreadMaxNum = 20;
// HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
// DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};

struct ProxyParam
{
    SOCKET clientSocket;
    SOCKET serverSocket;
};

int _tmain(int argc, _TCHAR* argv[])
{
    printf("�����������������\n");
    printf("��ʼ��...\n");
    if (!InitSocket())
    {
        printf("socket ��ʼ��ʧ��\n");
        return -1;
    }
    printf("����������������У������˿� %d\n", ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET; //��Ч�׽���-->��ʼ����
    ProxyParam* lpProxyParam;
    HANDLE hThread; //������Ͷ���һһ��Ӧ��32λ�޷�������ֵ
    DWORD dwThreadID; //unsigned long

    // ������������ϼ���
    while (true)
    {
        //accept���ͻ��˵���Ϣ�󶨵�һ��socket�ϣ�Ҳ���Ǹ��ͻ��˴���һ��socket��ͨ������ֵ���ظ����ǿͻ��˵�socket
        acceptSocket = accept(ProxyServer, NULL, NULL);
        lpProxyParam = new ProxyParam;
        if (lpProxyParam == NULL)
        {
            continue;
        }
        lpProxyParam->clientSocket = acceptSocket;

        //_beginthreadex�����̣߳���3��4�������ֱ�Ϊ�߳�ִ�к������̺߳����Ĳ���
        hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);

        /* CloseHandle
            ֻ�ǹر���һ���߳̾�����󣬱�ʾ�Ҳ���ʹ�øþ����
            ��������������Ӧ���߳����κθ�Ԥ�ˣ��ͽ����߳�û��һ���ϵ
        */
        CloseHandle(hThread);

        //�ӳ� from <windows.h>
        Sleep(200);
    }
    closesocket(ProxyServer);
    WSACleanup();
    return 0;
}

//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: ��ʼ���׽���
//************************************
BOOL InitSocket()
{

    // �����׽��ֿ⣨���룩
    WORD wVersionRequested;
    WSADATA wsaData;
    // �׽��ּ���ʱ������ʾ
    int err;
    // �汾 2.2
    wVersionRequested = MAKEWORD(2, 2);
    // ���� dll �ļ� Scoket ��
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0)
    {
        // �Ҳ��� winsock.dll
        printf("���� winsock ʧ�ܣ��������Ϊ: %d\n", WSAGetLastError());
        return FALSE;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        printf("�����ҵ���ȷ�� winsock �汾\n");
        WSACleanup();
        return FALSE;
    }

    /*socket() ������
        int af����ַ��淶����ǰ֧�ֵ�ֵΪAF_INET��AF_INET6������IPv4��IPv6��Internet��ַ���ʽ
        int type�����׽��ֵ����͹淶��SOCK_STREAM 1��һ���׽������ͣ���ͨ��OOB���ݴ�������ṩ
                  ˳��ģ��ɿ��ģ�˫��ģ��������ӵ��ֽ���
        int protocol��Э�顣ֵΪ0��������߲�ϣ��ָ��Э�飬�����ṩ�̽�ѡ��Ҫʹ�õ�Э��
    */
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);

    if (INVALID_SOCKET == ProxyServer)
    {
        printf("�����׽���ʧ�ܣ��������Ϊ�� %d\n", WSAGetLastError());
        return FALSE;
    }

    ProxyServerAddr.sin_family = AF_INET;
    ProxyServerAddr.sin_port = htons(ProxyPort);    //��һ���޷��Ŷ�������ֵת��ΪTCP/IP�����ֽ��򣬼����ģʽ(big-endian)
    //ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;  //INADDR_ANY == 0.0.0.0����ʾ����������IP
    ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");  //���ޱ����û��ɷ���


    //���׽����������ַ
    if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
    {
        printf("���׽���ʧ��\n");
        return FALSE;
    }

    //�����׽���
    if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("�����˿�%d ʧ��", ProxyPort);
        return FALSE;
    }
    return TRUE;
}

//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: �߳�ִ�к���
// Parameter: LPVOID lpParameter
// �̵߳��������ھ����̺߳����ӿ�ʼִ�е��߳̽���
// //************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
    //���建�����
    char filename[100] = { 0 };
    _Post_ _Notnull_ FILE* in;//������һ���ļ�ָ�룬����ָ�������ļ���_Post_ _Notnull_��һ���꣬��ʾ�ں�������֮��inָ����벻Ϊ��
    char* DateBuffer;//ָ�����ڻ����������ڻ��������ڴ洢���������ݰ��н�����������ֵ
    char date_str[30];  //������һ���ַ����飬���ڴ洢�����ַ����������ַ���ͨ�����ꡢ�¡�����ɣ����� "2021-08-01"
    FILE* fp;

    char Buffer[MAXSIZE];
    char* CacheBuffer;
    ZeroMemory(Buffer, MAXSIZE);
    SOCKADDR_IN clientAddr;
    int length = sizeof(SOCKADDR_IN);
    int recvSize;
    int ret;
    recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
    HttpHeader* httpHeader = new HttpHeader();
    CacheBuffer = new char[recvSize + 1];

    //goto��䲻������ʵ�������ֲ��������壩����ʵ�����Ƶ�������ͷ�Ϳ�����
    if (recvSize <= 0)
    {
        goto error;
    }
    
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);  //��srcָ���ַΪ��ʼ��ַ������n���ֽڵ����ݸ��Ƶ���destinָ���ַΪ��ʼ��ַ�Ŀռ���
    //����httpheader
    ParseHttpHead(CacheBuffer, httpHeader); 

    ZeroMemory(date_str, 30);
    printf("httpHeader->url : %s\n", httpHeader->url);
    makeFilename(httpHeader->url, filename);
    //printf("filename�� %s\n", filename);
    if ((fopen_s(&in, filename, "r")) == 0)
    {
        printf("\n�л���\n");

        getfileDate(in, date_str);//�õ����ػ����ļ��е�����date_str
        fclose(in);
        //printf("date_str:%s\n", date_str);
        sendnewHTTP(Buffer, date_str);
        //�����������һ�����󣬸�������Ҫ���� ��If-Modified-Since�� �ֶ�
        //������ͨ���Ա�ʱ�����жϻ����Ƿ����
        haveCache = TRUE;
    }

    delete CacheBuffer; 
    //printf("test\n");

    //�ڷ��ͱ���ǰ��������
    //if (strcmp(httpHeader->url,INVALID_WEBSITE)==0)
    //{
    //    printf("************************************\n");
    //    printf("----------����վ�ѱ�����------------\n");
    //    printf("************************************\n");
    //    goto error;
    //}

    ////��վ����
    if (strstr(httpHeader->url, FISH_WEBSITE_FROM) != NULL) {
        printf("\n=====================================\n\n");
        printf("-------------�Ѵ�Դ��ַ��%s ת�� Ŀ����ַ ��%s ----------------\n", FISH_WEBSITE_FROM, FISH_WEBSITE_TO);
        memcpy(httpHeader->host, FISH_WEBSITE_HOST, strlen(FISH_WEBSITE_HOST) + 1);
        memcpy(httpHeader->url, FISH_WEBSITE_TO, strlen(FISH_WEBSITE_TO));
     }


    if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host))
    {
        //printf("error\n");
        goto error;
    }
    printf("������������ %s �ɹ�\n", httpHeader->host);
    //printf("test");
   
    // ���ͻ��˷��͵� HTTP ���ݱ���ֱ��ת����Ŀ�������
    ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
    
    // �ȴ�Ŀ���������������
    recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
    if (recvSize <= 0)
    {
        goto error;
    }
    if (haveCache == true) {
        checkfileCache(Buffer, httpHeader->url);
    }
    if (needCache == true) {
        storefileCache(Buffer, httpHeader->url);
    }

    // ��Ŀ����������ص�����ֱ��ת�����ͻ���
    ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);


// ������
error:
    printf("�ر��׽���\n");
    Sleep(200);
    closesocket(((ProxyParam*)lpParameter)->clientSocket);
    closesocket(((ProxyParam*)lpParameter)->serverSocket);
    delete lpParameter;
    _endthreadex(0);//��ֹ�߳�
    return 0;
}

//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: void
// Qualifier: ���� TCP �����е� HTTP ͷ��
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader)
{
    char* p;
    char* ptr;
    const char* delim = "\r\n";
    p = strtok_s(buffer, delim, &ptr); // ��ȡ��һ��
    printf("%s\n", p);
    if (p[0] == 'G')
    { // GET ��ʽ
        memcpy(httpHeader->method, "GET", 3);
        memcpy(httpHeader->url, &p[4], strlen(p) - 13);
    }
    else if (p[0] == 'P')
    { // POST ��ʽ
        memcpy(httpHeader->method, "POST", 4);
        memcpy(httpHeader->url, &p[5], strlen(p) - 14);
    }
    printf("���ڷ���url��%s\n", httpHeader->url);
    p = strtok_s(NULL, delim, &ptr);    //��ȡ�ڶ���
    while (p)
    {
        switch (p[0])
        {
        case 'H': // Host
            memcpy(httpHeader->host, &p[6], strlen(p) - 6); //��Host���Ƶ�httpHeader->host��
            break;
        case 'C': // Cookie
            if (strlen(p) > 8)
            {
                char header[8];
                ZeroMemory(header, sizeof(header));
                memcpy(header, p, 6);
                if (!strcmp(header, "Cookie"))
                {
                    memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
                }
            }
            break;
        default:
            break;
        }
        p = strtok_s(NULL, delim, &ptr); //��ȡ��һ��
    }
}

//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: ������������Ŀ��������׽��֣�������
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET* serverSocket, char* host)
{
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(HTTP_PORT);
    HOSTENT* hostent = gethostbyname(host);
    if (!hostent)
    {
        //printf("error_hostent\n");
        return FALSE;
    }
    in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    *serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (*serverSocket == INVALID_SOCKET)
    {
        //printf("error_serverSocket\n");
        return FALSE;
    }
    if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        //printf("error_connect\n");
        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}

//���ʱ����ļ�����ȡ���ػ����е�����
void getfileDate(FILE* in, char* tempDate)
{
    char field[5] = "Date";
    //ptr�����ڴ洢strtok_s�����ķ���ֵ
    char* p, * ptr, temp[5];//p�����ڴ洢���ļ��ж�ȡ���ַ���

    char buffer[MAXSIZE];//�洢���ļ��ж�ȡ���ַ���
    ZeroMemory(buffer, MAXSIZE);
    fread(buffer, sizeof(char), MAXSIZE, in);//���ļ��ж�ȡ�ַ�����������洢��buffer������
    const char* delim = "\r\n";//���з�
    ZeroMemory(temp, 5);
    p = strtok_s(buffer, delim, &ptr);//ʹ��strtok_s������buffer���鰴�зָ��ַ�����������һ�д洢��p��
    //printf("p = %s\n", p);
    int len = strlen(field) + 2;
    while (p) //���pָ��ָ����ַ�������"Date"�ַ�������ʹ��memcpy������������Ϣ���Ƶ�tempDate�����У������ء�
        //���pָ��ָ����ַ���������"Date"�ַ����������������һ���ַ���
    {
        if (strstr(p, field) != NULL) {//����strstr��ָ���ָ��ƥ��ʣ��ĵ�һ���ַ�
            memcpy(tempDate, &p[len], strlen(p) - len);
            return;
        }
        p = strtok_s(NULL, delim, &ptr);
    }
}

//����HTTP������
void sendnewHTTP(char* buffer, char* datestring) {
    const char* field = "Host";
    const char* newfield = "If-Modified-Since: ";//�ֱ����ڱ�ʾ�����Ķ��е�Host�ֶκ�Ҫ��������ֶ�
    //const char *delim = "\r\n";
    char temp[MAXSIZE];//�洢�������ֶκ��������
    ZeroMemory(temp, MAXSIZE);
    char* pos = strstr(buffer, field);//��ȡ�����Ķ���Host��Ĳ�����Ϣ
    int i = 0;
    for (i = 0; i < strlen(pos); i++) {
        temp[i] = pos[i];//��pos���Ƹ�temp
    }
    *pos = '\0';
    //��posָ��ָ��Host�ֶκ�ĵ�һ���ַ���Ȼ��������ֶΣ������ֶ��е�ÿ���ַ����뵽posָ��ָ���λ��
    while (*newfield != '\0') {  //����If-Modified-Since�ֶ�
        *pos++ = *newfield++;
    }
    while (*datestring != '\0') {//��������ļ������±��޸�ʱ��
        *pos++ = *datestring++;
    }
    *pos++ = '\r';//���ϱ��ĸ�ʽ
    *pos++ = '\n';
    for (i = 0; i < strlen(temp); i++)//��ԭʼ�����Ķ��е�ʣ���ַ����Ƶ����ֶ�֮��
    {
        *pos++ = temp[i];
    }
}

//����url�����ļ���
void makeFilename(char* url, char* filename) {
    while (*url != '\0') {
        if ('a' <= *url && *url <= 'z') {
            *filename++ = *url;//�����ǰ�ַ���'a'��'z'�ķ�Χ�ڣ����临�Ƶ��ļ����ַ�����
        }
        url++;
    }
    strcat_s(filename, strlen(filename) + 9, ".txt");
}

//�����������ص�״̬�룬�����200������ݽ��б��ظ��»���
void storefileCache(char* buffer, char* url) {
    char* p, * ptr, tempBuffer[MAXSIZE + 1];

    const char* delim = "\r\n";
    ZeroMemory(tempBuffer, MAXSIZE + 1);
    memcpy(tempBuffer, buffer, strlen(buffer));
    p = strtok_s(tempBuffer, delim, &ptr);//��ȡ��һ��

    if (strstr(tempBuffer, "200") != NULL) {  //״̬����200ʱ����
        char filename[100] = { 0 };
        makeFilename(url, filename);
        printf("filename : %s\n", filename);
        FILE* out;
        fopen_s(&out, filename, "w+");
        fwrite(buffer, sizeof(char), strlen(buffer), out);//ʹ��fopen_s������д��ģʽ���ļ���������Ӧ����д���ļ�
        fclose(out);
        printf("\n===================���»���ok==================\n");
    }
}

//�����������ص�״̬�룬�����304��ӱ��ػ�ȡ�������ת����������Ҫ���»���
void checkfileCache(char* buffer, char* filename)
{
    char* p, * ptr, tempBuffer[MAXSIZE + 1];
    const char* delim = "\r\n";
    ZeroMemory(tempBuffer, MAXSIZE + 1);
    memcpy(tempBuffer, buffer, strlen(buffer));//��buffer���Ƶ�tempBuffer��
    p = strtok_s(tempBuffer, delim, &ptr);//��ȡ״̬��������
    //�������صı����е�״̬��Ϊ304ʱ�����ѻ��������
    if (strstr(p, "304") != NULL) {
        printf("\n=================�ӱ�����û���====================\n");
        ZeroMemory(buffer, strlen(buffer));
        FILE* in = NULL;
        if ((fopen_s(&in, filename, "r")) == 0) {
            fread(buffer, sizeof(char), MAXSIZE, in);//ʹ��fopen_s�����Զ�ȡģʽ���ļ��������ļ����ݴ洢��buffer������
            fclose(in);
        }
        needCache = FALSE;
    }
}
