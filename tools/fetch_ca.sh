#!/bin/sh
# Prints the root certificate of the IWLS API as a C string literal, ready to
# paste into IWLS_ROOT_CA in src/device/config.h.
#
#   sh tools/fetch_ca.sh
#
# Re-run it if the API's certificate chain changes; an expired or rotated root
# makes every download fail closed.
set -e
HOST=api-iwls.dfo-mpo.gc.ca
openssl s_client -connect "$HOST:443" -servername "$HOST" -showcerts </dev/null 2>/dev/null |
  awk '/BEGIN CERTIFICATE/{n++} n>0{print > ("/tmp/iwls-cert-" n ".pem")} /END CERTIFICATE/{}'
LAST=$(ls -1 /tmp/iwls-cert-*.pem | tail -n 1)
echo "// root certificate for $HOST"
echo '#define IWLS_ROOT_CA \'
sed 's/^/"/; s/$/\\n" \\/' "$LAST"
echo '""'
