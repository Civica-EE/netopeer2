#! /bin/sh

################################################################################
#
# Restconf helper script for non-FCGI enabled web servers
#
################################################################################

export REQUEST_URI="${REQUEST_URI/*cgi-bin/}"

env > request.env

exec /usr/bin/cgi-fcgi -bind -connect /var/run/netopeer2-fcgi.sock | tee reply.html



