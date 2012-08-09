#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/un.h>
#include <sys/epoll.h>

#include "error.h"
#include "common.h"
#include "control.h"
#include "net.h"
#include "vxlan.h"


void exec_command_invalid (char * str, int sock);
void exec_command_vni_create (char * str, int sock);
void exec_command_vni_destroy (char * str, int sock);

void (* exec_command_func[]) (char * str, int sock) =
{
	exec_command_invalid,
	exec_command_vni_create,
	exec_command_vni_destroy
};



void * process_vxlan_control (void * param);
int create_unix_server_socket (char * domain);
int strtocmdtype (char * str);

void
init_vxlan_control (void)
{
	pthread_attr_t attr;

        pthread_attr_init (&attr);
        pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
        pthread_create (&vxlan.control_tid, &attr, process_vxlan_control, NULL);

        return;
}

void *
process_vxlan_control (void * param)
{
	char * c;
	int socket, accept_socket;
	char buf[CONTROL_MSG_BUF_LEN];
	
	socket = create_unix_server_socket (VXLAN_UNIX_DOMAIN);
	vxlan.control_sock = socket;

	listen (socket, 1);

	while (1) {
		memset (buf, 0, sizeof (buf));
		
		accept_socket = accept (socket, NULL, 0);

		if (read (accept_socket, buf, sizeof (buf)) < 0) {
			warn ("read(2) faild : control socket");
			shutdown (accept_socket, 1);
			close (accept_socket);
			continue;
		}

		for (c = buf; *c == ' '; c++);

		exec_command_func[strtocmdtype (c)] (c, accept_socket);

		if (shutdown (accept_socket, SHUT_RDWR) != 0) {
			error_warn ("%s : shutdown : %s", __func__, strerror (errno));
		}

		if (close (accept_socket) != 0) {
			error_warn ("%s : close : %s", __func__, strerror (errno));	
		}
	}

	shutdown (vxlan.control_sock, SHUT_RDWR);
        if (close (vxlan.control_sock) < 0)
                error_warn ("%s : close control socket failed : %s", strerror (errno));
	
	/* not reached */
	return NULL;
}


int
strtocmdtype (char * str)
{
	if (strncmp (str, "create", 6) == 0) 
		return COMMAND_TYPE_VNI_CREATE;

	if (strncmp (str, "destroy", 7) == 0)
		return COMMAND_TYPE_VNI_DESTROY;		

	return COMMAND_TYPE_INVALID;
}


void
exec_command_invalid (char * str, int sock)
{
	write (sock, CONTROL_ERRMSG, sizeof (CONTROL_ERRMSG));
	return;
}

void
exec_command_vni_create (char * str, int sock)
{
	u_int8_t vnibuf[3];
	char cmd[CONTROL_CMD_BUF_LEN];
	char vni[CONTROL_CMD_BUF_LEN];
	struct vxlan_instance * vins;
	
	if (sscanf (str, "%s %s", cmd, vni) < 2) {
		write (sock, CONTROL_ERRMSG, sizeof (CONTROL_ERRMSG));
		return;
	}
	
	strtovni (vni, vnibuf);

	if ((vins = search_vxlan_instance (vnibuf)) != NULL) {
		char errbuf[] = "this vxlan instance exists\n";
		write (sock, errbuf, strlen (errbuf));
		return;
	}
	
	vins = create_vxlan_instance (vnibuf);
	add_vxlan_instance (vins);
	init_vxlan_instance (vins);
	vxlan.vins_num++;

	char msgbuf[] = "created\n";
	write (sock, msgbuf, strlen (msgbuf));
	return;
}


void
exec_command_vni_destroy (char * str, int sock)
{
	u_int8_t vnibuf[3];
	char cmd[CONTROL_CMD_BUF_LEN];
	char vni[CONTROL_CMD_BUF_LEN];
	struct vxlan_instance * vins;
	
	if (sscanf (str, "%s %s", cmd, vni) < 2) {
		write (sock, CONTROL_ERRMSG, sizeof (CONTROL_ERRMSG));
		return;
	}
	
	strtovni (vni, vnibuf);
	if ((vins = search_vxlan_instance (vnibuf)) == NULL) {
		char errbuf[] = "this vxlan instance does not exists\n";
		write (sock, errbuf, strlen (errbuf));
		return;
	}
	
	if (destroy_vxlan_instance (vins) < 0) {
		char errbuf[] = "Delete vxlan instance failed\n";
		write (sock, errbuf, strlen (errbuf));

	} else {
		char errbuf[] = "Deleted\n";
		write (sock, errbuf, strlen (errbuf));
	}

	return;	
}




int
create_unix_server_socket (char * domain)
{
	int sock;
	struct sockaddr_un saddru;
	
	memset (&saddru, 0, sizeof (saddru));
	saddru.sun_family = AF_UNIX;
	strncpy (saddru.sun_path, domain, UNIX_PATH_MAX);

	if ((sock = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
		err (EXIT_FAILURE, "can not create unix socket");
	
	if (bind (sock, (struct sockaddr *)&saddru, sizeof (saddru)) < 0)  {
		printf ("%s exists\n", VXLAN_UNIX_DOMAIN);
		exit (-1);
	}

        return sock;
}
	
