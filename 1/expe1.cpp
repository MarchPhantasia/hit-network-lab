#define _CRT_SECURE_NO_WARNINGS 1
#pragma comment(lib, "ws2_32.lib") // ����ws2_32.lib�⣬�ƺ����Ӳ��ˣ�����ͨ�������в������

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00 // ָ��Ŀ��汾Ϊwin11
#include <stdio.h>
#include <process.h>
#include <string.h>
#include <tchar.h>
#include <fstream>
#include <string>
#include <ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
// #include <Windows.h>
#include <iostream>
#include <chrono>
#include <sstream>
using namespace std;

#define MAXSIZE 165507 // �������ݱ��ĵ���󳤶�
#define HTTP_PORT 80   // http �������˿�
#define INET_ADDRSTRLEN 22
#define INET6_ADDRSTRLEN 65
// #define BANNED_WEB "lib.hit.edu.cn"                 // ������վ
#define PHISHING_WEB_SRC "jwc.hit.edu.cn"           // ����ԭ��ַ
#define PHISHING_WEB_DEST "http://jwts.hit.edu.cn/" // ����Ŀ����ַ
#define BANNED_NUM 3
const char *banedIP[BANNED_NUM] = {"192.168.0.1", "192.168.0.2", "192.168.0.3"};             // �����û�ip�б�
const char *banedWEB[BANNED_NUM] = {"lib.hit.edu.cn", "lib1.hit.edu.cn", "lib2.hit.edu.cn"}; // ������վ�б�
// Http����ͷ��ʽ
struct HttpHeader
{
    char method[4];         // POST �� GET
    char url[1024];         // ����� url
    char host[1024];        // Ŀ������
    char cookie[1024 * 10]; // cookie
    HttpHeader()
    {
        ZeroMemory(this, sizeof(HttpHeader));
    }
};

// ����ı��ĸ�ʽ
struct HttpCache
{
    char url[1024];
    char host[1024];
    char last_modified[200];
    char status[4];
    char buffer[MAXSIZE];
    HttpCache()
    {
        ZeroMemory(this, sizeof(HttpCache)); // ��ʼ��cache
    }
};
HttpCache Cache[1024];
int cached_number = 0; // �Ѿ������url��
int last_cache = 0;    // ��һ�λ��������

// ����������洢�Ŀͻ�����Ŀ��������׽���
struct ProxyParam
{
    SOCKET clientSocket;
    SOCKET serverSocket;
};

BOOL InitWsa();
BOOL InitSocket();
int getIpByUrl(char *url, char *ipstr);
void ParseCachedModified(char *buffer, char *status, char *last_modified);
int ParseHttpHead(char *buffer, HttpHeader *httpHeader, int *store);
BOOL ConnectToServer(SOCKET *serverSocket, char *url);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

// ������ز���
SOCKET ProxyServer;          // ����������׽���
sockaddr_in ProxyServerAddr; // �����������ַ
const int ProxyPort = 10240; // ����������˿�

// �����µ����Ӷ�ʹ�����߳̽��д������̵߳�Ƶ���Ĵ����������ر��˷���Դ
// ����ʹ���̳߳ؼ�����߷�����Ч��
// const int ProxyThreadMaxNum = 20;
// HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
// DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
bool isBanned(char *ip)
{
    for (int i = 0; i < BANNED_NUM; i++)
    {
        if (strcmp(ip, banedIP[i]) == 0)
        {
            printf("IP %s �ѱ�����\n", ip);
            return true;
        }
    }
    return false;
}
//************************************
//  Method: InitWsa
//  FullName: InitSWsa
//  Access: public
//  Returns: BOOL
//  Qualifier: ���ز�ȷ��Wsa
//************************************
BOOL InitWsa()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;
    wVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0)
    {
        printf("���� winsock ʧ�ܣ� �������Ϊ: %d\n", WSAGetLastError());
        return FALSE;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        printf("�����ҵ���ȷ�� winsock �汾\n");
        WSACleanup();
        return FALSE;
    }
    return TRUE;
}

//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: ��ʼ������������׽��֣���ʼ����.
//************************************
BOOL InitSocket()
{
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0); // IPv4, TCP
    if (INVALID_SOCKET == ProxyServer)
    {
        printf("�����׽���ʧ�ܣ��������Ϊ��%d\n", WSAGetLastError());
        return FALSE;
    }
    ProxyServerAddr.sin_family = AF_INET; // IPv4
    ProxyServerAddr.sin_port = htons(ProxyPort);
    // ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    // �󶨶˵��ַ���׽���
    if (bind(ProxyServer, (SOCKADDR *)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
    {
        printf("���׽���ʧ��\n");
        return FALSE;
    }
    if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("�����˿�%d ʧ��", ProxyPort);
        return FALSE;
    }
    return TRUE;
}

//*************************
// Method: ParseCache
// FullName: ParseCache
// Access: public
// Returns: void
// Qualifier: ������Ӧ�� HTTP ͷ��,��ȡ��״̬��� Last-Modified�������� cache���е�������ж��Ƿ���Ҫ���»���
// Parameter: char *buffer
// Parameter: char * status
// Parameter: HttpHeader *httpHeader
//*************************
void ParseCachedModified(char *buffer, char *status, char *last_modified)
{
    char *p;
    char *ptr;
    int flag = 0;
    const char *delim = "\r\n";
    p = strtok_s(buffer, delim, &ptr);
    memcpy(status, &p[9], 3);
    status[3] = '\0';
    p = strtok_s(NULL, delim, &ptr);
    while (p)
    {
        if (strstr(p, "Last-Modified") != NULL)
        {
            flag = 1;
            memcpy(last_modified, &p[15], strlen(p) - 15);
            break;
        }
        p = strtok_s(NULL, delim, &ptr);
    }
    if (flag == 0)
        printf("������û��û�з���Last-Modified�ֶ���Ϣ\n");
}

//*************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: int
// Qualifier: ���� TCP �����е� HTTP ͷ�����ж��Ƿ����л��棬�ڻ����д洢����� URL
// Parameter: char *buffer
// Parameter: HttpHeader *httpHeader
//*************************
int ParseHttpHead(char *buffer, HttpHeader *httpHeader, int *store)
{
    int flag = 0; // �����ж��Ƿ����� cache��1-���У�0-δ����
    char *p;
    char *ptr;
    const char *delim = "\r\n";
    // ����������
    p = strtok_s(buffer, delim, &ptr);
    // �� url ���� cache
    if (p[0] == 'G') // GET��ʽ
    {
        printf("GET����\n");
        memcpy(httpHeader->method, "GET", 3);
        // ��ȡ��·��
        int index = 0;
        while (p[index] != '/')
        {
            index++;
        }
        index += 2;
        int start = index;
        for (; index < strlen(p) && p[index] != '/'; index++)
        {
            httpHeader->url[index - start] = p[index];
        }
        httpHeader->url[index - start] = '\0';
        // printf("�����վ��ժ������URLURLURLURLUUUUURRRRRLLLLLLL: %s\n", httpHeader->url);
        for (int i = 0; i < 1024; i++)
        {
            if (Cache[i].url[0] != '\0')
            {
                printf("Cache[%d].url: %s\n", i, Cache[i].url);
            }
            if (strcmp(Cache[i].url, httpHeader->url) == 0) // �Ѵ��ڣ�����flag�˳�ѭ��
            {
                flag = 1;
                break;
            }
        }
        if (!flag && cached_number != 1023) // ������ && ��ǰ��cacheδ����ֱ�Ӵ洢
        {
            memcpy(Cache[cached_number].url, httpHeader->url, strlen(httpHeader->url));
            last_cache = cached_number; // ��¼���һ�λ��������
        }
        else if (!flag && cached_number == 1023) // ������ && ��ǰ��cache���������ǵ�һ��
        {
            memcpy(Cache[0].url, httpHeader->url, strlen(httpHeader->url));
            last_cache = 0;
        }
        store[0] = last_cache;
    }
    else if (p[0] == 'P') // POST��ʽ
    {
        printf("POST����\n");
        memcpy(httpHeader->method, "POST", 4);
        memcpy(httpHeader->url, &p[5], strlen(p) - 14);
        for (int i = 0; i < 1024; i++)
        {
            if (strcmp(Cache[i].url, httpHeader->url) == 0)
            {
                flag = 1;
                break;
            }
        }
        if (!flag && cached_number != 1023)
        {
            memcpy(Cache[cached_number].url, httpHeader->url, strlen(httpHeader->url));
            last_cache = cached_number;
        }
        else if (!flag && cached_number == 1023)
        {
            memcpy(Cache[0].url, httpHeader->url, strlen(httpHeader->url));
            last_cache = 0;
        }
    }
    else if (p[0] == 'C') // CONNECT��ʽ
    {
        printf("CONNECT����\n");
        memcpy(httpHeader->method, "CONNECT", 7);
        memcpy(httpHeader->url, &p[8], strlen(p) - 21);
        // printf("url��%s\n", httpHeader->url);
        for (int i = 0; i < 1024; i++)
        {
            if (strcmp(Cache[i].url, httpHeader->url) == 0)
            {
                flag = 1;
                break;
            }
        }
        if (!flag && cached_number != 1023)
        {
            memcpy(Cache[cached_number].url, httpHeader->url, strlen(httpHeader->url));
            last_cache = cached_number;
        }
        else if (!flag && cached_number == 1023)

        {
            memcpy(Cache[0].url, httpHeader->url, strlen(httpHeader->url));
            last_cache = 0;
        }
    }
    // �������� Host �� Cookie
    p = strtok_s(NULL, delim, &ptr);
    while (p)
    {
        switch (p[0])
        {
        case 'H': // HOST������ cache
            memcpy(httpHeader->host, &p[6], strlen(p) - 6);
            if (!flag && cached_number != 1023)
            {
                memcpy(Cache[last_cache].host, &p[6], strlen(p) - 6);
                cached_number++;
            }
            else if (!flag && cached_number == 1023)
            {
                memcpy(Cache[last_cache].host, &p[6], strlen(p) - 6);
            }
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
        p = strtok_s(NULL, delim, &ptr);
    }
    return flag;
}
//*************************
// Method: getIpByUrl
// FullName: getIpByUrl
// Access: public
// Returns: int
// Qualifier: ͨ�� url ��ȡ ip ��ַ
// Parameter: char *url
// Parameter: char *ipstr
//*************************
int getIpByUrl(char *url, char *ipstr)
{
    int iResult;
    // ��ʼ���ṹ��
    struct addrinfo *result = NULL;
    struct addrinfo *ptr = NULL;
    struct addrinfo hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // getaddrinfo
    iResult = getaddrinfo(url, NULL, &hints, &result);
    if (iResult != 0)
    {
        printf("getaddrinfo failed: %d\n", iResult);
        return 1;
    }
    // ������ַ
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        if (ptr->ai_family == AF_INET)
        { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)ptr->ai_addr;
            snprintf(ipstr, INET_ADDRSTRLEN, "%s", inet_ntoa(ipv4->sin_addr));
            printf("IPV4 Address: %s\n", ipstr);
        }
        else if (ptr->ai_family == AF_INET6)
        { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ptr->ai_addr;
            inet_ntop(AF_INET6, &ipv6->sin6_addr, ipstr, INET6_ADDRSTRLEN);
            printf("IPV6 Address: %s\n", ipstr);
        }
        else
        {
            continue;
        }
    }
    freeaddrinfo(result);
    return 0;
}
//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: ����Ŀ�������
// Parameter: SOCKET * serverSocket
// Parameter: char * url
//************************************
BOOL ConnectToServer(SOCKET *serverSocket, char *url)
{
    sockaddr_in serverAddr; // �������˿ڵ�ַ
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(HTTP_PORT);
    char ipstr[INET6_ADDRSTRLEN];
    int iResult = getIpByUrl(url, ipstr);
    if (iResult)
    {
        printf("����Ŀ�������ipʧ��\n");
        return FALSE;
    }
    serverAddr.sin_addr.s_addr = inet_addr(ipstr);
    // Ŀ���������ip��ַ
    printf("Ŀ�������ip��%s\n", ipstr);
    *serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (*serverSocket == INVALID_SOCKET)
    {
        printf("��������Ŀ�������socketʧ��\n");
        return FALSE;
    }
    if (connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        printf("����Ŀ�������ʧ��\n");
        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}
//************************************
// Method: createAndWriteToFile
// FullName: createAndWriteToFile
// Access: public
// Returns: void
// Qualifier: �����ļ���д���ַ���
// Parameter: const char * inputString
// Parameter: char * url
//************************************
void createAndWriteToFile(const char *inputString, char *url)
{
    char fileName[30] = "cache-";
    strncat(fileName, url, 23); // ��url�������ļ�
    char filePath[256];
    snprintf(filePath, sizeof(filePath), ".\\cpp-code\\computer-net\\expe1\\buffers\\%s", fileName);
    FILE *file = fopen(filePath, "w");
    if (file != NULL)
    {
        fputs(inputString, file); // ���ַ���д���ļ�
        fclose(file);             // �ر��ļ�
        printf("\n�ļ�%s�����ɹ�\n", fileName);
    }
    else
    {
        perror("�ļ���ʧ���ļ���ʧ���ļ���ʧ���ļ���ʧ��");
    }
}
//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: ���߳�ִ�к���������ͻ�������ת����Ŀ�������������Ŀ�����������Ӧ��ת�����ͻ���
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
    printf("����������߳�����\n");
    char Buffer[MAXSIZE];      // �յ��ֽ��������ַ����Ļ���
    char phishBuffer[MAXSIZE]; // ����Ĺ��챨�Ļ���
    char *CacheBuffer;         // �ֽ������ַ���ת���Ļ���

    SOCKADDR_IN clientAddr; // �ͻ��˶˵���Ϣ
    int length = sizeof(SOCKADDR_IN);
    int recvSize;   // ������������յ����ֽ���
    int ret;        // �����ֽ���
    int Have_cache; // ����������Ƿ��л���
    HttpHeader *httpHeader = new HttpHeader();

    // ��ȡ�ͻ��˵�IP��ַ
    getpeername(((ProxyParam *)lpParameter)->clientSocket, (SOCKADDR *)&clientAddr, &length);
    char *clientIP = inet_ntoa(clientAddr.sin_addr);
    printf("�ͻ��� IP: %s\n", clientIP);

    // ����Ƿ����θ�IP
    if (isBanned(clientIP))
    {
        printf("IP %s �����Σ��Ͽ�����\n", clientIP);
        goto error; // ֱ�ӶϿ�����
    }
    // ��ղ�������
    ZeroMemory(Buffer, MAXSIZE);
    ZeroMemory(phishBuffer, MAXSIZE);
    recvSize = recv(((ProxyParam *)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);

    if (recvSize <= 0)
    {
        printf("û���յ��ͻ��˱���\n");
        goto error;
    }
    printf("����������ӿͻ��˽��յ��ı����ǣ�\n");
    printf("%s", Buffer);
    // memcpy(sendBuffer, Buffer, recvSize);

    // ���ֽ���ת��Ϊ�ַ���
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1); // ȷ������� \0
    memcpy(CacheBuffer, Buffer, recvSize);
    int store[1];
    // ����HTTPͷ��������cache��step1
    Have_cache = ParseHttpHead(CacheBuffer, httpHeader, store);
    // printf("last_cache last_cache last_cache last_cache last_cache:%d\n", last_cache);
    delete CacheBuffer;

    // Ϊ��������������׽��֣�����Ŀ���������
    if (!ConnectToServer(&((ProxyParam *)lpParameter)->serverSocket, httpHeader->url))
    {
        printf("�������������Ŀ������� %s ʧ��\n", httpHeader->host);
        goto error;
    }
    printf("�������������Ŀ������� %s �ɹ�\n", httpHeader->host);

    for (int i = 0; i < BANNED_NUM; i++)
    {
        if (strcmp(httpHeader->host, banedWEB[i]) == 0)
        {
            // printf("��վ %s �ѱ������ѱ������ѱ������ѱ������ѱ������ѱ�����\n", banedWEB[i]);
            printf("��վ %s �ѱ�����\n", banedWEB[i]);
            goto error;
        }
    }

    // ��վ����
    if (strstr(httpHeader->url, PHISHING_WEB_SRC) != NULL)
    {
        char *pr;
        printf("��վ %s �ѱ��ɹ��ض����� %s\n", PHISHING_WEB_SRC, PHISHING_WEB_DEST);
        // ���챨�ģ����� Location
        int phishing_len = snprintf(phishBuffer, sizeof(phishBuffer),
                                    "HTTP/1.1 302 Moved Temporarily\r\n"
                                    "Connection:keep-alive\r\n"
                                    "Cache-Control:max-age=0\r\n"
                                    "Location: %s\r\n\r\n",
                                    PHISHING_WEB_DEST);
        // ��ƴ�Ӻõ� 302 ���ķ��͸��ͻ���
        ret = send(((ProxyParam *)lpParameter)->clientSocket, phishBuffer, phishing_len, 0);
        printf("�ɹ����͵��㱨��\n");
        goto error;
    }

    // ʵ��cache���ܣ�step
    if (Have_cache) // �����ҳ���ڴ���������л���
    {
        printf("�ǵ�һ�η��ʸ�ҳ�棬���ڻ��棬������֤�Ƿ���Ҫ�滻����\n");
        // printf("�����ҳ���ڴ���������л��������ҳ���ڴ���������л��������ҳ���ڴ���������л��������ҳ���ڴ���������л���\n");
        // printf("�����URL��%s\n", Cache[last_cache].url);
        char cached_buffer[MAXSIZE]; // ������͵� HTTP ���ݱ���
        ZeroMemory(cached_buffer, MAXSIZE);
        memcpy(cached_buffer, Buffer, recvSize);

        char ifModifiedSinceHeader[MAXSIZE];
        // if (Cache[last_cache].last_modified[0] == '\0') // û���ϴ��޸�ʱ��
        // {
        //     char lastModified[] = "Thu, 01 Jan 1970 00:00:00 GMT";
        //     snprintf(ifModifiedSinceHeader, sizeof(ifModifiedSinceHeader), "If-Modified-Since: %s\r\n", lastModified);
        // }
        // else
        // {
        //     snprintf(ifModifiedSinceHeader, sizeof(ifModifiedSinceHeader), "If-Modified-Since: %s\r\n", Cache[last_cache].last_modified);
        // }
        snprintf(ifModifiedSinceHeader, sizeof(ifModifiedSinceHeader), "If-Modified-Since: %s\r\n", Cache[last_cache].last_modified);

        char *headerEnd = strstr(cached_buffer, "\r\n\r\n"); // �ҵ�ͷ��������λ��
        if (headerEnd != nullptr)
        {
            int headerLength = headerEnd - cached_buffer + 2; // \r\n ��λ��
            char newBuffer[MAXSIZE];
            memset(newBuffer, 0, MAXSIZE);
            strncpy(newBuffer, cached_buffer, headerLength);
            strncat(newBuffer, ifModifiedSinceHeader, strlen(ifModifiedSinceHeader));
            strcat(newBuffer, headerEnd + 2); // �� \r\n\r\n ������ݿ�ʼ����
            recvSize = strlen(newBuffer);
            memset(cached_buffer, 0, MAXSIZE);
            strncpy(cached_buffer, newBuffer, recvSize);
        }

        printf("���͵��޸ĺ����ϢΪ\n%s\n", cached_buffer);
        // ���ʹ����ı��ĸ�Ŀ�������
        ret = send(((ProxyParam *)lpParameter)->serverSocket, cached_buffer, strlen(cached_buffer) + 1, 0);
        recvSize = recv(((ProxyParam *)lpParameter)->serverSocket, cached_buffer, MAXSIZE, 0);
        if (recvSize <= 0)
        {
            printf("û���յ�Ŀ������������If-Modified-Since����������Ļظ�\n");
            goto error;
        }
        char *headerPartPtr = strstr(cached_buffer, "\r\n\r\n");
        int length = headerPartPtr - cached_buffer + 4;
        char *headerPart = new char[length + 1];
        strncpy(headerPart, cached_buffer, length);
        headerPart[length] = '\0';
        printf("���ر���ͷΪ\n%s\n", headerPart);
        delete[] headerPart;
        CacheBuffer = new char[recvSize + 1];
        ZeroMemory(CacheBuffer, recvSize + 1);
        memcpy(CacheBuffer, cached_buffer, recvSize);
        char last_status[4];    // ��¼�������ص�״̬��
        char last_modified[30]; // ��¼����ҳ����޸�ʱ��
        ParseCachedModified(CacheBuffer, last_status, last_modified);
        printf("Ŀ��������ϴ��޸�ʱ��%s\n", last_modified);
        delete CacheBuffer;

        if (strcmp(last_status, "304") == 0) // 304״̬�룬�ļ�û�б��޸�
        {
            // printf("last_cache%s\n", Cache[last_cache].url);
            // printf("store%s\n", Cache[store[0]].url);
            printf("״̬��304��ҳ��δ���޸�\n");
            char url_for304[1024] = "304";
            strncat(url_for304, Cache[last_cache].url, 30);
            createAndWriteToFile(cached_buffer, url_for304);
            // ֱ�ӽ���������ת�����ͻ���
            ret = send(((ProxyParam *)lpParameter)->clientSocket, Cache[last_cache].buffer, sizeof(Cache[last_cache].buffer), 0);

            if (ret != SOCKET_ERROR)
                printf("�ɻ��淢��\n");
        }
        else if (strcmp(last_status, "200") == 0) // 200״̬�룬��ʾ�ļ��ѱ��޸�
        {
            // �޸Ļ�������
            printf("last_cache%s\n", Cache[last_cache].url);
            printf("store%s\n", Cache[store[0]].url);
            printf("״̬��200��ҳ�汻�޸�");
            memcpy(Cache[last_cache].buffer, cached_buffer, strlen(cached_buffer));        // ���»�buffer
            memcpy(Cache[last_cache].last_modified, last_modified, strlen(last_modified)); // �����޸�ʱ��
            createAndWriteToFile(cached_buffer, Cache[last_cache].url);
            // �����ͻ���
            ret = send(((ProxyParam *)lpParameter)->clientSocket, Cache[last_cache].buffer, sizeof(Cache[last_cache].buffer), 0);
            if (ret != SOCKET_ERROR)
                printf("�ɻ��淢�ͣ����޸�\n");
        }
    }
    else // û�л������ҳ��
    {
        // ���ͻ��˷��͵� HTTP ���ݱ���ֱ��ת����Ŀ�������
        // printf("û�л������ҳ��û�л������ҳ��û�л������ҳ��û�л������ҳ��û�л������ҳ��\n");
        printf("δ���У��ǵ�һ�η��ʸ�ҳ�棬�޻���\n");
        ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
        recvSize = recv(((ProxyParam *)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
        if (recvSize <= 0)
        {
            printf("û���յ�ת���ͻ��������Ŀ��������Ļظ�\n");
            goto error;
        }
        // ��Ŀ����������ص�����ֱ��ת�����ͻ���
        ret = send(((ProxyParam *)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
        memcpy(Cache[last_cache].buffer, Buffer, strlen(Buffer));
        createAndWriteToFile(Buffer, Cache[last_cache].url);
        if (ret != SOCKET_ERROR)
        {
            printf("���Է�����\n���������ת������ͷΪ\n");
            char *headerPartPtr = strstr(Buffer, "\r\n\r\n");
            int length = headerPartPtr - Buffer + 4;
            char *headerPart = new char[length + 1];
            strncpy(headerPart, Buffer, length);
            headerPart[length] = '\0';
            printf("%s\n", headerPart);
            delete[] headerPart;
        }
    }
    // ������
error:
    printf("error���ر��׽���\n\n");
    Sleep(200);
    closesocket(((ProxyParam *)lpParameter)->clientSocket);
    closesocket(((ProxyParam *)lpParameter)->serverSocket);
    free(lpParameter);
    _endthreadex(0);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("�����������������\n");
    printf("��ʼ��...\n");
    if (!InitWsa())
    {
        printf("WSA ��ʼ��ʧ��\n");
        return -1;
    }
    if (!InitSocket())
    {
        printf("��������� socket ��ʼ��ʧ��\n");
        return -1;
    }
    printf("����������������У������˿� %d\n", ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET; // ��������ͨ�ŵ��׽��֣���ʼ��Ϊ��Ч�׽���
    SOCKADDR_IN acceptAddr;               // �ͻ��˶˵���Ϣ
    ProxyParam *lpProxyParam;             // �������������
    HANDLE hThread;                       // �߳̾��
    DWORD dwThreadID;                     // �߳�ID

    // ������������ϼ���
    while (true)
    {
        acceptSocket = accept(ProxyServer, (SOCKADDR *)&acceptAddr, NULL);

        lpProxyParam = new ProxyParam;
        if (lpProxyParam == NULL)
        {
            continue;
        }
        lpProxyParam->clientSocket = acceptSocket;
        hThread = (HANDLE)_beginthreadex(NULL, 0,
                                         &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
        CloseHandle(hThread);
        Sleep(200);
    }
    closesocket(ProxyServer);
    WSACleanup();
    return 0;
}