#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

int 			g_sockfd = 0;
fd_set			rdfds, wrfds, currfds;

// STRUCT
typedef struct s_user
{
	int 			fd;
	int 			id;
	struct s_user	*next;
}	t_user;

// FATAL
void	clear_users(t_user **user_list)
{
	if (user_list)
	{
	    if (*user_list && (*user_list)->next)
	    {
		clear_users(&(*user_list)->next);
		FD_CLR((*user_list)->fd, &currfds);
		close((*user_list)->fd);
		free(*user_list);
		*user_list = NULL;
	    }
	}
}

void	fatal(t_user **user_list)
{
	write(2, "Fatal error\n", strlen("Fatal error\n"));
	clear_users(user_list);
	close(g_sockfd);
	exit(1);
}

// USERS
void	add_user_to_list(t_user **user_list, int fd)
{
	t_user	*user;
	t_user	*tmp;

	if (user_list)
	{
	    tmp = malloc(sizeof(*user));
	    if (!tmp)
		    fatal(user_list);
	    tmp->fd = fd;
	    tmp->next = NULL;
	    user = *user_list;
	    if (!user)
	    {
		tmp->id = 0;
		*user_list = tmp;
	    }
	    else
	    {
		while (user->next)
			user = user->next;
		tmp->id = user->id + 1;
		user->next = tmp;
	    }
	}
}

void	delete_user(t_user **users, int fd)
{
	t_user	*tmp;
	t_user	*todel;

	tmp = *users;
	if (users && tmp)
	{
	    if (tmp->fd == fd)
	    {
		todel = tmp;
		*users = (*users)->next;
	    }
	    else
	    {
		while (tmp->next && tmp->next->fd != fd)
		    tmp = tmp->next;
		if (tmp->next)
		{
		    todel = tmp->next;
		    tmp->next = tmp->next->next;
		}
		else
		    return ;
	    }
	    FD_CLR(fd, &currfds);
	    close(fd);
	    free(todel);
	}
}

int	get_user_id(t_user *users, int fd)
{
	while (users)
	{
	    if (users->fd == fd)
		return (users->id);
	    users = users->next;
	}
	return (-1);
}

// MSG
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

// SEND TO ALL
void	send_to_all(t_user **users, char *msg, int fd)
{
	t_user	*tmp;

	tmp = *users;
	while (tmp)
	{
	    if (fd != tmp->fd && FD_ISSET(tmp->fd, &wrfds))
	    {
		if (send(tmp->fd, msg, strlen(msg), 0) < 0)
		    fatal(users);
	    }
	    tmp = tmp->next;
	}
}

// SELECT
int	get_max_fd(t_user *list)
{
	int ret = g_sockfd;

	while (list)
	{
	    if (ret < list->fd)
		ret = list->fd;
	    list = list->next;
	}
	return (ret);
}

// ACCEPT
void	accept_user(t_user **users)
{
	struct sockaddr_in	cli;
	socklen_t		len;
	int		    	connfd;
	char			msg[42];

	len = sizeof(cli);
	connfd = accept(g_sockfd, (struct sockaddr *)&cli, &len);
	if (connfd < 0)
		fatal(users); 
	add_user_to_list(users, connfd);
	sprintf(msg, "server: client %d just arrived\n", get_user_id(*users, connfd));
	send_to_all(users, msg, connfd);
	FD_SET(connfd, &currfds);
}

// RECEIVE
void	receive_msg(t_user **users, int fd)
{
	char	tmp[1001];
	int	ret;
	char	*buff;
	char	short_msg[42];
	char	*msg;

	buff = NULL;
	msg = NULL;
	ret = 1000;
	sprintf(short_msg, "client %d: ", get_user_id(*users, fd));
	buff = str_join(buff, short_msg);
	while (ret == 1000 || tmp[ret - 1] != '\n')
	{
	    ret = recv(fd, tmp, 1000, 0);
	    if (ret <= 0)
		break ;
	    tmp[ret] = 0;
	    buff = str_join(buff, tmp);
	    if (!buff)
		fatal(users);
	}
	if (ret <= 0)
	{
	    free(buff);
	    free(msg);
	    sprintf(short_msg, "server: client %d just left\n", get_user_id(*users, fd));
	    delete_user(users, fd);
	    send_to_all(users, short_msg, fd);
	}
	else
	{
	    ret = extract_message(&buff, &msg);
	    while (ret > 0)
	    {
		send_to_all(users, msg, fd);
		free(msg);
		msg = NULL;
		ret = extract_message(&buff, &msg);
	    }
	    free(buff);
	    free(msg);
	    if (ret == -1)
		fatal(users);
	}
}

int main(int ac, char **av)
{
	int		maxfd;
	struct		sockaddr_in servaddr;
	unsigned int	port; 
	t_user		*users;

	if (ac != 2)
	{
	    write(2, "Wrong number of arguments\n", strlen("Wrong number of arguments\n"));
	    exit(1);
	}

	// socket create and verification 
	users = NULL;
	g_sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (g_sockfd == -1) 
	    fatal(&users);
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	port = atoi(av[1]);
	servaddr.sin_port = htons(port); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(g_sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
	    fatal(&users); 
	if (listen(g_sockfd, 0) != 0)
	    fatal(&users);

	// Main loop
	FD_ZERO(&currfds);
	FD_SET(g_sockfd, &currfds);

	while (1)
	{
	    maxfd = get_max_fd(users);
	    wrfds = rdfds = currfds;
	    if (select(maxfd + 1, &rdfds, &wrfds, NULL, NULL) < 0)
		continue;
	    for (int fd = 0; fd <= maxfd; fd++)
	    {
		if (FD_ISSET(fd, &rdfds))
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
}
