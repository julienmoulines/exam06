#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <stdio.h>

/* --- erreurs sujet --- */

static void	wrong_args(void)
{
	write(2, "Wrong number of arguments\n", 26);
	exit(1);
}

static void	fatal(void)
{
	write(2, "Fatal error\n", 12);
	exit(1);
}

/* --- buffers dynamiques (only allowed funcs) --- */

static void	append_bytes(char **buf, int *len, const char *add, int add_len)
{
	char	*nb;
	int		i;

	if (add_len <= 0)
		return ;
	nb = (char *)realloc(*buf, (size_t)(*len + add_len));
	if (!nb)
		fatal();
	i = 0;
	while (i < add_len)
	{
		nb[*len + i] = add[i];
		i++;
	}
	*buf = nb;
	*len += add_len;
}

static void	consume_front(char **buf, int *len, int n)
{
	int	rem;
	int	i;
	char	*shr;

	if (!*buf || n <= 0 || n > *len)
		return ;
	rem = *len - n;
	i = 0;
	while (i < rem)
	{
		(*buf)[i] = (*buf)[n + i];
		i++;
	}
	*len = rem;
	if (rem == 0)
	{
		free(*buf);
		*buf = 0;
	}
	else
	{
		shr = (char *)realloc(*buf, (size_t)rem);
		if (shr)
			*buf = shr;
	}
}

/* renvoie 1 si une ligne terminée par '\n' est extraite (sans le '\n') */
static int	extract_line(char **in, int *ilen, char **line, int *llen)
{
	int		i;
	int		k;
	int		L;
	char	*out;

	if (!*in || *ilen <= 0)
		return (0);
	i = 0;
	while (i < *ilen)
	{
		if ((*in)[i] == '\n')
		{
			L = i;
			out = (char *)malloc((size_t)L + 1);
			if (!out)
				fatal();
			k = 0;
			while (k < L)
			{
				out[k] = (*in)[k];
				k++;
			}
			out[L] = '\0';
			*line = out;
			*llen = L;
			consume_front(in, ilen, L + 1);
			return (1);
		}
		i++;
	}
	return (0);
}

/* flush le reste non terminé par '\n' à la déconnexion (sans ajouter de '\n') */
static void	flush_leftover_line(
	int fd, int *ids, char **inbuf, int *inlen,
	int maxfd, fd_set *master, int sockfd,
	char **outbuf, int *outlen)
{
	char	pre[64];
	int		pn;
	int		tot;
	char	*final;
	int		k;

	if (!*inbuf || *inlen <= 0)
		return ;
	pn = sprintf(pre, "client %d: ", ids[fd]);
	if (pn < 0)
		pn = 0;
	tot = pn + *inlen;
	final = (char *)malloc((size_t)tot);
	if (!final)
	{
		free(*inbuf);
		*inbuf = 0;
		*inlen = 0;
		fatal();
	}
	k = 0;
	while (k < pn)
	{
		final[k] = pre[k];
		k++;
	}
	k = 0;
	while (k < *inlen)
	{
		final[pn + k] = (*inbuf)[k];
		k++;
	}
	k = 0;
	while (k <= maxfd)
	{
		if (k != fd && k != sockfd && FD_ISSET(k, master) && ids[k] >= 0)
			append_bytes(&outbuf[k], &outlen[k], final, tot);
		k++;
	}
	free(final);
	free(*inbuf);
	*inbuf = 0;
	*inlen = 0;
}

/* --- serveur --- */

int	main(int ac, char **av)
{
	int					sockfd;
	struct sockaddr_in	serv;
	int					maxfd;
	int					next_id;
	int					ids[FD_SETSIZE];
	char				*inbuf[FD_SETSIZE];
	int					inlen[FD_SETSIZE];
	char				*outbuf[FD_SETSIZE];
	int					outlen[FD_SETSIZE];
	fd_set				master;
	fd_set				rfds;
	fd_set				wfds;

	if (ac != 2)
		wrong_args();
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		fatal();
	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = htonl(2130706433u);
	serv.sin_port = htons(atoi(av[1]));
	if (bind(sockfd, (const struct sockaddr *)&serv, sizeof(serv)) < 0)
		fatal();
	if (listen(sockfd, 128) < 0)
		fatal();
	maxfd = sockfd;
	next_id = 0;
	for (int i = 0; i < FD_SETSIZE; i++)
	{
		ids[i] = -1;
		inbuf[i] = 0;
		inlen[i] = 0;
		outbuf[i] = 0;
		outlen[i] = 0;
	}
	FD_ZERO(&master);
	FD_SET(sockfd, &master);
	while (1)
	{
		rfds = master;
		FD_ZERO(&wfds);
		for (int fd = 0; fd <= maxfd; fd++)
			if (fd != sockfd && FD_ISSET(fd, &master) && outlen[fd] > 0)
				FD_SET(fd, &wfds);
		if (select(maxfd + 1, &rfds, &wfds, 0, 0) < 0)
			continue ;
		if (FD_ISSET(sockfd, &rfds))
		{
			struct sockaddr_in	cli;
			socklen_t			clen;
			int					cfd;
			char				m[128];
			int					n;

			clen = sizeof(cli);
			cfd = accept(sockfd, (struct sockaddr *)&cli, &clen);
			if (cfd >= 0 && cfd < FD_SETSIZE)
			{
				FD_SET(cfd, &master);
				if (cfd > maxfd)
					maxfd = cfd;
				ids[cfd] = next_id++;
				n = sprintf(m, "server: client %d just arrived\n", ids[cfd]);
				if (n > 0)
					for (int k = 0; k <= maxfd; k++)
						if (k != cfd && k != sockfd && FD_ISSET(k, &master) && ids[k] >= 0)
							append_bytes(&outbuf[k], &outlen[k], m, n);
			}
		}
		for (int fd = 0; fd <= maxfd; fd++)
		{
			if (fd == sockfd || !FD_ISSET(fd, &rfds) || !FD_ISSET(fd, &master))
				continue ;
			{
				char	tmp[4096];
				int		r;

				r = (int)recv(fd, tmp, sizeof(tmp), 0);
				if (r <= 0)
				{
					char	m[128];
					int		n;

					flush_leftover_line(fd, ids, &inbuf[fd], &inlen[fd],
						maxfd, &master, sockfd, outbuf, outlen);
					if (ids[fd] >= 0)
					{
						n = sprintf(m, "server: client %d just left\n", ids[fd]);
						if (n > 0)
							for (int k = 0; k <= maxfd; k++)
								if (k != fd && k != sockfd && FD_ISSET(k, &master)
									&& ids[k] >= 0)
									append_bytes(&outbuf[k], &outlen[k], m, n);
					}
					FD_CLR(fd, &master);
					ids[fd] = -1;
					if (inbuf[fd])
					{
						free(inbuf[fd]);
						inbuf[fd] = 0;
						inlen[fd] = 0;
					}
					if (outbuf[fd])
					{
						free(outbuf[fd]);
						outbuf[fd] = 0;
						outlen[fd] = 0;
					}
					close(fd);
					continue ;
				}
				append_bytes(&inbuf[fd], &inlen[fd], tmp, r);
				while (1)
				{
					char	*line;
					int		L;
					char	pre[64];
					int		pn;
					int		tot;
					char	*final;

					line = 0;
					L = 0;
					if (!extract_line(&inbuf[fd], &inlen[fd], &line, &L))
						break ;
					pn = sprintf(pre, "client %d: ", ids[fd]);
					if (pn < 0)
						pn = 0;
					tot = pn + L + 1;
					final = (char *)malloc((size_t)tot);
					if (!final)
					{
						free(line);
						fatal();
					}
					for (int i = 0; i < pn; i++)
						final[i] = pre[i];
					for (int i = 0; i < L; i++)
						final[pn + i] = line[i];
					final[pn + L] = '\n';
					for (int k = 0; k <= maxfd; k++)
						if (k != fd && k != sockfd && FD_ISSET(k, &master) && ids[k] >= 0)
							append_bytes(&outbuf[k], &outlen[k], final, tot);
					free(final);
					free(line);
				}
			}
		}
		for (int fd = 0; fd <= maxfd; fd++)
		{
			if (fd == sockfd || !FD_ISSET(fd, &wfds) || !FD_ISSET(fd, &master))
				continue ;
			if (outlen[fd] <= 0 || !outbuf[fd])
				continue ;
			{
				int	n;

				n = (int)send(fd, outbuf[fd], (size_t)outlen[fd], 0);
				if (n <= 0)
				{
					char	m[128];
					int		x;

					/* pas d'input à flusher ici, on est côté écriture */
					if (ids[fd] >= 0)
					{
						x = sprintf(m, "server: client %d just left\n", ids[fd]);
						if (x > 0)
							for (int k = 0; k <= maxfd; k++)
								if (k != fd && k != sockfd && FD_ISSET(k, &master)
									&& ids[k] >= 0)
									append_bytes(&outbuf[k], &outlen[k], m, x);
					}
					FD_CLR(fd, &master);
					ids[fd] = -1;
					if (inbuf[fd])
					{
						free(inbuf[fd]);
						inbuf[fd] = 0;
						inlen[fd] = 0;
					}
					if (outbuf[fd])
					{
						free(outbuf[fd]);
						outbuf[fd] = 0;
						outlen[fd] = 0;
					}
					close(fd);
				}
				else
					consume_front(&outbuf[fd], &outlen[fd], n);
			}
		}
	}
	return (0);
}
