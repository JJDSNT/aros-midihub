/*
 * bonami_limits.h - internal constants (keep in sync with bonami/mdns.h)
 */

#ifndef BONAMI_PRIVATE_BONAMI_LIMITS_H
#define BONAMI_PRIVATE_BONAMI_LIMITS_H

#define MDNS_OK                 0
#define MDNSERR_BADPARAM        (-1)
#define MDNSERR_NOMEM           (-2)
#define MDNSERR_TIMEOUT         (-3)
#define MDNSERR_DUPLICATE       (-4)
#define MDNSERR_NOTFOUND        (-5)
#define MDNSERR_BADTYPE         (-6)
#define MDNSERR_NETWORK         (-7)
#define MDNSERR_NOENGINE        (-8)
#define MDNSERR_NOTREADY        (-9)
#define MDNSERR_BUSY            (-10)
#define MDNSERR_CANCELLED       (-11)

#define MDNS_DOMAIN_LOCAL       "local"
#define MDNS_TYPE_SERVICES_DNS_SD "_services._dns-sd._udp"
#define MDNS_TYPE_ENVOY_NIPC    "_envoy-nipc._tcp"

#define MDNS_MAX_NAME           64
#define MDNS_MAX_TYPE           32
#define MDNS_MAX_DOMAIN         16
#define MDNS_MAX_HOST           64
#define MDNS_MAX_TXT            256

#endif /* BONAMI_PRIVATE_BONAMI_LIMITS_H */
