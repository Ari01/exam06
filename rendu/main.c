#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

// globals
int	g_sockfd;
fd_set	g_currset, g_rdset, g_wrset;

// types
typedef struct s_user
{
    int		    fd;
    int		    id;
    struct s_user   *next;
}   t_user;

// utilities
int get_max_fd(t_user *users)
{
    int	ret;

    ret = g_sockfd;
    while (users)
    {
	if (users->fd > ret)
	    ret = users->fd;
	users = users->next;
    }
    return (ret);
}

// memory
void	clear_users(t_user **users)
{
    t_user  *tmp;

    if (users)
    {
	tmp = *users;
	if (tmp)
	{
	    clear_users(&(tmp->next));
	    FD_CLR(tmp->fd, &g_currset);
	    close(tmp->fd);
	    free(tmp);
	    tmp = NULL;
	}
    }
}

void	fatal(t_user **users)
{
    clear_users(users);
    close(g_sockfd);
    write(STDERR_FILENO, "Fatal error\n", strlen("Fatal error\n"));
    exit(1);
}

// users
void	add_user(t_user **users, int fd)
{
    t_user  *tmp;
    t_user  *ite;

    if (users)
    {
	ite = *users;
	tmp = malloc(sizeof(*tmp));
	if (!tmp)
	    fatal(users);
	tmp->fd = fd;
	tmp->next = NULL;
	if (!ite)
	{
	    tmp->id = 0;
	    *users = tmp;
	}
	else
	{
	    while (ite->next)
		ite = ite->next;
	    tmp->id = ite->id + 1;
	    ite->next = tmp;
	}
    }
}

void	delete_user(t_user **users, int fd)
{
    t_user  *ite;
    t_user  *todel;

    if (users)
    {
	ite = *users;
	if (!ite)
	    return ;
	if (ite->fd == fd)
	{
	    *users = ite->next;
	    todel = ite;
	}
	else
	{
	    while (ite->next && ite->next->fd != fd)
		ite = ite->next;
	    if (!ite->next)
		return ;
	    else
	    {
		todel = ite->next;
		ite->next = ite->next->next;
	    }
	}
	FD_CLR(fd, &g_currset);
	close(fd);
	free(todel);
	todel = NULL;
    }
}

int get_user_id(t_user *users, int fd)
{
    while (users)
    {
	if (users->fd == fd)
	    return (users->id);
	users = users->next;
    }
    return (-1);
}

// client messages
int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int	len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void	send_to_all(t_user **users, char *msg, int fd)
{
    t_user  *tmp;

    if (users)
    {
	tmp = *users;
	while (tmp)
	{
	    if (tmp->fd != fd && FD_ISSET(tmp->fd, &g_wrset))
	    {
		if (send(tmp->fd, msg, strlen(msg), 0) == -1)
		    fatal(users);
	    }
	    tmp = tmp->next;
	}
    }
}

void	receive_msg(t_user **users, int fd)
{
    int	    ret, id;
    char    tmp[1001];
    char    short_msg[42];
    char    *buff;
    char    *msg;
    char    *extract;

    ret = 1000;
    buff = 0;
    msg = 0;
    id = get_user_id(*users, fd);
    while (ret == 1000 || tmp[ret - 1] != '\n')
    {
	ret = recv(fd, tmp, 1000, 0);
	tmp[ret] = 0;
	if (ret <= 0)
	    break ;
	buff = str_join(buff, tmp);
	if (!buff)
	    fatal(users);
    }
    if (ret <= 0)
    {
	sprintf(short_msg, "server: client %d just left\n", id);
	send_to_all(users, short_msg, fd);
	delete_user(users, fd);
    }
    else
    {
	sprintf(short_msg, "client %d: ", id);
	ret = extract_message(&buff, &extract);
	while (ret == 1)
	{
	    msg = malloc(sizeof(*msg) * (strlen(short_msg) + strlen(extract) + 1));
	    if (!msg)
		fatal(users);
	    *msg = 0;
	    strcat(msg, short_msg);
	    strcat(msg, extract);
	    send_to_all(users, msg, fd);
	    free(extract);
	    free(msg);
	    ret = extract_message(&buff, &extract);
	}
	free(extract);
	free(buff);
	if (ret == -1)
	    fatal(users);
    }
}

// accept new user
void	accept_user(t_user **users)
{
	int			connfd;
	socklen_t		len;
	struct sockaddr_in	cli;
	char			msg[42];

	len = sizeof(cli);
	connfd = accept(g_sockfd, (struct sockaddr *)&cli, &len);
	if (connfd < 0)
	    fatal(users);
	add_user(users, connfd);
	sprintf(msg, "server: client %d just arrived\n", get_user_id(*users, connfd));
	send_to_all(users, msg, connfd);
	FD_SET(connfd, &g_currset);
}

int main(int ac, char **av) {
	struct sockaddr_in	servaddr; 
	unsigned int		port;
	t_user			*users;
	int			fd, maxfd;

	// init
	if (ac != 2)
	{
	    write(STDERR_FILENO, "Wrong number of arguments\n", strlen("Wrong number of arguments\n"));
	    exit(1);
	}
	users = NULL;
	port = atoi(av[1]);

	// socket create and verification 
	g_sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (g_sockfd == -1)
	    fatal(&users);
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(port); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(g_sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
	    fatal(&users);
	if (listen(g_sockfd, 0) != 0)
	    fatal(&users);
	
	// main loop
	FD_ZERO(&g_currset);
	FD_SET(g_sockfd, &g_currset);
	while (1)
	{
	    g_wrset = g_rdset = g_currset;
	    maxfd = get_max_fd(users);
	    if (select(maxfd + 1, &g_rdset, &g_wrset, NULL, NULL) < 0)
		continue ;
	    for (fd = 0; fd <= maxfd; fd++)
	    {
		if (FD_ISSET(fd, &g_rdset))
		{
		    if (fd == g_sockfd)
			accept_user(&users);
		    else
			receive_msg(&users, fd);
		}
	    }
	}
	clear_users(&users);
	close(g_sockfd);
	return (0);
}
