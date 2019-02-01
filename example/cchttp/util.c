

#include "util.h"
#include "Protocol.h"


/* this can only finds one ip, 127.0.0.1 */
inline static int
get_my_ip0(int *ips)
{
  char hostname[256];
  char *IPbuffer;
  struct hostent *host_entry;

  // To retrieve hostname
  if (gethostname(hostname, sizeof(hostname)) == -1) {
    TSError("error getting hostname");
    return -1;
  }

  // To retrieve host information
  host_entry = gethostbyname(hostname);
  if (host_entry == NULL)
    return -1;

  // To convert an Internet network
  // address into ASCII string
  IPbuffer = inet_ntoa(*((struct in_addr *)host_entry->h_addr_list[0]));
  if (IPbuffer == NULL)
    return -1;

  printf("Hostname: %s\n", hostname);
  printf("Host IP: %s", IPbuffer);
  return 1;
}

/* this only works on Linux
and assume each machine only has one public IP
ips should be a large enough pre-allocated int array for storing all public ips
 return the number of IPs found, -1 if error.
 */
int
get_my_ip(int *ips)
{
  struct ifaddrs *ifaddr, *ifa;
  int n = 0, ips_pos = 0, new_ip = 0;
  int ip_oct0, ip_oct1, ip_oct2, ip_oct3;
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddr) == -1) {
    TSError("getifaddrs error: %s\n", strerror(errno));
    return -1;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next, n++) {
    if (ifa->ifa_addr == NULL)
      continue;

    // we don't consider IPV6
    if (ifa->ifa_addr->sa_family == AF_INET) {
      if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) != 0) {
        TSDebug(PLUGIN_NAME, "getnameinfo() failed\n");
        return -1;
      }
      TSDebug(PLUGIN_NAME, "%-8s\t\taddress: <%s>\n", ifa->ifa_name, host);

      sscanf(host, "%d.%d.%d.%d", &ip_oct0, &ip_oct1, &ip_oct2, &ip_oct3);
      new_ip = (ip_oct0 << 24) | (ip_oct1 << 16) | (ip_oct2 << 8) | (ip_oct3);
      if (new_ip == ((127 << 24) | (0 << 16) | (0 << 8) | (1)))
        continue;
      else
        ips[ips_pos++] = new_ip;
    }
  }
  freeifaddrs(ifaddr);
  return ips_pos;
}

/* check a given ip address is my ip or not */
int
is_my_ip(int ip, int *ips, int n_ips)
{
  for (int i = 0; i < n_ips; i++) {
    if (ip == ips[i])
      return 1;
  }
  return 0;
}
