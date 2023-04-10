/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#include "CEngine.h"

/* Functions */
bool CEngine::CCreateSocket(int protocol, CSocket *s)
{
	int type;
	if (protocol == IPPROTO_TCP)
		type = SOCK_STREAM;
	else if (protocol == IPPROTO_UDP)
		type = SOCK_DGRAM;
	(*s) = socket(AF_INET, type, protocol);
	if ((*s) == CSOCKET_INVALID)
	{
		strcpy(inerr, "CCreateSocket: Failed to create the socket.");
		return false;
	}
	return true;
}

bool CEngine::CShutdownSocket(CSocket s, int what)
{
	if (shutdown(s, what) == CSOCKET_ERROR)
		return false;
	return true;
}

bool CEngine::CBindSocket(CSocket s, unsigned short port)
{
	sockaddr_in target;
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
	target.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (sockaddr*)&target, sizeof(target)) == CSOCKET_ERROR)
	{
		strcpy(inerr, "CBindSocket: Failed to bind the socket.");
		return false;
	}
	return true;
}

bool CEngine::CListenSocket(CSocket s, int connections)
{
	if (listen(s, connections) == CSOCKET_ERROR)
	{
		strcpy(inerr, "CListenSocket: Failed to set the socket to listening state.");
		return false;
	}
	return true;
}

void CEngine::CSetSockAddr(CSockAddr* address, unsigned short port, char *ipaddress)
{
	memset(address, 0, sizeof(CSockAddr));
	address->sin_family = AF_INET;
    address->sin_port = htons(port);
    address->sin_addr.s_addr = inet_addr(ipaddress);
}

#if defined(COS_WIN32)
void CEngine::CCloseSocket(CSocket s)
{
	closesocket(s);
}

bool CEngine::CSocketMode(CSocket s, unsigned long mode)
{
	if (ioctlsocket(s, FIONBIO, &mode) == SOCKET_ERROR)
	{
		strcpy(inerr, "CSocketMode: Failed to change the socket mode.");
		return false;
	}
	return true;
}

int CEngine::CConnectSocket(CSocket s, unsigned short port, char *ipaddress)
{
	SOCKADDR_IN target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr(ipaddress);
	if (connect(s, (SOCKADDR*)&target, sizeof(target)) == SOCKET_ERROR)
	{
		int e = WSAGetLastError();
		if (e == WSAEISCONN)
			return CTCP_DONE;
		else if (e != WSAEWOULDBLOCK && e != WSAEALREADY)
		{
			strcpy(inerr, "CConnectSocket: Failed to connect the socket. Error ");
			char f[6];
			sprintf(f, "%d", e);
			strcat(inerr, f);
			return CTCP_ERROR;
		}
		return CTCP_WAIT;
	}
	return CTCP_DONE;
}

int CEngine::CSend(CSocket s, char *buf, int bytes)
{
	int r = send(s, buf, bytes, 0);
	if (r == SOCKET_ERROR)
	{
		int e = WSAGetLastError();
		if (e == WSAEWOULDBLOCK)
			return CTCP_WAIT;
		else
		{
			strcpy(inerr, "CSend: Failed to send data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", e);
			strcat(inerr, f);
			return CTCP_ERROR;
		}
	}
	return r;
}

int CEngine::CReceive(CSocket s, char *buf, int bufsize)
{
	int r = recv(s, buf, bufsize, 0);
	if (r == SOCKET_ERROR)
	{
		int e = WSAGetLastError();
		if (e != WSAEWOULDBLOCK)
		{
			strcpy(inerr, "CReceive: Failed to receive data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", e);
			strcat(inerr, f);
			return CTCP_ERROR;
		}
		else
			return CTCP_WAIT;
	}
	return r;
}

int CEngine::CAcceptConnection(CSocket s, CSocket *client, char *ipaddress)
{
	sockaddr_in info;
	int size = sizeof(info);
	(*client) = accept(s, (sockaddr*)&info, &size);
	if (ipaddress != NULL)
		strcpy(ipaddress, inet_ntoa(info.sin_addr));
	if ((*client) == CSOCKET_INVALID)
	{
		int e = WSAGetLastError();
		if (e == WSAEWOULDBLOCK)
			return CTCP_WAIT;
		strcpy(inerr, "CAcceptConnection: Failed to accept an incoming connection. Error ");
		char f[6];
		sprintf(f, "%d", e);
		strcat(inerr, f);
		return CTCP_ERROR;
	}
	return CTCP_DONE;
}

bool CEngine::CVerifySocket(CSocket *s, unsigned char mode, unsigned int count, long msec)
{
	unsigned int i;
	int r;
	fd_set data;
	timeval period;
	data.fd_count = count;
	CLoops(i, 0, count)
	{
		data.fd_array[i] = s[i];
	}
	if (msec >= 0)
	{
		period.tv_sec = 0;
		period.tv_usec = msec * 1000;
	}
	if (mode == CSOCKET_READABLE)
		r = select(0, &data, NULL, NULL, (msec == -1 ? NULL : &period));
	else if (mode == CSOCKET_WRITABLE)
		r = select(0, NULL, &data, NULL, (msec == -1 ? NULL : &period));
	if (r == CSOCKET_ERROR)
	{
		strcpy(inerr, "CVerifySocket: Failed to verify the socket.");
		return false;
	}
	else if (r != count)
		return false;
	return true;
}

int CEngine::CSendTo(CSocket s, CSockAddr* address, char *buf, int bytes)
{
	int r = sendto(s, buf, bytes, 0, (SOCKADDR*)address, sizeof(SOCKADDR_IN));
	if (r == SOCKET_ERROR)
	{
		int e = WSAGetLastError();
		if (e != WSAEWOULDBLOCK)
		{
			strcpy(inerr, "CSendTo: Failed to send data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", e);
			strcat(inerr, f);
			return CUDP_ERROR;
		}
		else
			return CUDP_WAIT;
	}
	return r;
}

int CEngine::CRecvFrom(CSocket s, CSockAddr* from, char *buf, int bufsize)
{
	int r;
	if (from != NULL)
	{
		int asize = sizeof(CSockAddr);
		r = recvfrom(s, buf, bufsize, 0, (SOCKADDR*)from, &asize);
	}
	else
		r = recvfrom(s, buf, bufsize, 0, NULL, NULL);
	if (r == SOCKET_ERROR)
	{
		int e = WSAGetLastError();
		if (e != WSAEWOULDBLOCK)
		{
			strcpy(inerr, "CRecvFrom: Failed to receive data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", e);
			strcat(inerr, f);
			return CUDP_ERROR;
		}
		else
			return CUDP_WAIT;
	}
	return r;
}
#elif defined(COS_LINUX)
void CEngine::CCloseSocket(CSocket s)
{
	close(s);
}

bool CEngine::CSocketMode(CSocket s, unsigned long mode)
{
	if (ioctl(s, FIONBIO, &mode) == -1)
	{
		strcpy(inerr, "CSocketMode: Failed to change the socket mode.");
		return false;
	}
	return true;
}

int CEngine::CConnectSocket(CSocket s, unsigned short port, char *ipaddress)
{
	sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr(ipaddress);
	if (connect(s, (sockaddr*)&target, sizeof(target)) == -1)
	{
		if (errno == EISCONN)
			return CTCP_DONE;
		else if (errno != EWOULDBLOCK && errno != EINPROGRESS && errno != EALREADY)
		{
			strcpy(inerr, "CConnectSocket: Failed to connect the socket. Error ");
			char f[6];
			sprintf(f, "%d", errno);
			strcat(inerr, f);
			return CTCP_ERROR;
		}
		return CTCP_WAIT;
	}
	return CTCP_DONE;
}

int CEngine::CSend(CSocket s, char *buf, int bytes)
{
	int r = send(s, buf, bytes, 0);
	if (r == -1)
	{
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			bytes = CTCP_WAIT;
		else
		{
			strcpy(inerr, "CSend: Failed to send data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", errno);
			strcat(inerr, f);
			return CTCP_ERROR;
		}
	}
	return r;
}

int CEngine::CReceive(CSocket s, char *buf, int bufsize)
{
	int r = recv(s, buf, bufsize, 0);
	if (r == -1)
	{
		if (errno != EWOULDBLOCK && errno != EAGAIN)
		{
			strcpy(inerr, "CReceive: Failed to receive data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", errno);
			strcat(inerr, f);
			return CTCP_ERROR;
		}
		else
			return CTCP_WAIT;
	}
	return r;
}

int CEngine::CAcceptConnection(CSocket s, CSocket *client, char *ipaddress)
{
	sockaddr_in info;
	socklen_t size = sizeof(info);
	(*client) = accept(s, (sockaddr*)&info, &size);
	if (ipaddress != NULL)
		strcpy(ipaddress, inet_ntoa(info.sin_addr));
	if ((*client) == -1)
	{
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return CTCP_WAIT;
		strcpy(inerr, "CAcceptConnection: Failed to accept an incoming connection. Error ");
		char f[6];
		sprintf(f, "%d", errno);
		strcat(inerr, f);
		return CTCP_ERROR;
	}
	return CTCP_DONE;
}

bool CEngine::CVerifySocket(CSocket *s, unsigned char mode, unsigned int count, long msec)
{
	unsigned int i;
	int r;
	fd_set data;
	timeval period;
	if (msec >= 0)
	{
		period.tv_sec = 0;
		period.tv_usec = msec * 1000;
	}
	FD_ZERO(&data);
	CLoops(i, 0, count)
	{
		FD_SET(s[i], &data);
	}
	if (mode == CSOCKET_READABLE)
		r = select(NULL, &data, NULL, NULL, (msec == -1 ? NULL : &period));
	else if (mode == CSOCKET_WRITABLE)
		r = select(NULL, NULL, &data, NULL, (msec == -1 ? NULL : &period));
	if (r == CSOCKET_ERROR)
	{
		strcpy(inerr, "CVerifySocket: Failed to connect the socket.");
		return false;
	}
	else if (r != count)
		return false;
	return true;
}

int CEngine::CSendTo(CSocket s, CSockAddr* address, char *buf, int bytes)
{
	int r = sendto(s, buf, bytes, 0, (sockaddr*)address, sizeof(sockaddr_in));
	if (r == -1)
	{	
		if (errno != EWOULDBLOCK && errno != EAGAIN)
		{
			strcpy(inerr, "CSendTo: Failed to send data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", errno);
			strcat(inerr, f);
			return CUDP_ERROR;
		}
		else
			return CUDP_WAIT;
	}
	return r;
}

int CEngine::CRecvFrom(CSocket s, CSockAddr* from, char *buf, int bufsize)
{
	int r;
	if (from != NULL)
	{
		socklen_t asize = sizeof(CSockAddr);
		r = recvfrom(s, buf, bufsize, 0, (sockaddr*)from, &asize);
	}
	else
		r = recvfrom(s, buf, bufsize, 0, NULL, NULL);
	if (r == -1)
	{
		if (errno != EWOULDBLOCK && errno != EAGAIN)
		{
			strcpy(inerr, "CRecvFrom: Failed to receive data on the socket. Error ");
			char f[6];
			sprintf(f, "%d", errno);
			strcat(inerr, f);
			return CUDP_ERROR;
		}
		else
			return CUDP_WAIT;
	}
	return r;
}
#endif
bool CEngine::CSetupTcpSession(CTcpSession* net)
{
	if (!CSocketMode(net->tcp, CSOCKET_NONBLOCKING))
		return false;
	net->process = false;
	net->msgpos = 0;
	return true;
}

void CEngine::CCloseTcpSession(CTcpSession* net)
{
	CShutdownSocket(net->tcp, CSOCKET_BOTH);
	CCloseSocket(net->tcp);
}

int CEngine::CSendMsg(CTcpSession* net, char *msg, unsigned char size)
{
	if (!net->process)
	{
		net->buf[0] = size;
		memcpy(net->buf + 1, msg, size);
		net->process = true;
	}
	net->msgsize = CSend(net->tcp, net->buf, size + 1);
	if (net->msgsize > 0)
	{
		net->msgpos += net->msgsize;
		if (net->msgpos < size + 1)
			return CTCP_WAIT;
	}
	else
		return net->msgsize;
	net->msgpos = 0;
	net->process = false;
	return CTCP_DONE;
}

int CEngine::CRecvMsg(CTcpSession* net, char *msg, unsigned char *size)
{
	if (!net->process)
	{
		net->msgsize = CReceive(net->tcp, net->buf + net->msgpos, CTCP_MSGSIZE - net->msgpos);
		if (net->msgsize > 0)
		{
			if (net->msgpos != 0)
			{
				net->msgsize += net->msgpos;
				net->msgpos = 0;
			}
			net->process = true;
		}
		else
			return net->msgsize;
	}
	if (net->msgsize <= net->buf[net->msgpos] + net->msgpos)
	{
		CShiftCharArray(net->buf, -net->msgpos, CTCP_MSGSIZE);
		net->msgpos = net->msgsize - net->msgpos;
		net->process = false;
		return CTCP_WAIT;
	}
	memcpy(msg, net->buf + net->msgpos + 1, net->buf[net->msgpos]);
	if (size != NULL)
		(*size) = (unsigned char)net->buf[net->msgpos];
	net->msgpos += net->buf[net->msgpos] + 1;
	if (net->msgpos == net->msgsize)
	{
		net->msgpos = 0;
		net->process = false;
	}
	return CTCP_DONE;
}
