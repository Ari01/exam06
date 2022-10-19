#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

// types
typedef struct	s_user
{
    int		    id;
    int		    fd;
    struct s_user *next;
}   t_user;

// globals
int	    sockfd;
fd_set	    currfds, wrfds, rdfds;
char	    buff[4096 * 42], tmp[4096 * 42], msg[4096 * 42 + 42];

// utils
int get_maxfd(t_user *users)
{
    int	ret = sockfd;

    while (users)
    {
	if (users->fd > ret)
	    ret = users->fd;
	users = users->next;
    }
    return (ret);
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

// mem
void clear_users(t_user **users)
{
    if (users)
    {
	if (*users)
	{
	    clear_users(&(*users)->next);
	    FD_CLR((*users)->fd, &currfds);
	    close((*users)->fd);
	    free(*users);
	}
    }
}

void	fatal(t_user **users)
{
    clear_users(users);
    FD_CLR(sockfd, &currfds);
    close(sockfd);
    write(2, "Fatal error\n", strlen("Fatal error\n"));
    exit(1);
}

// user
void	add_user(t_user **users, int fd)
{
    t_user  *toadd;
    t_user  *ite;

    if (users)
    {
	toadd = malloc(sizeof(*toadd));
	toadd->fd = fd;
	toadd->next = NULL;
	ite = *users;
	if (!ite)
	{
	    toadd->id = 0;
	    *users = toadd;
	}
	else
	{
	    while (ite->next)
		ite = ite->next;
	    toadd->id = ite->id + 1;
	    ite->next = toadd;
	}
    }
}

void	delete_user(t_user **users, int fd)
{
    t_user  *todel;
    t_user  *ite;

    if (users && *users)
    {
	ite = *users;
	if (ite->fd == fd)
	{
	    todel = ite;
	    *users = (*users)->next;
	}
	else
	{
	    while (ite->next && ite->next->fd != fd)
		ite = ite->next;
	    if (!ite->next)
		return ;
	    todel = ite->next;
	    ite->next = ite->next->next;
	}
	FD_CLR(fd, &currfds);
	close(fd);
	free(todel);
    }
}

// msg
char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

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
    t_user  *ite;

    if (users)
    {
	ite = *users;
	while (ite)
	{
	    if (ite->fd != fd && FD_ISSET(ite->fd, &wrfds))
	    {
		if (send(ite->fd, msg, strlen(msg), 0) < 0)
		    fatal(users);
	    }
	    ite = ite->next;
	}
    }
}

void	extract_message(t_user **users, int fd)
{
    int	i,j;

    i = 0;
    j = 0;
    while (buff[i])
    {
	tmp[j] = buff[i];
	j++;
	if (buff[i] == '\n')
	{
	    sprintf(msg, "client %d: %s", get_user_id(*users, fd), tmp);
	    send_to_all(users, msg, fd);
	    bzero(&tmp, strlen(tmp));
	    bzero(&msg, strlen(msg));
	    j = 0;
	}
	i++;
    }
    bzero(&buff, strlen(buff));
}


int	receive_msg(t_user **users, int fd)
{
    int	    ret, id;
    char    short_msg[42];

    ret = 1000;
    id = get_user_id(*users, fd);
    while (ret == 1000 || buff[strlen(buff) - 1] != '\n')
    {
	ret = recv(fd, buff + strlen(buff), 1000, 0);
	if (ret <= 0)
	    break ;
    }
    if (ret < 0)
	fatal(users);
    else if (!ret)
    {
	sprintf(short_msg, "server: client %d just left\n", id);
	send_to_all(users, short_msg, fd);
	delete_user(users, fd);
	return (0);
    }
    else
	extract_message(users, fd);
    return (1);
}

void	accept_user(t_user **users)
{
    int			connfd;
    socklen_t		len;
    struct sockaddr_in	cli;
    char		short_msg[42];

    len = sizeof(cli);
    connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
    if (connfd < 0)
	fatal(users);
    add_user(users, connfd);
    sprintf(short_msg, "server: client %d just arrived\n", get_user_id(*users, connfd));
    send_to_all(users, short_msg, connfd);
    FD_SET(connfd, &currfds);
}

int main(int ac, char **av) {
	int		    fd, maxfd;
	unsigned int	    port;
	struct sockaddr_in  servaddr;
	t_user		    *users;

	// init
	if (ac != 2)
	{
	    write(2, "Wrong number of arguments\n", strlen("Wrong number of arguments\n"));
	    exit(1);
	}
	port = atoi(av[1]);
	users = NULL;

	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
	    fatal(&users);
	} 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(port); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
	    fatal(&users);
	} 
	if (listen(sockfd, 0) != 0) {
	    fatal(&users);
	}

	// main loop
	FD_ZERO(&currfds);
	FD_SET(sockfd, &currfds);
	bzero(&msg, sizeof(msg));
	bzero(&buff, sizeof(buff));
	bzero(&tmp, sizeof(tmp));
	while (1)
	{
	    wrfds = rdfds = currfds;
	    maxfd = get_maxfd(users);
	    if (select(maxfd + 1, &rdfds, &wrfds, NULL, NULL) < 0)
		continue ;
	    for (fd = 0; fd <= maxfd; fd++)
	    {
		if (FD_ISSET(fd, &rdfds))
		{
		    if (fd == sockfd)
		    {
			accept_user(&users);
			break ;
		    }
		    else if (!receive_msg(&users, fd))
			break ;
		}
	    }
	}
}
